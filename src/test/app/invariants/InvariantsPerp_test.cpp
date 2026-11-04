#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/perp.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsPerp_test : public InvariantsBase
{
    using Account = jtx::Account;
    using Env = jtx::Env;
    using Perp = jtx::Perp;

    FeatureBitset const all_{jtx::testableAmendments()};

    Account const owner_{"owner"};
    Account const alice_{"alice"};
    Account const bob_{"bob"};
    Account const carol_{"carol"};
    Account const dave_{"dave"};
    Account const gw_{"gw"};
    Account const o1_{"o1"};
    Account const o2_{"o2"};
    Account const o3_{"o3"};
    jtx::IOU const usd_{gw_["USD"]};

    /**
     * The entries setup creates: alice long 10 and bob short 10 in XRP/USD, alice's resting bid
     * of 5, carol's resting bid of 1 in the isolated-only ETH/USD market, and dave's flat
     * account.
     */
    struct World
    {
        Keylet exchange;
        Keylet xrp;
        Keylet eth;
        Keylet vault;
        Keylet spareVault;
        Keylet aliceOrder;
        Keylet carolOrder;
    };

    using Change = std::function<bool(World const&, ApplyContext&)>;

    [[nodiscard]] json::Value
    marketTx(Keylet const& exchange, std::string const& base, std::uint32_t flags) const
    {
        Perp::MarketArgs args;
        args.oracles = {
            {.owner = o1_.id(), .documentID = 1},
            {.owner = o2_.id(), .documentID = 1},
            {.owner = o3_.id(), .documentID = 1}};
        args.baseAsset = base;
        args.indexMaxLedgers = 1'000;
        args.indexMaxAge = 100'000;
        args.tiers = {
            {.notionalMax = Number{1'000'000},
             .initialMarginRate = 4'000,
             .maintenanceMarginRate = 2'000}};
        auto jv = Perp::marketSet(owner_, exchange.key, args);
        if (flags != 0)
            jv[jss::Flags] = flags;
        return jv;
    }

    [[nodiscard]] static json::Value
    order(
        Account const& account,
        Keylet const& market,
        Number const& price,
        Number const& size,
        std::uint32_t flags = 0)
    {
        auto jv = Perp::order(account, market.key, price, size);
        if (flags != 0)
            jv[jss::Flags] = flags;
        return jv;
    }

    World
    setup(Env& env)
    {
        using namespace jtx;
        env.fund(XRP(100'000), owner_, alice_, bob_, carol_, dave_, gw_, o1_, o2_, o3_);
        env.close();
        for (auto const& account : {owner_, alice_, bob_, carol_, dave_})
        {
            env.trust(usd_(1'000'000), account);
            env(pay(gw_, account, usd_(100'000)));
        }
        env.close(env.timeKeeper().now() + (oracle::kTestStartTime - kEpochOffset));

        oracle::DataSeries const series{{"XRP", "USD", 1'000, 1}, {"ETH", "USD", 1'000, 1}};
        for (auto const& provider : {o1_, o2_, o3_})
        {
            oracle::Oracle const quote{
                env, oracle::CreateArg{.owner = provider.id(), .documentID = 1, .series = series}};
        }
        env.close();

        Vault const vault{.env = env};
        auto [vaultTx, vaultKeylet] = vault.create({.owner = owner_, .asset = usd_});
        env(vaultTx);
        auto [spareTx, spareKeylet] = vault.create({.owner = owner_, .asset = usd_});
        env(spareTx);
        env.close();
        env(vault.deposit({.depositor = owner_, .id = vaultKeylet.key, .amount = usd_(1'000)}));
        env(vault.deposit({.depositor = owner_, .id = spareKeylet.key, .amount = usd_(1'000)}));
        env.close();

        auto const exchange = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        create[sfVaultID] = to_string(vaultKeylet.key);
        create[sfVaultLossWindow] = 86'400;
        create[sfVaultLossLimit] = 100'000;
        env(create);
        env.close();

        auto const xrp = keylet::perpMarket(exchange.key, toCurrency("XRP"), toCurrency("USD"));
        auto const eth = keylet::perpMarket(exchange.key, toCurrency("ETH"), toCurrency("USD"));
        env(marketTx(exchange, "XRP", 0));
        env(marketTx(exchange, "ETH", tfPerpIsolatedOnly));
        env.close();

        env(Perp::deposit(alice_, exchange.key, usd_(2'000)));
        env(Perp::deposit(bob_, exchange.key, usd_(2'000)));
        env(Perp::deposit(carol_, exchange.key, usd_(1'000)));
        env(Perp::deposit(dave_, exchange.key, usd_(100)));
        env.close();

        env(order(alice_, xrp, Number(100), Number(10)));
        env.close();
        env(order(bob_, xrp, Number(100), Number(10), tfPerpSell));
        env.close();

        auto const aliceOrder = Perp::orderKeylet(env, alice_);
        env(order(alice_, xrp, Number(95), Number(5)));
        auto const carolOrder = Perp::orderKeylet(env, carol_);
        env(order(carol_, eth, Number(95), Number(1)));
        env.close();

        BEAST_EXPECT(env.le(aliceOrder));
        BEAST_EXPECT(env.le(carolOrder));
        return {
            .exchange = exchange,
            .xrp = xrp,
            .eth = eth,
            .vault = vaultKeylet,
            .spareVault = spareKeylet,
            .aliceOrder = aliceOrder,
            .carolOrder = carolOrder};
    }

    [[nodiscard]] static Keylet
    margin(World const& w, Account const& account)
    {
        return keylet::marginAccount(account.id(), w.exchange.key, 0);
    }

    [[nodiscard]] static Keylet
    position(World const& w, Account const& account, Keylet const& market)
    {
        return keylet::marginPosition(margin(w, account).key, market.key);
    }

    /**
     * Applies edit to the entry at keylet in the context's view.
     */
    static bool
    edit(ApplyContext& ac, Keylet const& keylet, std::function<void(SLE&)> const& change)
    {
        auto const sle = ac.view().peek(keylet);
        if (!sle)
            return false;
        change(*sle);
        ac.view().update(sle);
        return true;
    }

    static void
    add(SLE& sle, SF_NUMBER const& field, Number const& amount)
    {
        sle[field] = sle[field] + amount;
    }

    /**
     * Builds the world, applies change in a transaction of type, and runs the invariants.
     */
    void
    check(
        std::vector<std::string> const& logs,
        Change const& change,
        TxType type,
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        TER initialResult = tesSUCCESS,
        std::source_location const& loc = std::source_location::current())
    {
        auto env = makeEnv(all_);
        auto const world = setup(env);
        doInvariantCheck(
            std::move(env),
            alice_,
            bob_,
            logs,
            [&](Account const&, Account const&, ApplyContext& ac) { return change(world, ac); },
            XRPAmount{},
            STTx{type, [](STObject&) {}},
            ters,
            loc,
            initialResult);
    }

    void
    testCollateralMirror()
    {
        testcase("1 collateral mirror");

        check(
            {"perp exchange assets total does not match its pseudo-account balance"},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.exchange, [](SLE& sle) {
                    add(sle, sfAssetsTotal, Number{1});
                    add(sle, sfInsuranceFund, Number{1});
                });
            },
            ttPERP_ORDER_CREATE);

        // The pseudo-account's balance moves while the exchange is untouched.
        check(
            {"perp exchange assets total does not match its pseudo-account balance"},
            [&](World const& w, ApplyContext& ac) {
                auto const exchange = ac.view().read(w.exchange);
                if (!exchange)
                    return false;
                return isTesSuccess(accountSend(
                    ac.view(),
                    (*exchange)[sfAccount],
                    owner_.id(),
                    usd_(1).value(),
                    ac.journal,
                    {},
                    WaiveTransferFee::Yes));
            },
            ttPERP_REFRESH);
    }

    void
    testAccountingIdentity()
    {
        testcase("2 accounting identity");

        check(
            {"perp exchange accounting identity does not hold"},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.exchange, [](SLE& sle) { add(sle, sfInsuranceFund, Number{1}); });
            },
            ttPERP_LIQUIDATE);

        // Bad debt counts against the assets.
        check(
            {"perp exchange accounting identity does not hold"},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.exchange, [](SLE& sle) { add(sle, sfBadDebt, Number{1}); });
            },
            ttPERP_LIQUIDATE);

        check(
            {"perp exchange cash total does not match its margin accounts' cash"},
            [&](World const& w, ApplyContext& ac) {
                return edit(ac, margin(w, alice_), [](SLE& sle) { add(sle, sfCash, Number{1}); });
            },
            ttMARGIN_DEPOSIT);

        // A failed transaction is checked too.
        check(
            {"perp exchange accounting identity does not hold"},
            [](World const& w, ApplyContext& ac) {
                return edit(
                    ac, w.exchange, [](SLE& sle) { add(sle, sfCoverAvailable, Number{1}); });
            },
            ttMARGIN_WITHDRAW,
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            tecPERP_INSUFFICIENT_MARGIN);
    }

    void
    testZeroSumFills()
    {
        testcase("3 zero-sum fills");

        check(
            {"perp fills in a live market are not zero-sum",
             "perp live market open interest is unbalanced"},
            [&](World const& w, ApplyContext& ac) {
                return edit(
                           ac,
                           position(w, alice_, w.xrp),
                           [](SLE& sle) { add(sle, sfSize, Number{1}); }) &&
                    edit(ac, w.xrp, [](SLE& sle) { add(sle, sfOpenInterestLong, Number{1}); });
            },
            ttPERP_ORDER_CREATE);
    }

    void
    testSettledCloseOneWay()
    {
        testcase("4 settled markets close one way");

        check(
            {"perp position in a settled market changed other than to zero"},
            [&](World const& w, ApplyContext& ac) {
                return edit(
                           ac,
                           w.xrp,
                           [](SLE& sle) {
                               sle.setFieldU8(sfMarketStatus, 2);
                               add(sle, sfOpenInterestLong, Number{-5});
                               add(sle, sfOpenInterestShort, Number{-5});
                           }) &&
                    edit(ac,
                         position(w, alice_, w.xrp),
                         [](SLE& sle) { add(sle, sfSize, Number{-5}); }) &&
                    edit(ac, position(w, bob_, w.xrp), [](SLE& sle) {
                           add(sle, sfSize, Number{5});
                       });
            },
            ttPERP_REFRESH);
    }

    void
    testOpenInterest()
    {
        testcase("5 open interest");

        check(
            {"perp market open interest does not match its positions"},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.xrp, [](SLE& sle) {
                    add(sle, sfOpenInterestLong, Number{1});
                    add(sle, sfOpenInterestShort, Number{1});
                });
            },
            ttPERP_REFRESH);
    }

    void
    testNonNegative()
    {
        testcase("6 non-negative balances");

        check(
            {"perp exchange InsuranceFund is negative"},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.exchange, [](SLE& sle) {
                    add(sle, sfInsuranceFund, Number{-1});
                    add(sle, sfCoverAvailable, Number{1});
                });
            },
            ttPERP_LIQUIDATE);

        check(
            {"perp margin account waterfall credit is negative"},
            [&](World const& w, ApplyContext& ac) {
                return edit(ac, margin(w, alice_), [](SLE& sle) {
                    add(sle, sfWaterfallCredit, Number{-1});
                });
            },
            ttPERP_LIQUIDATE);

        check(
            {"perp margin account without markets has negative cash"},
            [&](World const& w, ApplyContext& ac) {
                return edit(
                           ac,
                           margin(w, dave_),
                           [](SLE& sle) { add(sle, sfCash, Number{-101}); }) &&
                    edit(ac, w.exchange, [](SLE& sle) {
                           add(sle, sfCashTotal, Number{-101});
                           add(sle, sfSettlementBalance, Number{101});
                       });
            },
            ttPERP_REFRESH);
    }

    void
    testMonotoneIndexes()
    {
        testcase("7 monotone loss indexes");

        check(
            {"perp market loss index fell"},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.xrp, [](SLE& sle) { add(sle, sfLossIndexShort, Number{-1}); });
            },
            ttPERP_LIQUIDATE);
    }

    void
    testLotMultiples()
    {
        testcase("8 lot multiples");

        check(
            {"perp size is not a multiple of the lot size"},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.aliceOrder, [](SLE& sle) { add(sle, sfSize, Number{5, -1}); });
            },
            ttPERP_ORDER_CREATE);

        check(
            {"perp size is not a multiple of the lot size"},
            [&](World const& w, ApplyContext& ac) {
                return edit(ac, position(w, alice_, w.xrp), [](SLE& sle) {
                    add(sle, sfOpenBuy, Number{5, -1});
                });
            },
            ttPERP_ORDER_CREATE);
    }

    void
    testCleanDeletion()
    {
        testcase("9 clean deletion");

        check(
            {"perp position deleted with size or orders"},
            [&](World const& w, ApplyContext& ac) {
                auto const sle = ac.view().peek(position(w, carol_, w.eth));
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return edit(ac, margin(w, carol_), [](SLE& account) {
                    account.setFieldV256(sfMarkets, STVector256{});
                });
            },
            ttPERP_ORDER_CANCEL);

        check(
            {"perp margin account deleted with cash, credit or markets"},
            [&](World const& w, ApplyContext& ac) {
                auto const sle = ac.view().peek(margin(w, dave_));
                if (!sle)
                    return false;
                ac.view().erase(sle);
                return edit(ac, w.exchange, [](SLE& exchange) {
                    add(exchange, sfCashTotal, Number{-100});
                    add(exchange, sfSettlementBalance, Number{100});
                });
            },
            ttMARGIN_WITHDRAW);
    }

    void
    testBoundedLists()
    {
        testcase("10 bounded lists");

        check(
            {"perp margin account lists too many markets"},
            [&](World const& w, ApplyContext& ac) {
                return edit(ac, margin(w, alice_), [](SLE& sle) {
                    STVector256 markets = sle.getFieldV256(sfMarkets);
                    for (std::uint64_t i = 1; i <= 16; ++i)
                        markets.pushBack(uint256{i});
                    sle.setFieldV256(sfMarkets, markets);
                });
            },
            ttPERP_ORDER_CREATE);

        check(
            {"perp isolated-only position is not its account's only position"},
            [&](World const& w, ApplyContext& ac) {
                return edit(ac, margin(w, carol_), [&](SLE& sle) {
                    STVector256 markets = sle.getFieldV256(sfMarkets);
                    markets.pushBack(w.xrp.key);
                    sle.setFieldV256(sfMarkets, markets);
                });
            },
            ttPERP_ORDER_CREATE);
    }

    void
    testPseudoAccountOwnership()
    {
        testcase("exchange pseudo-account owns only its collateral holding");

        check(
            {"may not own RippleState"},
            [&](World const& w, ApplyContext& ac) {
                auto const exchange = ac.view().read(w.exchange);
                if (!exchange)
                    return false;
                AccountID const pseudo = (*exchange)[sfAccount];
                return ac.view()
                    .dirInsert(
                        keylet::ownerDir(pseudo),
                        keylet::trustLine(alice_.id(), usd_.issue()).key,
                        describeOwnerDir(pseudo))
                    .has_value();
            },
            ttPERP_ORDER_CREATE);
    }

    void
    testUnmodifiableFields()
    {
        std::vector<std::pair<std::string, Change>> const changes{
            {"exchange ParameterDelay",
             [](World const& w, ApplyContext& ac) {
                 return edit(ac, w.exchange, [](SLE& sle) {
                     sle.setFieldU32(sfParameterDelay, sle.getFieldU32(sfParameterDelay) + 1);
                 });
             }},
            {"market LotSize",
             [](World const& w, ApplyContext& ac) {
                 return edit(ac, w.xrp, [](SLE& sle) { sle[sfLotSize] = Number{2}; });
             }},
            {"margin account Subaccount",
             [&](World const& w, ApplyContext& ac) {
                 return edit(
                     ac, margin(w, alice_), [](SLE& sle) { sle.setFieldU32(sfSubaccount, 7); });
             }},
            {"position OwnerNode",
             [&](World const& w, ApplyContext& ac) {
                 return edit(ac, position(w, alice_, w.xrp), [](SLE& sle) {
                     sle.setFieldU64(sfOwnerNode, 5);
                 });
             }},
            {"order LimitPrice",
             [](World const& w, ApplyContext& ac) {
                 return edit(ac, w.aliceOrder, [](SLE& sle) { sle[sfLimitPrice] = Number{96}; });
             }},
        };
        for (auto const& [label, change] : changes)
        {
            testcase("fixed field: " + label);
            check({"changed an unchangeable field"}, change, ttPERP_REFRESH);
        }
    }

    /**
     * Moves amount from the bound vault to the exchange as a waterfall draw credited to alice.
     */
    [[nodiscard]] bool
    drawVault(World const& w, ApplyContext& ac, STAmount const& amount) const
    {
        auto const vault = ac.view().peek(w.vault);
        auto const exchange = ac.view().peek(w.exchange);
        if (!vault || !exchange)
            return false;
        if (!isTesSuccess(accountSend(
                ac.view(),
                (*vault)[sfAccount],
                (*exchange)[sfAccount],
                amount,
                ac.journal,
                {},
                WaiveTransferFee::Yes)) ||
            !isTesSuccess(adjustVaultCash(vault, -amount, ac.journal)))
            return false;
        ac.view().update(vault);
        Number const value = amount;
        add(*exchange, sfAssetsTotal, value);
        add(*exchange, sfCashTotal, value);
        ac.view().update(exchange);
        return edit(ac, margin(w, alice_), [&](SLE& sle) {
            add(sle, sfCash, value);
            add(sle, sfWaterfallCredit, value);
        });
    }

    /**
     * Moves amount of alice's cash to the bound vault as a fee share.
     */
    [[nodiscard]] bool
    payVault(World const& w, ApplyContext& ac, STAmount const& amount) const
    {
        auto const vault = ac.view().peek(w.vault);
        auto const exchange = ac.view().peek(w.exchange);
        if (!vault || !exchange)
            return false;
        if (!isTesSuccess(accountSend(
                ac.view(),
                (*exchange)[sfAccount],
                (*vault)[sfAccount],
                amount,
                ac.journal,
                {},
                WaiveTransferFee::Yes)) ||
            !isTesSuccess(adjustVaultCash(vault, amount, ac.journal)))
            return false;
        ac.view().update(vault);
        Number const value = amount;
        add(*exchange, sfAssetsTotal, -value);
        add(*exchange, sfCashTotal, -value);
        ac.view().update(exchange);
        return edit(ac, margin(w, alice_), [&](SLE& sle) { add(sle, sfCash, -value); });
    }

    void
    testVaultWriters()
    {
        testcase("perp vault writes pass ValidVault");
        std::initializer_list<TER> const pass{tesSUCCESS, tesSUCCESS};

        check(
            {},
            [](World const& w, ApplyContext& ac) {
                return edit(ac, w.spareVault, [&](SLE& sle) {
                    sle.setFieldH256(sfPerpExchangeID, w.exchange.key);
                });
            },
            ttPERP_EXCHANGE_SET,
            pass);

        for (auto const type : {ttPERP_ORDER_CREATE, ttPERP_LIQUIDATE})
        {
            testcase("vault fee share paid by " + std::to_string(static_cast<int>(type)));
            check(
                {},
                [&](World const& w, ApplyContext& ac) { return payVault(w, ac, usd_(10)); },
                type,
                pass);
        }

        // Every transaction that settles an account may draw the vault in the waterfall.
        for (auto const type :
             {ttMARGIN_DEPOSIT,
              ttPERP_LIQUIDATE,
              ttPERP_REFRESH,
              ttPERP_ORDER_CREATE,
              ttMARGIN_WITHDRAW})
        {
            testcase("vault draw by " + std::to_string(static_cast<int>(type)));
            check(
                {},
                [&](World const& w, ApplyContext& ac) { return drawVault(w, ac, usd_(10)); },
                type,
                pass);
        }
    }

public:
    void
    run() override
    {
        testCollateralMirror();
        testAccountingIdentity();
        testZeroSumFills();
        testSettledCloseOneWay();
        testOpenInterest();
        testNonNegative();
        testMonotoneIndexes();
        testLotMultiples();
        testCleanDeletion();
        testBoundedLists();
        testPseudoAccountOwnership();
        testUnmodifiableFields();
        testVaultWriters();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsPerp, perp, xrpl);

}  // namespace xrpl::test
