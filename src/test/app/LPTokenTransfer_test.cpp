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

#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/amount.h>
#include <test/jtx/sendmax.h>
#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/paths/AMMOffer.h>
#include <xrpld/app/tx/detail/AMMBid.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

#include <chrono>
#include <utility>
#include <vector>

#include <boost/regex.hpp>
namespace ripple {
namespace test {

class LPTokenTransfer_test : public jtx::AMMTest
{
    void
    testRipplingFrozenAsset(FeatureBitset features)
    {
        testcase("Rippling Frozen Asset");

        using namespace jtx;
        Env env{*this, features};
        fund(env, gw, {alice}, {USD(20'000), BTC(0.5)}, Fund::All);
        env(rate(gw, 1.25));
        env.close();
        AMM ammAlice(env, alice, USD(20'000), BTC(0.5));
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(20'000), BTC(0.5), IOUAmount{100, 0}));
        BEAST_EXPECT(expectLine(env, alice, USD(0)));
        BEAST_EXPECT(expectLine(env, alice, BTC(0)));
        fund(env, gw, {carol}, {USD(2'000), BTC(0.05)}, Fund::Acct);
        ammAlice.deposit(carol, 10);
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(22'000), BTC(0.55), IOUAmount{110, 0}));
        BEAST_EXPECT(expectLine(env, carol, USD(0)));
        BEAST_EXPECT(expectLine(env, carol, BTC(0)));

        fund(env, gw, {bob}, {USD(2'000), BTC(0.05)}, Fund::Acct);
        ammAlice.deposit(bob, 10);
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(24'000), BTC(0.60), IOUAmount{120, 0}));
        BEAST_EXPECT(expectLine(env, bob, USD(0)));
        BEAST_EXPECT(expectLine(env, bob, BTC(0)));
        auto const lpIssue = ammAlice.lptIssue();
        env.trust(STAmount{lpIssue, 500}, alice);
        env.trust(STAmount{lpIssue, 500}, bob);
        env.trust(STAmount{lpIssue, 500}, carol);
        env.close();

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // bob can still send to lptoken to carol even tho carol's USD is
        // frozen, regardless of whether fixLPTokenTransfer is enabled or not
        env(pay(bob, carol, STAmount{lpIssue, 5}));

        if (features[fixLPTokenTransfer])
        {
            // carol is frozen on USD and therefore can't send lptoken to bob
            env(pay(carol, bob, STAmount{lpIssue, 5}), ter(tecPATH_DRY));
        }
        else
        {
            // carol can still send lptoken with frozen USD
            env(pay(carol, bob, STAmount{lpIssue, 5}));
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        for (auto const feature : {all, all - fixLPTokenTransfer})
            testRipplingFrozenAsset(feature);
    }
};

BEAST_DEFINE_TESTSUITE(LPTokenTransfer, app, ripple);
}  // namespace test
}  // namespace ripple
