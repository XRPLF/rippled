//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

//#include <ripple/app/misc/AMM.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
//#include <test/jtx/WSClient.h>

namespace ripple {
namespace test {

class AMMInfo_test : public beast::unit_test::suite
{
    jtx::Account const gw;
    jtx::Account const alice;
    jtx::Account const carol;
    jtx::IOU const USD;

    template <typename F>
    void
    proc(
        F&& cb,
        std::optional<std::pair<std::uint32_t, std::uint32_t>> const& pool = {},
        std::optional<IOUAmount> const& lpt = {})
    {
        using namespace jtx;
        Env env{*this};

        env.fund(jtx::XRP(30000), alice, carol, gw);
        env.trust(USD(30000), alice);
        env.trust(USD(30000), carol);

        env(pay(gw, alice, USD(30000)));
        env(pay(gw, carol, USD(30000)));

        auto [asset1, asset2] =
            [&]() -> std::pair<std::uint32_t, std::uint32_t> {
            if (pool)
                return *pool;
            return {10000, 10000};
        }();
        auto tokens = [&]() {
            if (lpt)
                return *lpt;
            return IOUAmount{10000000, 0};
        }();
        AMM ammAlice(env, alice, XRP(asset1), USD(asset2));
        BEAST_EXPECT(ammAlice.expectBalances(XRP(asset1), USD(asset2), tokens));
        cb(ammAlice, env);
    }

public:
    AMMInfo_test() : gw("gw"), alice("alice"), carol("carol"), USD(gw["USD"])
    {
    }
    void
    testErrors()
    {
        testcase("Errors");

        using namespace jtx;
        // Invalid AMM hash
        proc([&](AMM& ammAlice, Env&) {
            uint256 hash{1};
            auto const jv = ammAlice.ammRpcInfo({}, {}, hash);
            BEAST_EXPECT(
                jv.has_value() &&
                (*jv)[jss::error_message] == "Account not found.");
        });

        // Invalid LP account id
        proc([&](AMM& ammAlice, Env&) {
            Account bogie("bogie");
            auto const jv = ammAlice.ammRpcInfo(bogie.id());
            BEAST_EXPECT(
                jv.has_value() &&
                (*jv)[jss::error_message] == "Account malformed.");
        });
    }

    void
    testSimpleRpc()
    {
        testcase("RPC simple");

        using namespace jtx;
        proc([&](AMM& ammAlice, Env&) {
            BEAST_EXPECT(ammAlice.expectAmmRpcInfo(
                XRP(10000), USD(10000), IOUAmount{10000000, 0}));
        });
        proc([&](AMM& ammAlice, Env&) {
            auto const ammID = [&]() -> std::optional<uint256> {
                if (auto const jv = ammAlice.ammRpcInfo({}, {}, {}, true);
                    jv.has_value())
                {
                    uint256 ammID;
                    if (ammID.parseHex((*jv)[jss::AMMID].asString()))
                        return ammID;
                }
                return {};
            }();
            BEAST_EXPECT(ammID.has_value() && ammAlice.ammID() == *ammID);
        });
    }

    void
    run() override
    {
        testErrors();
        testSimpleRpc();
    }
};

BEAST_DEFINE_TESTSUITE(AMMInfo, app, ripple);

}  // namespace test
}  // namespace ripple
