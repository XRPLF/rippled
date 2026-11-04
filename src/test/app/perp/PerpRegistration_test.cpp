#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/pay.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/applySteps.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xrpl {

class PerpRegistration_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    struct NamedTx
    {
        std::string name;
        json::Value jv;
        bool ownerTx;
    };

    /**
     * One well-formed transaction of each of the ten PerpetualFutures types.
     */
    std::vector<NamedTx>
    allTransactions() const
    {
        using namespace test::jtx;
        auto const exchangeID = keylet::perpExchange(owner_.id(), SeqProxy::rawSequence(5)).key;
        auto const marketID = keylet::perpMarket(exchangeID, xrpCurrency(), toCurrency("USD")).key;
        auto const marginAccountID = keylet::marginAccount(alice_.id(), exchangeID).key;

        Perp::MarketArgs const market{
            .oracles =
                {{.owner = o1_.id(), .documentID = 1},
                 {.owner = o2_.id(), .documentID = 1},
                 {.owner = o3_.id(), .documentID = 1}},
        };

        return {
            {.name = "PerpExchangeSet",
             .jv = Perp::exchangeSet(owner_, xrpIssue()),
             .ownerTx = true},
            {.name = "PerpExchangeDelete",
             .jv = Perp::exchangeDelete(owner_, exchangeID),
             .ownerTx = true},
            {.name = "PerpMarketSet",
             .jv = Perp::marketSet(owner_, exchangeID, market),
             .ownerTx = true},
            {.name = "PerpMarketDelete",
             .jv = Perp::marketDelete(owner_, marketID),
             .ownerTx = true},
            {.name = "MarginDeposit",
             .jv = Perp::deposit(alice_, exchangeID, XRP(100)),
             .ownerTx = false},
            {.name = "MarginWithdraw",
             .jv = Perp::withdraw(alice_, exchangeID, XRP(100)),
             .ownerTx = false},
            {.name = "PerpOrderCreate",
             .jv = Perp::order(alice_, marketID, Number{2}, Number{10}),
             .ownerTx = false},
            {.name = "PerpOrderCancel", .jv = Perp::cancel(alice_, 5), .ownerTx = false},
            {.name = "PerpLiquidate",
             .jv = Perp::liquidate(liq_, marginAccountID, marketID, tfPerpLiquidateToBook),
             .ownerTx = false},
            {.name = "PerpRefresh", .jv = Perp::refresh(liq_, marketID), .ownerTx = false},
        };
    }

    void
    testAmendmentGate()
    {
        testcase("amendment gate");
        using namespace test::jtx;

        Env env{*this, all_ - featurePerpetualFutures};
        fundAccounts(env);
        for (auto const& [name, jv, ownerTx] : allTransactions())
        {
            testcase << "amendment gate: " << name;
            env(jv, Ter(temDISABLED));
        }
    }

    /**
     * Runs preflight on jv as an inner transaction of a Batch.
     */
    static NotTEC
    innerPreflight(Env& env, json::Value jv)
    {
        jv[jss::Flags] = jv.get(jss::Flags, 0u).asUInt() | tfInnerBatchTxn;
        auto const jt = env.jt(jv);
        return preflight(
                   env.app(), env.current()->rules(), uint256{1}, *jt.stx, TapBatch, env.journal)
            .ter;
    }

    /**
     * Runs preflight on jv as an ordinary transaction.
     */
    static NotTEC
    outerPreflight(Env& env, json::Value const& jv)
    {
        auto const jt = env.jt(jv);
        return preflight(env.app(), env.current()->rules(), *jt.stx, TapNone, env.journal).ter;
    }

    void
    testInnerBatchRefused()
    {
        testcase("inner batch refused");
        using namespace test::jtx;

        Env env{*this, all_};
        fundAccounts(env);

        for (auto const& [name, jv, ownerTx] : allTransactions())
        {
            testcase << "inner batch: " << name;
            BEAST_EXPECTS(isTesSuccess(outerPreflight(env, jv)), name);
            if (!ownerTx)
            {
                BEAST_EXPECTS(isTesSuccess(innerPreflight(env, jv)), name);
                continue;
            }

            BEAST_EXPECTS(innerPreflight(env, jv) == temINVALID_INNER_BATCH, name);

            auto const seq = env.seq(owner_);
            auto const batchFee = batch::calcBatchFee(env, 0, 2);
            env(batch::outer(owner_, seq, batchFee, tfAllOrNothing),
                batch::Inner(jv, seq + 1),
                batch::Inner(pay(owner_, alice_, XRP(1)), seq + 2),
                Ter(temINVALID_INNER_BATCH));
            env.close();
        }
    }

    void
    testFlagsMasks()
    {
        testcase("flags masks");
        using namespace test::jtx;

        Env env{*this, all_};
        fundAccounts(env);

        std::uint32_t constexpr kUndefinedFlag = 0x00000001;
        for (auto [name, jv, ownerTx] : allTransactions())
        {
            testcase << "flags mask: " << name;
            jv[jss::Flags] = jv.get(jss::Flags, 0u).asUInt() | kUndefinedFlag;
            env(jv, Ter(temINVALID_FLAG));
        }
    }

    void
    testLedgerEntryRpc()
    {
        testcase("ledger_entry");
        using namespace test::jtx;

        Env env{*this, all_};
        fundAccounts(env);

        auto const exchange = keylet::perpExchange(owner_.id(), SeqProxy::rawSequence(7));
        auto const usd = toCurrency("USD");
        auto const market = keylet::perpMarket(exchange.key, xrpCurrency(), usd);
        auto const marginAccount = keylet::marginAccount(alice_.id(), exchange.key);
        auto const position = keylet::marginPosition(marginAccount.key, market.key);
        auto const order = keylet::perpOrder(alice_.id(), SeqProxy::rawSequence(9));

        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            for (auto const& k : {exchange, market, marginAccount, position, order})
                view.rawInsert(std::make_shared<SLE>(k));
            return true;
        });

        auto const check = [&](json::StaticString const& rpcName,
                               json::Value const& object,
                               Keylet const& expected,
                               std::string const& entryType) {
            json::Value params;
            params[jss::ledger_index] = "current";
            params[rpcName] = object;
            auto const result =
                env.rpc("json", "ledger_entry", json::to_string(params))[jss::result];
            BEAST_EXPECTS(
                result[jss::index].asString() == to_string(expected.key),
                std::string{rpcName.cStr()} + ": " + result.toStyledString());
            BEAST_EXPECTS(
                result[jss::node][sfLedgerEntryType.jsonName].asString() == entryType,
                std::string{rpcName.cStr()});
        };

        {
            json::Value object;
            object[jss::owner] = owner_.human();
            object[jss::seq] = 7;
            check(jss::perp_exchange, object, exchange, "PerpExchange");
        }
        {
            json::Value object;
            object[jss::exchange_id] = to_string(exchange.key);
            object[jss::base_asset] = "XRP";
            object[jss::quote_asset] = "USD";
            check(jss::perp_market, object, market, "PerpMarket");
        }
        {
            json::Value object;
            object[jss::account] = alice_.human();
            object[jss::exchange_id] = to_string(exchange.key);
            check(jss::margin_account, object, marginAccount, "MarginAccount");
        }
        {
            json::Value object;
            object[jss::margin_account_id] = to_string(marginAccount.key);
            object[jss::market_id] = to_string(market.key);
            check(jss::margin_position, object, position, "MarginPosition");
        }
        {
            json::Value object;
            object[jss::account] = alice_.human();
            object[jss::seq] = 9;
            check(jss::perp_order, object, order, "PerpOrder");
        }
    }

    /**
     * The amendments that switch on 19-digit Number other than PerpetualFutures.
     */
    [[nodiscard]] FeatureBitset
    withoutVault() const
    {
        return all_ - featureSingleAssetVault - featureLendingProtocol - featureMPTokensV2;
    }

    void
    testLargeNumbersWithoutVault()
    {
        testcase("large numbers without vault");
        using namespace test::jtx;

        // 2^63 - 1 has 19 significant digits; the 16-digit regime rounds it.
        auto const holds19Digits = [] {
            Number const probe{9'223'372'036'854'775'807LL, 0};
            return probe.mantissa() == 9'223'372'036'854'775'807LL && probe.exponent() == 0;
        };

        // PerpetualFutures alone switches the transaction rules to 19-digit Number.
        {
            Env env{*this, withoutVault()};
            auto const& rules = env.current()->rules();
            BEAST_EXPECT(useRulesGuards(rules));
            CurrentTransactionRulesGuard const guard(rules);
            BEAST_EXPECT(Number::getMantissaScale() != MantissaRange::MantissaScale::Small);
            BEAST_EXPECT(holds19Digits());
        }
        {
            Env env{*this, withoutVault() - featurePerpetualFutures};
            CurrentTransactionRulesGuard const guard(env.current()->rules());
            BEAST_EXPECT(Number::getMantissaScale() == MantissaRange::MantissaScale::Small);
            BEAST_EXPECT(!holds19Digits());
        }

        // A fill of 123 at 2.000000000000001 keeps its 18-digit notional exactly.
        Env env{*this, withoutVault()};
        setupMarketWorld(env);
        auto const exchange = createExchange(env, owner_, usd_);
        auto args = marketArgs();
        args.priceTick = Number{1, -15};
        env(Perp::marketSet(owner_, exchange.key, args));
        env.close();
        auto const market = marketKeylet(exchange.key);
        for (auto const* trader : {&alice_, &bob_})
            env(Perp::deposit(*trader, exchange.key, usd_(1'000)));
        env.close();
        Number const price{2'000'000'000'000'001, -15};
        auto ask = Perp::order(bob_, market.key, price, Number{123});
        ask[jss::Flags] = tfPerpSell;
        env(ask);
        env(Perp::order(alice_, market.key, price, Number{123}));
        auto const position = env.le(
            keylet::marginPosition(
                keylet::marginAccount(alice_.id(), exchange.key).key, market.key));
        BEAST_EXPECT(
            position && (*position)[sfEntryNotional] == Number(246'000'000'000'000'123, -15));
    }

    void
    testNoVaultDependency()
    {
        testcase("no vault dependency");
        using namespace test::jtx;

        // Every transaction type applies with SingleAssetVault, LendingProtocol and MPTokensV2
        // disabled.
        Env env{*this, withoutVault()};
        BEAST_EXPECT(!env.current()->rules().enabled(featureSingleAssetVault));
        setupMarketWorld(env);
        auto const exchange = createExchange(env, owner_, usd_);
        env(Perp::marketSet(owner_, exchange.key, marketArgs()));
        env.close();
        auto const market = marketKeylet(exchange.key);
        BEAST_EXPECT(env.le(market));

        env(Perp::deposit(owner_, exchange.key, usd_(100)), Txflags(tfPerpCover));
        for (auto const* trader : {&alice_, &bob_, &liq_})
            env(Perp::deposit(*trader, exchange.key, usd_(1'000)));
        env.close();

        auto ask = Perp::order(bob_, market.key, Number{2}, Number{10});
        ask[jss::Flags] = tfPerpSell;
        env(ask);
        env(Perp::order(alice_, market.key, Number{2}, Number{10}));
        auto const restingSeq = env.seq(alice_);
        env(Perp::order(alice_, market.key, Number{19, -1}, Number{1}));
        env.close();
        auto const alicePosition = keylet::marginPosition(
            keylet::marginAccount(alice_.id(), exchange.key).key, market.key);
        auto const position = env.le(alicePosition);
        BEAST_EXPECT(position && (*position)[sfSize] == Number(10));

        env(Perp::cancel(alice_, restingSeq));
        env(Perp::refresh(liq_, market.key));
        env(Perp::refreshAccount(liq_, keylet::marginAccount(bob_.id(), exchange.key).key));
        env(Perp::liquidate(
                liq_,
                keylet::marginAccount(alice_.id(), exchange.key).key,
                market.key,
                tfPerpLiquidateToBook),
            Ter(tecPERP_HEALTHY));
        env(Perp::withdraw(liq_, exchange.key, usd_(1'000)), Txflags(tfPerpDeleteAccount));
        env.close();
        BEAST_EXPECT(!env.le(keylet::marginAccount(liq_.id(), exchange.key)));

        auto settle = Perp::marketUpdate(owner_, market.key);
        settle[jss::Flags] = tfPerpMarketSettle;
        env(settle);
        env.close();
        env(Perp::refreshAccount(liq_, keylet::marginAccount(alice_.id(), exchange.key).key));
        env(Perp::refreshAccount(liq_, keylet::marginAccount(bob_.id(), exchange.key).key));
        env.close();
        BEAST_EXPECT(!env.le(alicePosition));
        env(Perp::marketDelete(owner_, market.key));
        env.close();
        BEAST_EXPECT(!env.le(market));
    }

public:
    void
    run() override
    {
        testAmendmentGate();
        testInnerBatchRefused();
        testFlagsMasks();
        testLedgerEntryRpc();
        testLargeNumbersWithoutVault();
        testNoVaultDependency();
    }
};

BEAST_DEFINE_TESTSUITE(PerpRegistration, perp, xrpl);

}  // namespace xrpl
