#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>

#include <xrpld/app/ledger/OpenLedger.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xrpl::test::jtx::oracle {

class GetAggregatePrice_test : public beast::unit_test::Suite
{
public:
    void
    testErrors()
    {
        testcase("Errors");
        using namespace jtx;
        Account const owner{"owner"};
        Account const some{"some"};
        static OraclesData kOracles = {{owner, 1}};

        {
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
            // missing base_asset
            auto ret = Oracle::aggregatePrice(env, std::nullopt, "USD", kOracles);
            BEAST_EXPECT(ret[jss::error_message].asString() == "Missing field 'base_asset'.");

            // missing quote_asset
            ret = Oracle::aggregatePrice(env, "XRP", std::nullopt, kOracles);
            BEAST_EXPECT(ret[jss::error_message].asString() == "Missing field 'quote_asset'.");

            // invalid base_asset, quote_asset
            std::vector<AnyValue> const invalidAsset = {
                kNoneTag,
                1,
                -1,
                1.2,
                "",
                "invalid",
                "a",
                "ab",
                "A",
                "AB",
                "ABCD",
                "010101",
                "012345678901234567890123456789012345678",
                "012345678901234567890123456789012345678G"};
            for (auto const& v : invalidAsset)
            {
                ret = Oracle::aggregatePrice(env, "USD", v, kOracles);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
                ret = Oracle::aggregatePrice(env, v, "USD", kOracles);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
                ret = Oracle::aggregatePrice(env, v, v, kOracles);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }

            // missing oracles array
            ret = Oracle::aggregatePrice(env, "XRP", "USD");
            BEAST_EXPECT(ret[jss::error_message].asString() == "Missing field 'oracles'.");

            // empty oracles array
            ret = Oracle::aggregatePrice(env, "XRP", "USD", OraclesData{});
            BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

            // no token pairs found
            ret = Oracle::aggregatePrice(env, "YAN", "USD", kOracles);
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");

            // invalid oracle document id
            // id doesn't exist
            ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, 2}}});
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");
            // invalid values
            std::vector<AnyValue> const invalidDocument = {kNoneTag, 1.2, -1, "", "none", "1.2"};
            for (auto const& v : invalidDocument)
            {
                ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, v}}});
                json::Value jv;
                toJson(jv, v);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }
            // missing document id
            ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, std::nullopt}}});
            BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

            // invalid owner
            ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{some, 1}}});
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");
            // missing account
            ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{std::nullopt, 1}}});
            BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

            // oracles have wrong asset pair
            env.fund(XRP(1'000), owner);
            Oracle const oracle(
                env,
                {.owner = owner,
                 .series = {{"XRP", "EUR", 740, 1}},
                 .fee = static_cast<int>(baseFee.drops())});
            ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, oracle.documentID()}}});
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");

            // invalid trim value
            std::vector<AnyValue> const invalidTrim = {kNoneTag, 0, 26, -1, 1.2, "", "none", "1.2"};
            for (auto const& v : invalidTrim)
            {
                ret =
                    Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, oracle.documentID()}}}, v);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }

            // invalid time threshold value
            std::vector<AnyValue> const invalidTime = {kNoneTag, -1, 1.2, "", "none", "1.2"};
            for (auto const& v : invalidTime)
            {
                ret = Oracle::aggregatePrice(
                    env, "XRP", "USD", {{{owner, oracle.documentID()}}}, std::nullopt, v);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }
        }

        // too many oracles
        {
            Env env(*this);
            auto const baseFee = static_cast<int>(env.current()->fees().base.drops());

            OraclesData oracles;
            for (int i = 0; i < 201; ++i)
            {
                Account const owner(std::to_string(i));
                env.fund(XRP(1'000), owner);
                Oracle const oracle(env, {.owner = owner, .documentID = i, .fee = baseFee});
                oracles.emplace_back(owner, oracle.documentID());
            }
            auto const ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles);
            BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");
        }
    }

    void
    testRpc()
    {
        testcase("RPC");
        using namespace jtx;

        auto prep = [&](Env& env, auto& oracles) {
            oracles.reserve(10);
            for (int i = 0; i < 10; ++i)
            {
                auto const baseFee = static_cast<int>(env.current()->fees().base.drops());

                Account const owner{std::to_string(i)};
                env.fund(XRP(1'000), owner);
                Oracle const oracle(
                    env,
                    {.owner = owner,
                     .documentID = rand(),
                     .series = {{"XRP", "USD", 740 + i, 1}, {"XRP", "EUR", 740, 1}},
                     .fee = baseFee});
                oracles.emplace_back(owner, oracle.documentID());
            }
        };

        // Aggregate data set includes all price oracle instances, no trimming
        // or time threshold
        {
            auto const all = testableAmendments();
            for (auto const& feats : {all - featureSingleAssetVault - featureLendingProtocol, all})
            {
                for (auto const mantissaSize : MantissaRange::getAllScales())
                {
                    // Regardless of the features enabled, RPC is controlled by
                    // the global mantissa size. And since it's a thread-local,
                    // overriding it locally won't make a difference either.
                    // This will mean all RPC will use the default of "large".
                    NumberMantissaScaleGuard const mg(mantissaSize);

                    Env env(*this, feats);
                    OraclesData oracles;
                    prep(env, oracles);
                    // entire and trimmed stats
                    auto ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles);
                    BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74.45");
                    BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 10);
                    // Short: 0.3027650354097492
                    BEAST_EXPECTS(
                        ret[jss::entire_set][jss::standard_deviation] == "0.3027650354097491666",
                        ret[jss::entire_set][jss::standard_deviation].asString());
                    BEAST_EXPECT(ret[jss::median] == "74.45");
                    BEAST_EXPECT(ret[jss::time] == 946694900);
                }
            }
        }

        // Aggregate data set includes all price oracle instances
        {
            Env env(*this);
            OraclesData oracles;
            prep(env, oracles);
            // entire and trimmed stats
            auto ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles, 20, 100);
            BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74.45");
            BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 10);
            // Short: "0.3027650354097492",
            BEAST_EXPECTS(
                ret[jss::entire_set][jss::standard_deviation] == "0.3027650354097491666",
                ret[jss::entire_set][jss::standard_deviation].asString());
            BEAST_EXPECT(ret[jss::median] == "74.45");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::mean] == "74.45");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::size].asUInt() == 6);
            // Short: "0.187082869338697",
            BEAST_EXPECTS(
                ret[jss::trimmed_set][jss::standard_deviation] == "0.1870828693386970693",
                ret[jss::trimmed_set][jss::standard_deviation].asString());
            BEAST_EXPECT(ret[jss::time] == 946694900);
        }

        // A reduced dataset, as some price oracles have data beyond three
        // updated ledgers
        {
            Env env(*this);
            auto const baseFee = static_cast<int>(env.current()->fees().base.drops());

            OraclesData oracles;
            prep(env, oracles);
            for (int i = 0; i < 3; ++i)
            {
                Oracle oracle(
                    env,
                    {.owner = oracles[i].first,
                     // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                     .documentID = asUInt(*oracles[i].second),
                     .fee = baseFee},
                    false);
                // push XRP/USD by more than three ledgers, so this price
                // oracle is not included in the dataset
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}, .fee = baseFee});
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}, .fee = baseFee});
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}, .fee = baseFee});
            }
            for (int i = 3; i < 6; ++i)
            {
                Oracle oracle(
                    env,
                    {.owner = oracles[i].first,
                     // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                     .documentID = asUInt(*oracles[i].second),
                     .fee = baseFee},
                    false);
                // push XRP/USD by two ledgers, so this price
                // is included in the dataset
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}, .fee = baseFee});
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}, .fee = baseFee});
            }

            // entire and trimmed stats
            auto ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles, 20, "200");
            BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74.6");
            BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 7);
            // Short: 0.2160246899469287
            BEAST_EXPECTS(
                ret[jss::entire_set][jss::standard_deviation] == "0.2160246899469286744",
                ret[jss::entire_set][jss::standard_deviation].asString());
            BEAST_EXPECT(ret[jss::median] == "74.6");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::mean] == "74.6");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::size].asUInt() == 5);
            // Short: 0.158113883008419
            BEAST_EXPECTS(
                ret[jss::trimmed_set][jss::standard_deviation] == "0.1581138830084189666",
                ret[jss::trimmed_set][jss::standard_deviation].asString());
            BEAST_EXPECT(ret[jss::time] == 946694900);
        }

        // Reduced data set because of the time threshold
        {
            Env env(*this);
            auto const baseFee = static_cast<int>(env.current()->fees().base.drops());

            OraclesData oracles;
            prep(env, oracles);
            for (int i = 0; i < oracles.size(); ++i)
            {
                Oracle oracle(
                    env,
                    {.owner = oracles[i].first,
                     // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                     .documentID = asUInt(*oracles[i].second),
                     .fee = baseFee},
                    false);
                // push XRP/USD by two ledgers, so this price
                // is included in the dataset
                oracle.set(UpdateArg{.series = {{"XRP", "USD", 740, 1}}, .fee = baseFee});
            }

            // entire stats only, limit lastUpdateTime to {200, 125}
            auto ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles, std::nullopt, 75);
            BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74");
            BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 8);
            BEAST_EXPECT(ret[jss::entire_set][jss::standard_deviation] == "0");
            BEAST_EXPECT(ret[jss::median] == "74");
            BEAST_EXPECT(ret[jss::time] == 946695000);
        }
    }

    void
    testNullTxReadMeta()
    {
        testcase("Null txRead metadata");
        using namespace jtx;

        // Verify that iteratePriceData handles a null txRead result
        // gracefully (returns early) rather than crashing with a
        // nullptr dereference. This simulates local data corruption
        // where a transaction referenced by sfPreviousTxnID is missing
        // from the ledger's transaction map.
        Env env(*this);
        auto const baseFee = static_cast<int>(env.current()->fees().base.drops());

        Account const owner{"owner"};
        env.fund(XRP(1'000), owner);

        // Create oracle with XRP/USD and XRP/EUR
        Oracle oracle(
            env,
            {.owner = owner,
             .series = {{"XRP", "USD", 740, 1}, {"XRP", "EUR", 840, 1}},
             .fee = baseFee});

        // Update oracle to only have XRP/EUR, pushing XRP/USD into
        // history. iteratePriceData will need to read historical tx
        // metadata to find the XRP/USD price.
        oracle.set(UpdateArg{.series = {{"XRP", "EUR", 850, 1}}, .fee = baseFee});

        OraclesData const oracles{{owner, oracle.documentID()}};

        // Precondition: with an uncorrupted oracle, the historical
        // traversal must succeed and produce a price for XRP/USD.
        // This proves the test reaches iteratePriceData's history
        // path; without it, a future change that breaks the setup
        // could turn the post-corruption assertion into a vacuous
        // pass (objectNotFound is reachable from many unrelated
        // code paths).
        {
            auto const ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles);
            BEAST_EXPECT(!ret.isMember(jss::error));
            BEAST_EXPECT(ret.isMember(jss::median));
        }

        // Simulate data corruption: modify the oracle SLE in the open
        // ledger to have a bogus sfPreviousTxnID that doesn't exist in
        // any ledger. sfPreviousTxnLgrSeq still points to a valid closed
        // ledger, so getLedgerBySeq succeeds but txRead returns null.
        auto const oracleKeylet = keylet::oracle(owner, oracle.documentID());
        uint256 const bogusTxnID{0xABCABCAB};
        bool const modified = env.app().getOpenLedger().modify(
            [&oracleKeylet, &bogusTxnID](OpenView& view, beast::Journal) -> bool {
                auto const sle = view.read(oracleKeylet);
                if (!sle)
                    return false;
                auto replacement = std::make_shared<SLE>(*sle, sle->key());
                replacement->setFieldH256(sfPreviousTxnID, bogusTxnID);
                view.rawReplace(replacement);
                return true;
            });

        // Confirm the injection actually took effect: modify must
        // report success, and re-reading the SLE must show the
        // bogus hash. Otherwise the failure-mode assertion below
        // would not be exercising the null-txRead path at all.
        BEAST_EXPECT(modified);
        if (auto const sle = env.current()->read(oracleKeylet); BEAST_EXPECT(sle))
            BEAST_EXPECT(sle->getFieldH256(sfPreviousTxnID) == bogusTxnID);

        // Query for XRP/USD using the "current" (open) ledger.
        // The oracle SLE now has a bogus sfPreviousTxnID. The current
        // oracle only has EUR, so iteratePriceData will try to read
        // history. txRead returns null for the bogus hash, and the
        // null check should cause a graceful early return instead of
        // a nullptr dereference.
        auto const ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles);
        BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");
    }

    void
    run() override
    {
        testErrors();
        testRpc();
        testNullTxReadMeta();
    }
};

BEAST_DEFINE_TESTSUITE(GetAggregatePrice, rpc, xrpl);

}  // namespace xrpl::test::jtx::oracle
