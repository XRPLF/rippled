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
#include <xrpld/ledger/ApplyViewImpl.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/resource/Fees.h>

#include "xrpl/protocol/TER.h"
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

        fund(env, gw, {carol}, {USD(4'000), BTC(1)}, Fund::Acct);
        ammAlice.deposit(carol, 10);
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(22'000), BTC(0.55), IOUAmount{110, 0}));

        fund(env, gw, {bob}, {USD(4'000), BTC(1)}, Fund::Acct);
        ammAlice.deposit(bob, 10);
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(24'000), BTC(0.60), IOUAmount{120, 0}));

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
        // Note: Deep freeze is not considered for LPToken transfer
        env(pay(bob, carol, STAmount{lpIssue, 5}));
        env.close();

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

    void
    testOrderBookFrozenAsset(FeatureBitset features)
    {
        testcase("Rippling Order Book");

        using namespace jtx;
        Env env{*this, features};

        fund(
            env,
            gw,
            {alice, bob, carol},
            {USD(10'000), EUR(10'000)},
            Fund::All);
        AMM ammAlice(env, alice, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol, 1'000);
        ammAlice.deposit(bob, 1'000);

        auto const lpIssue = ammAlice.lptIssue();
        env(offer(carol, XRP(10), STAmount{lpIssue, 10}), txflags(tfPassive));
        env.close();
        BEAST_EXPECT(expectOffers(env, carol, 1));

        env.trust(STAmount{lpIssue, 1'000'000'000}, alice);
        env.trust(STAmount{lpIssue, 1'000'000'000}, bob);
        env.trust(STAmount{lpIssue, 1'000'000'000}, carol);
        env.close();

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // bob can still send to lptoken to carol even tho carol's USD is
        // frozen, regardless of whether fixLPTokenTransfer is enabled or not
        // Note: Deep freeze is not considered for LPToken transfer
        env(pay(bob, carol, STAmount{lpIssue, 1}));
        env.close();

        if (features[fixLPTokenTransfer])
        {
            env(pay(alice, bob, STAmount{lpIssue, 10}),
                txflags(tfPartialPayment),
                sendmax(XRP(10)),
                ter(tecPATH_DRY));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 1));

            // gateway freezes carol's USD
            env(trust(gw, carol["USD"](1'000'000'000), tfClearFreeze));
            env.close();

            env(pay(alice, bob, STAmount{lpIssue, 10}),
                txflags(tfPartialPayment),
                sendmax(XRP(10)));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }
        else
        {
            // carol can still send lptoken with frozen USD
            env(pay(alice, bob, STAmount{lpIssue, 10}),
                txflags(tfPartialPayment),
                sendmax(XRP(10)));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        env.close();
    }

    // // Offer crossing with two AMM LPTokens.
    // testAMM([&](AMM& ammAlice, Env& env) {
    //     ammAlice.deposit(carol, 1'000'000);
    //     fund(env, gw, {alice, carol}, {EUR(10'000)}, Fund::IOUOnly);
    //     AMM ammAlice1(env, alice, XRP(10'000), EUR(10'000));
    //     ammAlice1.deposit(carol, 1'000'000);
    //     auto const token1 = ammAlice.lptIssue();
    //     auto const token2 = ammAlice1.lptIssue();
    //     env(offer(alice, STAmount{token1, 100}, STAmount{token2, 100}),
    //         txflags(tfPassive));
    //     env.close();
    //     BEAST_EXPECT(expectOffers(env, alice, 1));
    //     env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
    //     env.close();
    //     BEAST_EXPECT(
    //         expectLine(env, alice, STAmount{token1, 10'000'100}) &&
    //         expectLine(env, alice, STAmount{token2, 9'999'900}));
    //     BEAST_EXPECT(
    //         expectLine(env, carol, STAmount{token2, 1'000'100}) &&
    //         expectLine(env, carol, STAmount{token1, 999'900}));
    //     BEAST_EXPECT(
    //         expectOffers(env, alice, 0) && expectOffers(env, carol, 0));
    // });

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};

        for (auto const features : {all, all - fixLPTokenTransfer})
        {
            //  testRipplingFrozenAsset(features);
            testOrderBookFrozenAsset(features);
        }
    }
};

BEAST_DEFINE_TESTSUITE(LPTokenTransfer, app, ripple);
}  // namespace test
}  // namespace ripple
