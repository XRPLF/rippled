#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/pay.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>

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
        auto const marginAccountID = keylet::marginAccount(alice_.id(), exchangeID, 0).key;

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

        for (auto const& features :
             {all_ - featurePerpetualFutures, all_ - featureSingleAssetVault})
        {
            Env env{*this, features};
            fundAccounts(env);
            for (auto const& [name, jv, ownerTx] : allTransactions())
            {
                testcase << "amendment gate: " << name;
                env(jv, Ter(temDISABLED));
            }
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
        auto const marginAccount = keylet::marginAccount(alice_.id(), exchange.key, 3);
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
            object[jss::subaccount] = 3;
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

public:
    void
    run() override
    {
        testAmendmentGate();
        testInnerBatchRefused();
        testFlagsMasks();
        testLedgerEntryRpc();
    }
};

BEAST_DEFINE_TESTSUITE(PerpRegistration, perp, xrpl);

}  // namespace xrpl
