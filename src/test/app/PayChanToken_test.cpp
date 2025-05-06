//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/ledger/Dir.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
struct PayChanToken_test : public beast::unit_test::suite
{
    void
    testIOUEnablement(FeatureBitset features)
    {
        testcase("IOU Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenPaychan : {false, true})
        {
            auto const amend =
                withTokenPaychan ? features : features - featureTokenPaychan;
            Env env{*this, amend};
            // auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const createResult =
                withTokenPaychan ? ter(tesSUCCESS) : ter(temDISABLED);
            // auto const fundClaimResult =
            //     withTokenPaychan ? ter(tesSUCCESS) : ter(tecNO_TARGET);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            // auto const chan = channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(1000), settleDelay, pk),
                createResult);
            env.close();
            // env(escrow::finish(bob, alice, seq1),
            //     escrow::condition(escrow::cb1),
            //     escrow::fulfillment(escrow::fb1),
            //     fee(baseFee * 150),
            //     finishResult);
            // env.close();

            // auto const seq2 = env.seq(alice);
            // env(escrow::create(alice, bob, USD(1'000)),
            //     escrow::condition(escrow::cb2),
            //     escrow::finish_time(env.now() + 1s),
            //     escrow::cancel_time(env.now() + 2s),
            //     fee(baseFee * 150),
            //     createResult);
            // env.close();
            // env(escrow::cancel(bob, alice, seq2), finishResult);
            // env.close();
        }
    }

    void
    testTest(FeatureBitset features)
    {
        testcase("Test");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        // auto const baseFee = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTokenLocking));
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, USD(1000), settleDelay, pk),
            ter(tesSUCCESS));
        env.close();
        env(paychan::fund(alice, chan, USD(1000)), ter(tesSUCCESS));
        env.close();

        // auto const seq2 = env.seq(alice);
        // env(escrow::create(alice, bob, USD(1'000)),
        //     escrow::condition(escrow::cb2),
        //     escrow::finish_time(env.now() + 1s),
        //     escrow::cancel_time(env.now() + 2s),
        //     fee(baseFee * 150),
        //     createResult);
        // env.close();
        // env(escrow::cancel(bob, alice, seq2), finishResult);
        // env.close();
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testIOUEnablement(features);
        testTest(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(PayChanToken, app, ripple);
}  // namespace test
}  // namespace ripple
