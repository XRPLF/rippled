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
    testRipplingUnathorizedAsset(FeatureBitset features)
    {
        testcase("Rippling Unauthorized Asset");

        using namespace jtx;

        // disable AMMClawback to allow single side deposit without owning one
        // of the assets
        Env env(*this, features - featureAMMClawback);
        env.fund(XRP(1000), gw, alice, carol, bob);
        env(fset(gw, asfRequireAuth));
        env(rate(gw, 1.25));
        env.close();

        // gateway authorizes alice
        auto authAndFund = [&](Account const& account,
                               std::string const currency) {
            env(trust(gw, account[currency](100'000)), txflags(tfSetfAuth));
            env(trust(account, gw[currency](100'000)));
            env.close();
            env(pay(gw, account, gw[currency](30'000)));
            env.close();
        };

        // carol has BTC line but not USD line
        // bob has USD line but not BTC line
        // alice has both USD and BTC line
        authAndFund(alice, "BTC");
        authAndFund(alice, "USD");
        authAndFund(bob, "BTC");
        authAndFund(carol, "USD");

        AMM ammAlice(env, alice, USD(20'000), BTC(0.5));

        // bob single side deposits with BTC
        ammAlice.deposit(bob, BTC(10));

        // carol single side deposits with USD
        ammAlice.deposit(carol, USD(2000));

        // increase limit for lptoken lines so that they can transfer lptokens
        // to each other
        auto const lpIssue = ammAlice.lptIssue();
        env.trust(STAmount{lpIssue, 500}, alice);
        env.trust(STAmount{lpIssue, 500}, bob);
        env.trust(STAmount{lpIssue, 500}, carol);
        env.close();

        env.enableFeature(featureAMMClawback);
        env.close();

        // transfer LPToken between alice, bob and carol and validate expected
        // result
        auto executeLPTokenPayments = [&](TER const code) {
            env(pay(carol, alice, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(alice, carol, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(bob, alice, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(alice, bob, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(bob, carol, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(carol, bob, STAmount{lpIssue, 1}), ter(code));
            env.close();
        };

        // We are going to test the behavior when bob and carol tries to send
        // lptoken pre/post amendment.
        //
        // With fixLPTokenTransfer amendment, carol and bob cannot receive nor
        // send lptoken if they doesn't have one of the trustlines
        if (features[fixLPTokenTransfer])
        {
            executeLPTokenPayments(tecPATH_DRY);
        }
        // without fixLPTokenTransfer, carol and bob can still receive and send
        // lptoken freely even tho they don't have trustlines for one of the
        // assets
        else
        {
            executeLPTokenPayments(tesSUCCESS);
        }

        // bob and carol create trustlines for the assets that they are missing,
        // however, they are still unauthorized!
        env(trust(bob, gw["USD"](100'000)));
        env(trust(carol, gw["BTC"](100'000)));
        env.close();

        // With fixLPTokenTransfer amendment, carol and bob cannot receive nor
        // send lptoken if they have unauthorized trustlines
        if (features[fixLPTokenTransfer])
        {
            executeLPTokenPayments(tecPATH_DRY);
        }
        // without fixLPTokenTransfer, carol and bob can still receive and send
        // lptoken freely even tho they don't have authorized trustlines
        else
        {
            executeLPTokenPayments(tesSUCCESS);
        }

        // gateway authorizes bob and carol for their respective trustlines
        env(trust(gw, bob["USD"](100'000)), txflags(tfSetfAuth));
        env.close();
        env(trust(gw, carol["BTC"](100'000)), txflags(tfSetfAuth));
        env.close();

        // bob and carol can now transfer lptoken freely since they have
        // authorized lines for both assets
        executeLPTokenPayments(tesSUCCESS);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};

        for (auto const features : {all, all - fixLPTokenTransfer})
        {
            testRipplingFrozenAsset(features);
            testRipplingUnathorizedAsset(features);
        }
    }
};

BEAST_DEFINE_TESTSUITE(LPTokenTransfer, app, ripple);
}  // namespace test
}  // namespace ripple
