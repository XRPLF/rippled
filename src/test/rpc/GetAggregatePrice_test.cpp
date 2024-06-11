//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/Oracle.h>

namespace ripple {
namespace test {
namespace jtx {
namespace oracle {

class GetAggregatePrice_test : public beast::unit_test::suite
{
public:
    void
    testErrors()
    {
        testcase("Errors");
        using namespace jtx;
        Account const owner{"owner"};
        Account const some{"some"};
        static OraclesData oracles = {{owner, 1}};

        {
            Env env(*this);
            // missing base_asset
            auto ret =
                Oracle::aggregatePrice(env, std::nullopt, "USD", oracles);
            BEAST_EXPECT(
                ret[jss::error_message].asString() ==
                "Missing field 'base_asset'.");

            // missing quote_asset
            ret = Oracle::aggregatePrice(env, "XRP", std::nullopt, oracles);
            BEAST_EXPECT(
                ret[jss::error_message].asString() ==
                "Missing field 'quote_asset'.");

            // invalid base_asset, quote_asset
            std::vector<AnyValue> invalidAsset = {
                NoneTag,
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
                ret = Oracle::aggregatePrice(env, "USD", v, oracles);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
                ret = Oracle::aggregatePrice(env, v, "USD", oracles);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
                ret = Oracle::aggregatePrice(env, v, v, oracles);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }

            // missing oracles array
            ret = Oracle::aggregatePrice(env, "XRP", "USD");
            BEAST_EXPECT(
                ret[jss::error_message].asString() ==
                "Missing field 'oracles'.");

            // empty oracles array
            ret = Oracle::aggregatePrice(env, "XRP", "USD", OraclesData{});
            BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

            // no token pairs found
            ret = Oracle::aggregatePrice(env, "YAN", "USD", oracles);
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");

            // invalid oracle document id
            // id doesn't exist
            ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, 2}}});
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");
            // invalid values
            std::vector<AnyValue> invalidDocument = {
                NoneTag, 1.2, -1, "", "none", "1.2"};
            for (auto const& v : invalidDocument)
            {
                ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{owner, v}}});
                Json::Value jv;
                toJson(jv, v);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }
            // missing document id
            ret = Oracle::aggregatePrice(
                env, "XRP", "USD", {{{owner, std::nullopt}}});
            BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

            // invalid owner
            ret = Oracle::aggregatePrice(env, "XRP", "USD", {{{some, 1}}});
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");
            // missing account
            ret = Oracle::aggregatePrice(
                env, "XRP", "USD", {{{std::nullopt, 1}}});
            BEAST_EXPECT(ret[jss::error].asString() == "oracleMalformed");

            // oracles have wrong asset pair
            env.fund(XRP(1'000), owner);
            Oracle oracle(
                env, {.owner = owner, .series = {{"XRP", "EUR", 740, 1}}});
            ret = Oracle::aggregatePrice(
                env, "XRP", "USD", {{{owner, oracle.documentID()}}});
            BEAST_EXPECT(ret[jss::error].asString() == "objectNotFound");

            // invalid trim value
            std::vector<AnyValue> invalidTrim = {
                NoneTag, 0, 26, -1, 1.2, "", "none", "1.2"};
            for (auto const& v : invalidTrim)
            {
                ret = Oracle::aggregatePrice(
                    env, "XRP", "USD", {{{owner, oracle.documentID()}}}, v);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }

            // invalid time threshold value
            std::vector<AnyValue> invalidTime = {
                NoneTag, -1, 1.2, "", "none", "1.2"};
            for (auto const& v : invalidTime)
            {
                ret = Oracle::aggregatePrice(
                    env,
                    "XRP",
                    "USD",
                    {{{owner, oracle.documentID()}}},
                    std::nullopt,
                    v);
                BEAST_EXPECT(ret[jss::error].asString() == "invalidParams");
            }
        }

        // too many oracles
        {
            Env env(*this);
            OraclesData oracles;
            for (int i = 0; i < 201; ++i)
            {
                Account const owner(std::to_string(i));
                env.fund(XRP(1'000), owner);
                Oracle oracle(env, {.owner = owner, .documentID = i});
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
                Account const owner{std::to_string(i)};
                env.fund(XRP(1'000), owner);
                Oracle oracle(
                    env,
                    {.owner = owner,
                     .documentID = rand(),
                     .series = {
                         {"XRP", "USD", 740 + i, 1}, {"XRP", "EUR", 740, 1}}});
                oracles.emplace_back(owner, oracle.documentID());
            }
        };

        // Aggregate data set includes all price oracle instances, no trimming
        // or time threshold
        {
            Env env(*this);
            OraclesData oracles;
            prep(env, oracles);
            // entire and trimmed stats
            auto ret = Oracle::aggregatePrice(env, "XRP", "USD", oracles);
            BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74.45");
            BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 10);
            BEAST_EXPECT(
                ret[jss::entire_set][jss::standard_deviation] ==
                "0.3027650354097492");
            BEAST_EXPECT(ret[jss::median] == "74.45");
            BEAST_EXPECT(ret[jss::time] == 946694900);
        }

        // Aggregate data set includes all price oracle instances
        {
            Env env(*this);
            OraclesData oracles;
            prep(env, oracles);
            // entire and trimmed stats
            auto ret =
                Oracle::aggregatePrice(env, "XRP", "USD", oracles, 20, 100);
            BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74.45");
            BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 10);
            BEAST_EXPECT(
                ret[jss::entire_set][jss::standard_deviation] ==
                "0.3027650354097492");
            BEAST_EXPECT(ret[jss::median] == "74.45");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::mean] == "74.45");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::size].asUInt() == 6);
            BEAST_EXPECT(
                ret[jss::trimmed_set][jss::standard_deviation] ==
                "0.187082869338697");
            BEAST_EXPECT(ret[jss::time] == 946694900);
        }

        // A reduced dataset, as some price oracles have data beyond three
        // updated ledgers
        {
            Env env(*this);
            OraclesData oracles;
            prep(env, oracles);
            for (int i = 0; i < 3; ++i)
            {
                Oracle oracle(
                    env,
                    {.owner = oracles[i].first,
                     .documentID = asUInt(*oracles[i].second)},
                    false);
                // push XRP/USD by more than three ledgers, so this price
                // oracle is not included in the dataset
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}});
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}});
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}});
            }
            for (int i = 3; i < 6; ++i)
            {
                Oracle oracle(
                    env,
                    {.owner = oracles[i].first,
                     .documentID = asUInt(*oracles[i].second)},
                    false);
                // push XRP/USD by two ledgers, so this price
                // is included in the dataset
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}});
                oracle.set(UpdateArg{.series = {{"XRP", "EUR", 740, 1}}});
            }

            // entire and trimmed stats
            auto ret =
                Oracle::aggregatePrice(env, "XRP", "USD", oracles, 20, "200");
            BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74.6");
            BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 7);
            BEAST_EXPECT(
                ret[jss::entire_set][jss::standard_deviation] ==
                "0.2160246899469287");
            BEAST_EXPECT(ret[jss::median] == "74.6");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::mean] == "74.6");
            BEAST_EXPECT(ret[jss::trimmed_set][jss::size].asUInt() == 5);
            BEAST_EXPECT(
                ret[jss::trimmed_set][jss::standard_deviation] ==
                "0.158113883008419");
            BEAST_EXPECT(ret[jss::time] == 946694900);
        }

        // Reduced data set because of the time threshold
        {
            Env env(*this);
            OraclesData oracles;
            prep(env, oracles);
            for (int i = 0; i < oracles.size(); ++i)
            {
                Oracle oracle(
                    env,
                    {.owner = oracles[i].first,
                     .documentID = asUInt(*oracles[i].second)},
                    false);
                // push XRP/USD by two ledgers, so this price
                // is included in the dataset
                oracle.set(UpdateArg{.series = {{"XRP", "USD", 740, 1}}});
            }

            // entire stats only, limit lastUpdateTime to {200, 125}
            auto ret = Oracle::aggregatePrice(
                env, "XRP", "USD", oracles, std::nullopt, 75);
            BEAST_EXPECT(ret[jss::entire_set][jss::mean] == "74");
            BEAST_EXPECT(ret[jss::entire_set][jss::size].asUInt() == 8);
            BEAST_EXPECT(ret[jss::entire_set][jss::standard_deviation] == "0");
            BEAST_EXPECT(ret[jss::median] == "74");
            BEAST_EXPECT(ret[jss::time] == 946695000);
        }
    }

    void
    run() override
    {
        testErrors();
        testRpc();
    }
};

BEAST_DEFINE_TESTSUITE(GetAggregatePrice, app, ripple);

}  // namespace oracle
}  // namespace jtx
}  // namespace test
}  // namespace ripple
