#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/mpt.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/perp.h>
#include <test/jtx/rate.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace xrpl {

class PerpMargin_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    /**
     * A USD exchange with two Live markets, XRP/USD and ETH/USD, both at index 2.0.
     */
    struct Trading
    {
        Keylet exchange;
        Keylet xrp;
        Keylet eth;
    };

    Trading
    setupTrading(Env& env)
    {
        using namespace test::jtx;
        setupMarketWorld(env, {{"XRP", "USD", 20, 1}, {"ETH", "USD", 20, 1}});
        auto const exchange = createExchange(env, owner_, usd_);
        auto args = marketArgs();
        args.indexMaxLedgers = 20;
        args.indexMaxAge = 3'600;
        env(Perp::marketSet(owner_, exchange.key, args));
        args.baseAsset = "ETH";
        env(Perp::marketSet(owner_, exchange.key, args));
        env.close();
        return {
            .exchange = exchange,
            .xrp = marketKeylet(exchange.key),
            .eth = marketKeylet(exchange.key, "ETH")};
    }

    [[nodiscard]] static Keylet
    marginKeylet(Account const& account, Keylet const& exchange, std::uint32_t subaccount = 0)
    {
        return keylet::marginAccount(account.id(), exchange.key, subaccount);
    }

    /**
     * One position written into the open ledger by seedPositions.
     */
    struct SeedPosition
    {
        Account const* account;
        Number size;
        Number entryNotional;
        Number openBuy{};
        Number openSell{};
        std::uint32_t orderCount{};
    };

    /**
     * Writes positions into a market for subaccount 0 of each account, with the market's open
     * interest and order count and the exchange's open-interest notional moved to match.
     */
    void
    seedPositions(
        Env& env,
        Keylet const& exchange,
        Keylet const& market,
        std::vector<SeedPosition> const& positions)
    {
        seed(env, market, [&](ApplyView& view, SLE::pointer const& sle) {
            Number oiLong;
            Number oiShort;
            std::uint32_t orders = 0;
            for (auto const& p : positions)
            {
                auto const accountKeylet = marginKeylet(*p.account, exchange);
                auto const account = view.peek(accountKeylet);
                if (!BEAST_EXPECT(account))
                    return;
                auto const position =
                    std::make_shared<SLE>(keylet::marginPosition(accountKeylet.key, sle->key()));
                (*position)[sfMarginAccountID] = accountKeylet.key;
                (*position)[sfMarketID] = sle->key();
                (*position)[sfSize] = p.size;
                (*position)[sfEntryNotional] = p.entryNotional;
                (*position)[sfOpenBuy] = p.openBuy;
                (*position)[sfOpenSell] = p.openSell;
                (*position)[sfOrderCount] = p.orderCount;
                (*position)[sfFundingIndex] = (*sle)[sfCumulativeFunding];
                (*position)[sfLossIndex] =
                    p.size < Number{} ? (*sle)[sfLossIndexShort] : (*sle)[sfLossIndexLong];
                view.insert(position);

                STVector256 markets = account->getFieldV256(sfMarkets);
                markets.pushBack(sle->key());
                account->setFieldV256(sfMarkets, markets);
                view.update(account);

                oiLong = oiLong + std::max(p.size, Number{});
                oiShort = oiShort + std::max(-p.size, Number{});
                orders += p.orderCount;
            }
            (*sle)[sfOpenInterestLong] = (*sle)[sfOpenInterestLong] + oiLong;
            (*sle)[sfOpenInterestShort] = (*sle)[sfOpenInterestShort] + oiShort;
            (*sle)[sfOrderCount] = (*sle)[sfOrderCount] + orders;

            auto const sleExchange = view.peek(exchange);
            if (!BEAST_EXPECT(sleExchange))
                return;
            (*sleExchange)[sfOpenInterestNotional] =
                (*sleExchange)[sfOpenInterestNotional] + (oiLong * (*sle)[sfMarkPrice]);
            view.update(sleExchange);
        });
    }

    /**
     * The exchange pseudo-account's balance of the collateral asset.
     */
    static Number
    pseudoBalance(Env& env, SLE const& exchange)
    {
        auto const pseudoID = exchange[sfAccount];
        Asset const asset = exchange[sfAsset];
        if (asset.native())
        {
            auto const pseudo = env.le(keylet::account(pseudoID));
            return pseudo ? Number{(*pseudo)[sfBalance]} : Number{};
        }
        return accountHolds(
            *env.current(),
            pseudoID,
            asset,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
    }

    /**
     * Checks invariants 1 and 2 of the spec on an exchange: the collateral mirror and the
     * accounting identity.
     */
    void
    expectAccounting(Env& env, Keylet const& exchange)
    {
        auto const sle = env.le(exchange);
        if (!BEAST_EXPECT(sle))
            return;
        Number const assets = (*sle)[sfAssetsTotal];
        BEAST_EXPECT(pseudoBalance(env, *sle) == assets);
        BEAST_EXPECT(
            assets ==
            (*sle)[sfCashTotal] + (*sle)[sfInsuranceFund] + (*sle)[sfCoverAvailable] +
                (*sle)[sfOwnerFees] + (*sle)[sfSettlementBalance] - (*sle)[sfBadDebt]);
    }

    [[nodiscard]] static Number
    cashOf(Env& env, Keylet const& marginAccount)
    {
        auto const sle = env.le(marginAccount);
        return sle ? Number{(*sle)[sfCash]} : Number{-1};
    }

    [[nodiscard]] Number
    usdOf(Env& env, Account const& account) const
    {
        return env.balance(account, usd_).value();
    }

    void
    testDepositCreatesAccount()
    {
        testcase("deposit creates account");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const exchange = createExchange(env, owner_, usd_);
        auto const margin = marginKeylet(alice_, exchange);

        auto const ownerCount = env.ownerCount(alice_);
        Number const before = usdOf(env, alice_);
        env(Perp::deposit(alice_, exchange.key, usd_(100)));
        env.close();
        {
            auto const account = env.le(margin);
            if (!BEAST_EXPECT(account))
                return;
            BEAST_EXPECT((*account)[sfAccount] == alice_.id());
            BEAST_EXPECT((*account)[sfExchangeID] == exchange.key);
            BEAST_EXPECT((*account)[sfSubaccount] == 0);
            BEAST_EXPECT((*account)[sfCash] == Number(100));
            BEAST_EXPECT(account->getFieldV256(sfMarkets).empty());
            BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 1);
            BEAST_EXPECT(usdOf(env, alice_) == before - Number{100});
            auto const sle = env.le(exchange);
            BEAST_EXPECT(sle && (*sle)[sfMarginAccountCount] == 1);
            BEAST_EXPECT(sle && (*sle)[sfCashTotal] == Number(100));
            BEAST_EXPECT(sle && (*sle)[sfAssetsTotal] == Number(100));
            expectAccounting(env, exchange);
        }

        // A second deposit to the same subaccount adds to it.
        env(Perp::deposit(alice_, exchange.key, usd_(50)));
        env.close();
        BEAST_EXPECT(cashOf(env, margin) == Number(150));
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 1);

        // Another subaccount is another margin account.
        auto sub = Perp::deposit(alice_, exchange.key, usd_(25));
        sub[sfSubaccount] = 2;
        env(sub);
        env.close();
        BEAST_EXPECT(cashOf(env, marginKeylet(alice_, exchange, 2)) == Number(25));
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 2);
        {
            auto const sle = env.le(exchange);
            BEAST_EXPECT(sle && (*sle)[sfMarginAccountCount] == 2);
            BEAST_EXPECT(sle && (*sle)[sfCashTotal] == Number(175));
        }
        expectAccounting(env, exchange);

        // Failures: missing exchange, wrong asset, more than held, cover from a trader.
        env(Perp::deposit(alice_, uint256{5}, usd_(1)), Ter(tecNO_ENTRY));
        env(Perp::deposit(alice_, exchange.key, XRP(1)), Ter(tecWRONG_ASSET));
        env(Perp::deposit(alice_, exchange.key, usd_(1'000'000)), Ter(tecINSUFFICIENT_FUNDS));
        env(Perp::deposit(alice_, exchange.key, usd_(1)),
            Txflags(tfPerpCover),
            Ter(tecNO_PERMISSION));
        env.close();

        // An account that cannot cover the margin account's reserve.
        auto const xrpExchange = createExchange(env, owner_, xrpIssue());
        Account const poor{"poor"};
        env.fund(
            env.current()->fees().accountReserve(0, 1) + env.current()->fees().base * 10, poor);
        env.close();
        env(Perp::deposit(poor, xrpExchange.key, drops(10)), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!env.le(marginKeylet(poor, xrpExchange)));
        BEAST_EXPECT(env.ownerCount(poor) == 0);

        // XRP collateral is held in drops.
        env(Perp::deposit(alice_, xrpExchange.key, XRP(10)));
        env.close();
        BEAST_EXPECT(cashOf(env, marginKeylet(alice_, xrpExchange)) == Number(10'000'000));
        expectAccounting(env, xrpExchange);
    }

    void
    testDepositDomain()
    {
        testcase("deposit domain");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        Account const domainOwner{"domainOwner"};
        std::string const credType = "Cred";
        auto const domainID = setupDomain(env, {alice_}, domainOwner, credType);

        auto const exchange = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, xrpIssue());
        create[sfDomainID] = to_string(domainID);
        env(create);
        env.close();

        // Outside the domain gives tecNO_AUTH; inside, and the domain owner, may deposit.
        env(Perp::deposit(bob_, exchange.key, XRP(100)), Ter(tecNO_AUTH));
        env(Perp::deposit(alice_, exchange.key, XRP(100)));
        env(Perp::deposit(domainOwner, exchange.key, XRP(100)));
        env.close();
        BEAST_EXPECT(!env.le(marginKeylet(bob_, exchange)));
        BEAST_EXPECT(cashOf(env, marginKeylet(alice_, exchange)) == Number(100'000'000));

        // The exchange owner's cover deposit is not a trader's deposit.
        env(Perp::deposit(owner_, exchange.key, XRP(50)), Txflags(tfPerpCover));
        env.close();

        // A lapsed membership freezes deposits, never withdrawals.
        env(credentials::deleteCred(alice_, alice_, domainOwner, credType));
        env.close();
        env(Perp::deposit(alice_, exchange.key, XRP(1)), Ter(tecNO_AUTH));
        env(Perp::withdraw(alice_, exchange.key, XRP(100)), Txflags(tfPerpDeleteAccount));
        env.close();
        BEAST_EXPECT(!env.le(marginKeylet(alice_, exchange)));
        expectAccounting(env, exchange);
    }

    void
    testCoverDepositReducesBadDebt()
    {
        testcase("cover deposit reduces bad debt");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const exchange = createExchange(env, owner_, usd_);

        auto const seedBadDebt = [&](Number const& badDebt) {
            seed(env, exchange, [&](ApplyView&, SLE::pointer const& sle) {
                (*sle)[sfBadDebt] = badDebt;
                (*sle)[sfSettlementBalance] = badDebt;
            });
        };

        // A deposit below the bad debt only reduces it.
        seedBadDebt(Number{100});
        env(Perp::deposit(owner_, exchange.key, usd_(40)), Txflags(tfPerpCover));
        {
            auto const sle = env.le(exchange);
            BEAST_EXPECT(sle && (*sle)[sfBadDebt] == Number(60));
            BEAST_EXPECT(sle && (*sle)[sfCoverAvailable] == Number(0));
            BEAST_EXPECT(sle && (*sle)[sfAssetsTotal] == Number(40));
        }
        expectAccounting(env, exchange);

        // The rest of a larger deposit becomes cover.
        env(Perp::deposit(owner_, exchange.key, usd_(150)), Txflags(tfPerpCover));
        {
            auto const sle = env.le(exchange);
            BEAST_EXPECT(sle && (*sle)[sfBadDebt] == Number(0));
            BEAST_EXPECT(sle && (*sle)[sfCoverAvailable] == Number(90));
            BEAST_EXPECT(sle && (*sle)[sfAssetsTotal] == Number(190));
            BEAST_EXPECT(sle && (*sle)[sfMarginAccountCount] == 0);
        }
        expectAccounting(env, exchange);
        BEAST_EXPECT(!env.le(marginKeylet(owner_, exchange)));

        // A cover deposit names no subaccount.
        auto withSub = Perp::deposit(owner_, exchange.key, usd_(1));
        withSub[sfSubaccount] = 0;
        env(withSub, Txflags(tfPerpCover), Ter(temMALFORMED));
    }

    void
    testWithdrawRule()
    {
        testcase("withdraw rule");
        using namespace test::jtx;

        // Positive uPnL is not withdrawable: cash 1000, uPnL +200, IM 200 gives 800.
        {
            Env env{*this, all_};
            auto const t = setupTrading(env);
            env(Perp::deposit(alice_, t.exchange.key, usd_(1'000)));
            env(Perp::deposit(bob_, t.exchange.key, usd_(1'000)));
            env.close();
            seedPositions(
                env,
                t.exchange,
                t.xrp,
                {{.account = &alice_, .size = Number{1'000}, .entryNotional = Number{1'800}},
                 {.account = &bob_, .size = Number{-1'000}, .entryNotional = Number{-1'800}}});
            env(Perp::withdraw(alice_, t.exchange.key, usd_(801)),
                Ter(tecPERP_INSUFFICIENT_MARGIN));
            Number const before = usdOf(env, alice_);
            env(Perp::withdraw(alice_, t.exchange.key, usd_(800)));
            BEAST_EXPECT(cashOf(env, marginKeylet(alice_, t.exchange)) == Number(200));
            BEAST_EXPECT(usdOf(env, alice_) == before + Number{800});
            expectAccounting(env, t.exchange);
        }

        // Negative uPnL reduces it: cash 1000, uPnL +200 and -50, IM 300 gives 650.
        {
            Env env{*this, all_};
            auto const t = setupTrading(env);
            env(Perp::deposit(alice_, t.exchange.key, usd_(1'000)));
            env(Perp::deposit(bob_, t.exchange.key, usd_(1'000)));
            env.close();
            seedPositions(
                env,
                t.exchange,
                t.xrp,
                {{.account = &alice_, .size = Number{1'000}, .entryNotional = Number{1'800}},
                 {.account = &bob_, .size = Number{-1'000}, .entryNotional = Number{-1'800}}});
            seedPositions(
                env,
                t.exchange,
                t.eth,
                {{.account = &alice_, .size = Number{-500}, .entryNotional = Number{-950}},
                 {.account = &bob_, .size = Number{500}, .entryNotional = Number{950}}});
            env(Perp::withdraw(alice_, t.exchange.key, usd_(651)),
                Ter(tecPERP_INSUFFICIENT_MARGIN));
            env(Perp::withdraw(alice_, t.exchange.key, usd_(650)));
            BEAST_EXPECT(cashOf(env, marginKeylet(alice_, t.exchange)) == Number(350));
            expectAccounting(env, t.exchange);
        }

        // Resting orders count at their worst case: a 1000 buy on a flat position needs IM 200.
        {
            Env env{*this, all_};
            auto const t = setupTrading(env);
            env(Perp::deposit(alice_, t.exchange.key, usd_(1'000)));
            env.close();
            seedPositions(
                env,
                t.exchange,
                t.xrp,
                {{.account = &alice_,
                  .size = Number{},
                  .entryNotional = Number{},
                  .openBuy = Number{1'000},
                  .orderCount = 1}});
            env(Perp::withdraw(alice_, t.exchange.key, usd_(801)),
                Ter(tecPERP_INSUFFICIENT_MARGIN));
            env(Perp::withdraw(alice_, t.exchange.key, usd_(800)));
            expectAccounting(env, t.exchange);
        }

        // A position in a market without index quorum needs its mark.
        {
            Env env{*this, all_};
            auto const t = setupTrading(env);
            env(Perp::deposit(alice_, t.exchange.key, usd_(1'000)));
            env(Perp::deposit(bob_, t.exchange.key, usd_(1'000)));
            for (int i = 0; i < 21; ++i)
                env.close();
            seedPositions(
                env,
                t.exchange,
                t.xrp,
                {{.account = &alice_, .size = Number{10}, .entryNotional = Number{20}},
                 {.account = &bob_, .size = Number{-10}, .entryNotional = Number{-20}}});
            env(Perp::withdraw(alice_, t.exchange.key, usd_(1)), Ter(tecPERP_STALE_PRICE));
        }
    }

    void
    testWithdrawTiers()
    {
        testcase("withdraw tiers");
        using namespace test::jtx;

        // Tiers 10 % to notional 100 000 and 20 % to 1 000 000, cash 20 000.
        auto const check = [&](Number const& size, Number const& refused, Number const& allowed) {
            Env env{*this, all_};
            auto const t = setupTrading(env);
            env(Perp::deposit(alice_, t.exchange.key, usd_(20'000)));
            env(Perp::deposit(bob_, t.exchange.key, usd_(20'000)));
            env.close();
            Number const notional = size * Number{2};
            seedPositions(
                env,
                t.exchange,
                t.xrp,
                {{.account = &alice_, .size = size, .entryNotional = notional},
                 {.account = &bob_, .size = -size, .entryNotional = -notional}});
            env(Perp::withdraw(alice_, t.exchange.key, STAmount{usd_.issue(), refused}),
                Ter(tecPERP_INSUFFICIENT_MARGIN));
            env(Perp::withdraw(alice_, t.exchange.key, STAmount{usd_.issue(), allowed}));
            BEAST_EXPECT(cashOf(env, marginKeylet(alice_, t.exchange)) == Number{20'000} - allowed);
        };

        // Notional 100 000 at the boundary: IM 10 000, withdrawable 10 000.
        check(Number{50'000}, Number{10'000'000'001, -6}, Number{10'000});

        // Notional 100 002 just above it: IM 100 002 x 20 % - 10 000 = 10 000.4.
        check(Number{50'001}, Number{9'999'600'001, -6}, Number{99'996, -1});
    }

    void
    testWithdrawHaircut()
    {
        testcase("withdraw haircut");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const exchange = createExchange(env, owner_, usd_);
        env(Perp::deposit(alice_, exchange.key, usd_(1'000)));
        env.close();

        // Bad debt 100 over cash total 1000: h = 10 %, so a withdrawal of 100 pays 90.
        seed(env, exchange, [](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfBadDebt] = Number{100};
            (*sle)[sfSettlementBalance] = Number{100};
        });
        Number const before = usdOf(env, alice_);
        env(Perp::withdraw(alice_, exchange.key, usd_(100)));
        BEAST_EXPECT(usdOf(env, alice_) == before + Number{90});
        BEAST_EXPECT(cashOf(env, marginKeylet(alice_, exchange)) == Number(900));
        auto const sle = env.le(exchange);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT((*sle)[sfBadDebt] == Number(90));
        BEAST_EXPECT((*sle)[sfCashTotal] == Number(900));
        BEAST_EXPECT((*sle)[sfAssetsTotal] == Number(910));
        expectAccounting(env, exchange);
    }

    void
    testWithdrawWaterfallCredit()
    {
        testcase("withdraw waterfall credit");
        using namespace test::jtx;

        // Credit outstanding while a position is open.
        {
            Env env{*this, all_};
            auto const t = setupTrading(env);
            auto const margin = marginKeylet(alice_, t.exchange);
            env(Perp::deposit(alice_, t.exchange.key, usd_(1'000)));
            env(Perp::deposit(bob_, t.exchange.key, usd_(1'000)));
            env.close();
            seedPositions(
                env,
                t.exchange,
                t.xrp,
                {{.account = &alice_, .size = Number{10}, .entryNotional = Number{20}},
                 {.account = &bob_, .size = Number{-10}, .entryNotional = Number{-20}}});
            seed(env, margin, [](ApplyView&, SLE::pointer const& sle) {
                (*sle)[sfWaterfallCredit] = Number{100};
            });
            env(Perp::withdraw(alice_, t.exchange.key, usd_(1)), Ter(tecPERP_WATERFALL_CREDIT));
            BEAST_EXPECT(cashOf(env, margin) == Number(1'000));
        }

        // A flat account repays from cash first; cash below the credit still blocks.
        {
            Env env{*this, all_};
            fundAccounts(env);
            auto const exchange = createExchange(env, owner_, usd_);
            auto const margin = marginKeylet(alice_, exchange);
            env(Perp::deposit(alice_, exchange.key, usd_(50)));
            env.close();
            seed(env, margin, [](ApplyView&, SLE::pointer const& sle) {
                (*sle)[sfWaterfallCredit] = Number{100};
            });
            env(Perp::withdraw(alice_, exchange.key, usd_(10)), Ter(tecPERP_WATERFALL_CREDIT));
            auto const account = env.le(margin);
            BEAST_EXPECT(account && (*account)[sfWaterfallCredit] == Number(100));
        }

        // Once repaid, the rest of the cash is withdrawable; the repayment clears bad debt first.
        {
            Env env{*this, all_};
            fundAccounts(env);
            auto const exchange = createExchange(env, owner_, usd_);
            auto const margin = marginKeylet(alice_, exchange);
            env(Perp::deposit(alice_, exchange.key, usd_(1'000)));
            env.close();
            seed(env, margin, [](ApplyView&, SLE::pointer const& sle) {
                (*sle)[sfWaterfallCredit] = Number{100};
            });
            seed(env, exchange, [](ApplyView&, SLE::pointer const& sle) {
                (*sle)[sfBadDebt] = Number{30};
                (*sle)[sfSettlementBalance] = Number{30};
            });
            env(Perp::withdraw(alice_, exchange.key, usd_(901)), Ter(tecPERP_INSUFFICIENT_MARGIN));
            env(Perp::withdraw(alice_, exchange.key, usd_(900)));
            auto const account = env.le(margin);
            BEAST_EXPECT(account && (*account)[sfWaterfallCredit] == Number(0));
            BEAST_EXPECT(account && (*account)[sfCash] == Number(0));
            auto const sle = env.le(exchange);
            BEAST_EXPECT(sle && (*sle)[sfBadDebt] == Number(0));
            BEAST_EXPECT(sle && (*sle)[sfInsuranceFund] == Number(70));
            expectAccounting(env, exchange);
        }
    }

    void
    testCoverWithdrawFloor()
    {
        testcase("cover withdraw floor");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const exchange = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        create[sfCoverRateMinimum] = 5'000;
        env(create);
        env.close();
        env(Perp::deposit(owner_, exchange.key, usd_(1'000)), Txflags(tfPerpCover));
        env.close();

        // Open-interest notional 10 000 at 5 % needs cover 500.
        seed(env, exchange, [](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfOpenInterestNotional] = Number{10'000};
        });
        env(Perp::withdraw(alice_, exchange.key, usd_(1)),
            Txflags(tfPerpCover),
            Ter(tecNO_PERMISSION));
        env(Perp::withdraw(owner_, exchange.key, usd_(501)),
            Txflags(tfPerpCover),
            Ter(tecPERP_INSUFFICIENT_COVER));
        Number const before = usdOf(env, owner_);
        env(Perp::withdraw(owner_, exchange.key, usd_(500)), Txflags(tfPerpCover));
        BEAST_EXPECT(usdOf(env, owner_) == before + Number{500});
        {
            auto const sle = env.le(exchange);
            BEAST_EXPECT(sle && (*sle)[sfCoverAvailable] == Number(500));
        }
        expectAccounting(env, exchange);

        // Refused while there is bad debt.
        seed(env, exchange, [](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfBadDebt] = Number{1};
            (*sle)[sfSettlementBalance] = Number{1};
        });
        env(Perp::withdraw(owner_, exchange.key, usd_(1)),
            Txflags(tfPerpCover),
            Ter(tecPERP_INSUFFICIENT_COVER));

        // With no open interest and no bad debt all of it may go.
        seed(env, exchange, [](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfBadDebt] = Number{};
            (*sle)[sfSettlementBalance] = Number{};
            (*sle)[sfOpenInterestNotional] = Number{};
        });
        env(Perp::withdraw(owner_, exchange.key, usd_(501)),
            Txflags(tfPerpCover),
            Ter(tecINSUFFICIENT_FUNDS));
        env(Perp::withdraw(owner_, exchange.key, usd_(500)), Txflags(tfPerpCover));
        auto const sle = env.le(exchange);
        BEAST_EXPECT(sle && (*sle)[sfCoverAvailable] == Number(0));
        expectAccounting(env, exchange);
    }

    void
    testOwnerFees()
    {
        testcase("owner fees");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const exchange = createExchange(env, owner_, usd_);
        auto const created = env.le(exchange);
        if (!BEAST_EXPECT(created))
            return;
        auto const pseudoID = (*created)[sfAccount];

        // Each call adds 50 to the pseudo-account and makes it all owner fees.
        auto const seedFees = [&](Number const& badDebt) {
            seed(env, exchange, [&](ApplyView& view, SLE::pointer const& sle) {
                BEAST_EXPECT(
                    isTesSuccess(accountSend(view, gw_.id(), pseudoID, usd_(50), env.journal)));
                (*sle)[sfAssetsTotal] = (*sle)[sfAssetsTotal] + Number{50};
                (*sle)[sfOwnerFees] = (*sle)[sfAssetsTotal];
                (*sle)[sfBadDebt] = badDebt;
                (*sle)[sfSettlementBalance] = badDebt;
            });
        };

        // Bad debt blocks fee withdrawals.
        seedFees(Number{10});
        env(Perp::withdraw(owner_, exchange.key, usd_(10)),
            Txflags(tfPerpOwnerFees),
            Ter(tecPERP_INSUFFICIENT_COVER));

        seed(env, exchange, [](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfBadDebt] = Number{};
            (*sle)[sfSettlementBalance] = Number{};
        });
        env(Perp::withdraw(alice_, exchange.key, usd_(10)),
            Txflags(tfPerpOwnerFees),
            Ter(tecNO_PERMISSION));
        env(Perp::withdraw(owner_, exchange.key, usd_(51)),
            Txflags(tfPerpOwnerFees),
            Ter(tecINSUFFICIENT_FUNDS));
        Number const before = usdOf(env, owner_);
        env(Perp::withdraw(owner_, exchange.key, usd_(50)), Txflags(tfPerpOwnerFees));
        BEAST_EXPECT(usdOf(env, owner_) == before + Number{50});
        {
            auto const sle = env.le(exchange);
            BEAST_EXPECT(sle && (*sle)[sfOwnerFees] == Number(0));
            BEAST_EXPECT(sle && (*sle)[sfAssetsTotal] == Number(0));
        }
        expectAccounting(env, exchange);

        // Owner fees go to a named destination.
        seedFees(Number{});
        Number const carolBefore = usdOf(env, carol_);
        auto toCarol = Perp::withdraw(owner_, exchange.key, usd_(20));
        toCarol[sfDestination] = carol_.human();
        env(toCarol, Txflags(tfPerpOwnerFees));
        BEAST_EXPECT(usdOf(env, carol_) == carolBefore + Number{20});
        auto const sle = env.le(exchange);
        BEAST_EXPECT(sle && (*sle)[sfOwnerFees] == Number(30));
        expectAccounting(env, exchange);

        // Fee withdrawals name no subaccount.
        auto withSub = Perp::withdraw(owner_, exchange.key, usd_(1));
        withSub[sfSubaccount] = 0;
        env(withSub, Txflags(tfPerpOwnerFees), Ter(temMALFORMED));
    }

    void
    testDeleteAccount()
    {
        testcase("delete account");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);
        auto const margin = marginKeylet(alice_, t.exchange);
        auto const ownerCount = env.ownerCount(alice_);
        env(Perp::deposit(alice_, t.exchange.key, usd_(100)));
        env.close();
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 1);

        // Cash left after the withdrawal blocks the deletion.
        env(Perp::withdraw(alice_, t.exchange.key, usd_(50)),
            Txflags(tfPerpDeleteAccount),
            Ter(tecHAS_OBLIGATIONS));
        env.close();
        BEAST_EXPECT(env.le(margin));

        // Emptying the account deletes it and releases its reserve.
        env(Perp::withdraw(alice_, t.exchange.key, usd_(100)), Txflags(tfPerpDeleteAccount));
        env.close();
        BEAST_EXPECT(!env.le(margin));
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount);
        {
            auto const sle = env.le(t.exchange);
            BEAST_EXPECT(sle && (*sle)[sfMarginAccountCount] == 0);
        }
        expectAccounting(env, t.exchange);

        // An account already at zero cash is deleted by a zero withdrawal.
        env(Perp::deposit(alice_, t.exchange.key, usd_(10)));
        env(Perp::withdraw(alice_, t.exchange.key, usd_(10)));
        env.close();
        BEAST_EXPECT(cashOf(env, margin) == Number(0));
        env(Perp::withdraw(alice_, t.exchange.key, usd_(0)), Txflags(tfPerpDeleteAccount));
        env.close();
        BEAST_EXPECT(!env.le(margin));

        // Withdrawing from a missing account.
        env(Perp::withdraw(alice_, t.exchange.key, usd_(1)), Ter(tecNO_ENTRY));
        env.close();

        // A position holding only a reduce-only order blocks it.
        env(Perp::deposit(alice_, t.exchange.key, usd_(100)));
        env.close();
        seedPositions(
            env,
            t.exchange,
            t.xrp,
            {{.account = &alice_, .size = Number{}, .entryNotional = Number{}, .orderCount = 1}});
        env(Perp::withdraw(alice_, t.exchange.key, usd_(100)),
            Txflags(tfPerpDeleteAccount),
            Ter(tecHAS_OBLIGATIONS));
        BEAST_EXPECT(env.le(margin));
    }

    void
    testIouTransferRateWaived()
    {
        testcase("IOU transfer rate waived");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        env(rate(gw_, 1.002));
        env.close();
        auto const exchange = createExchange(env, owner_, usd_);

        Number const before = usdOf(env, alice_);
        env(Perp::deposit(alice_, exchange.key, usd_(100)));
        env.close();
        BEAST_EXPECT(usdOf(env, alice_) == before - Number{100});
        expectAccounting(env, exchange);

        env(Perp::withdraw(alice_, exchange.key, usd_(40)));
        env.close();
        BEAST_EXPECT(usdOf(env, alice_) == before - Number{60});
        auto const sle = env.le(exchange);
        BEAST_EXPECT(sle && (*sle)[sfAssetsTotal] == Number(60));
        expectAccounting(env, exchange);
    }

    void
    testSettlement()
    {
        testcase("settlement");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);
        env(Perp::deposit(alice_, t.exchange.key, usd_(1'000)));
        env(Perp::deposit(bob_, t.exchange.key, usd_(1'000)));
        env.close();
        seedPositions(
            env,
            t.exchange,
            t.xrp,
            {{.account = &alice_, .size = Number{1'000}, .entryNotional = Number{2'000}},
             {.account = &bob_, .size = Number{-1'000}, .entryNotional = Number{-2'000}}});

        // Funding 0.01 per unit and a long loss charge of 0.002 per unit since the snapshot.
        seed(env, t.xrp, [](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfCumulativeFunding] = Number{1, -2};
            (*sle)[sfLossIndexLong] = Number{2, -3};
        });

        // The long pays 10 funding and 2 loss on its withdrawal.
        env(Perp::withdraw(alice_, t.exchange.key, usd_(1)));
        BEAST_EXPECT(cashOf(env, marginKeylet(alice_, t.exchange)) == Number(987));
        {
            auto const position =
                env.le(keylet::marginPosition(marginKeylet(alice_, t.exchange).key, t.xrp.key));
            BEAST_EXPECT(position && (*position)[sfFundingIndex] == Number(1, -2));
            BEAST_EXPECT(position && (*position)[sfLossIndex] == Number(2, -3));
            auto const sle = env.le(t.exchange);
            BEAST_EXPECT(sle && (*sle)[sfSettlementBalance] == Number(12));
        }
        expectAccounting(env, t.exchange);

        // The short receives 10 funding on its deposit.
        env(Perp::deposit(bob_, t.exchange.key, usd_(1)));
        BEAST_EXPECT(cashOf(env, marginKeylet(bob_, t.exchange)) == Number(1'011));
        {
            auto const sle = env.le(t.exchange);
            BEAST_EXPECT(sle && (*sle)[sfSettlementBalance] == Number(2));
            BEAST_EXPECT(sle && (*sle)[sfCashTotal] == Number(1'998));
        }
        expectAccounting(env, t.exchange);
    }

    void
    testMptCollateral()
    {
        testcase("MPT collateral");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        MPTTester mpt{env, gw_, kMptInitNoFund};
        mpt.create({.flags = tfMPTCanTransfer});
        mpt.authorize({.account = alice_});
        mpt.pay(gw_, alice_, 1'000);
        auto const exchange = createExchange(env, owner_, Asset{mpt});

        env(Perp::deposit(alice_, exchange.key, mpt(100)));
        env.close();
        BEAST_EXPECT(mpt.getBalance(alice_) == 900);
        BEAST_EXPECT(cashOf(env, marginKeylet(alice_, exchange)) == Number(100));
        expectAccounting(env, exchange);

        // A third party without an MPToken cannot be paid; the owner can.
        auto toBob = Perp::withdraw(alice_, exchange.key, mpt(10));
        toBob[sfDestination] = bob_.human();
        env(toBob, Ter(tecNO_AUTH));
        env(Perp::withdraw(alice_, exchange.key, mpt(40)));
        env.close();
        BEAST_EXPECT(mpt.getBalance(alice_) == 940);
        expectAccounting(env, exchange);
    }

public:
    void
    run() override
    {
        testDepositCreatesAccount();
        testDepositDomain();
        testCoverDepositReducesBadDebt();
        testWithdrawRule();
        testWithdrawTiers();
        testWithdrawHaircut();
        testWithdrawWaterfallCredit();
        testCoverWithdrawFloor();
        testOwnerFees();
        testDeleteAccount();
        testIouTransferRateWaived();
        testMptCollateral();
        testSettlement();
    }
};

BEAST_DEFINE_TESTSUITE(PerpMargin, perp, xrpl);

}  // namespace xrpl
