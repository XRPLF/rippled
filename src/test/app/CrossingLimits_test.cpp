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
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/Feature.h>

namespace ripple {
namespace test {

class CrossingLimits_test : public beast::unit_test::suite
{
private:
    void
    n_offers (
        jtx::Env& env,
        std::size_t n,
        jtx::Account const& account,
        STAmount const& in,
        STAmount const& out)
    {
        using namespace jtx;
        auto const ownerCount = env.le(account)->getFieldU32(sfOwnerCount);
        for (std::size_t i = 0; i < n; i++)
        {
            env(offer(account, in, out));
            env.close();
        }
        env.require (owners (account, ownerCount + n));
    }

public:

    void
    testStepLimit(FeatureBitset features)
    {
        testcase ("Step Limit");

        using namespace jtx;
        Env env(*this, features);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        env.fund(XRP(100000000), gw, "alice", "bob", "carol", "dan");
        env.trust(USD(1), "bob");
        env(pay(gw, "bob", USD(1)));
        env.trust(USD(1), "dan");
        env(pay(gw, "dan", USD(1)));
        n_offers (env, 2000, "bob", XRP(1), USD(1));
        n_offers (env, 1, "dan", XRP(1), USD(1));

        // Alice offers to buy 1000 XRP for 1000 USD. She takes Bob's first
        // offer, and removes 999 more as unfunded and hits the step limit.
        env(offer("alice", USD(1000), XRP(1000)));
        env.require (balance("alice", USD(1)));
        env.require (owners("alice", 2));
        env.require (balance("bob", USD(0)));
        env.require (owners("bob", 1001));
        env.require (balance("dan", USD(1)));
        env.require (owners("dan", 2));

        // Carol offers to buy 1000 XRP for 1000 USD. She removes Bob's next
        // 1000 offers as unfunded and hits the step limit.
        env(offer("carol", USD(1000), XRP(1000)));
        env.require (balance("carol", USD(none)));
        env.require (owners("carol", 1));
        env.require (balance("bob", USD(0)));
        env.require (owners("bob", 1));
        env.require (balance("dan", USD(1)));
        env.require (owners("dan", 2));
    }

    void
    testCrossingLimit(FeatureBitset features)
    {
        testcase ("Crossing Limit");

        using namespace jtx;
        Env env(*this, features);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        env.fund(XRP(100000000), gw, "alice", "bob", "carol");
        env.trust(USD(1000), "bob");
        env(pay(gw, "bob", USD(1000)));
        n_offers (env, 1000, "bob", XRP(1), USD(1));

        // Alice offers to buy 1000 XRP for 1000 USD. She takes the first
        // 850 offers, hitting the crossing limit.
        env(offer("alice", USD(1000), XRP(1000)));
        env.require (balance("alice", USD(850)));
        env.require (balance("bob", USD(150)));
        env.require (owners ("bob", 151));

        // Carol offers to buy 1000 XRP for 1000 USD. She takes the remaining
        // 150 offers without hitting a limit.
        env(offer("carol", USD(1000), XRP(1000)));
        env.require (balance("carol", USD(150)));
        env.require (balance("bob", USD(0)));
        env.require (owners ("bob", 1));
    }

    void
    testStepAndCrossingLimit(FeatureBitset features)
    {
        testcase ("Step And Crossing Limit");

        using namespace jtx;
        Env env(*this, features);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        env.fund(XRP(100000000), gw, "alice", "bob", "carol", "dan", "evita");

        env.trust(USD(1000), "alice");
        env(pay(gw, "alice", USD(1000)));
        env.trust(USD(1000), "carol");
        env(pay(gw, "carol", USD(1)));
        env.trust(USD(1000), "evita");
        env(pay(gw, "evita", USD(1000)));

        n_offers (env, 400, "alice", XRP(1), USD(1));
        n_offers (env, 700, "carol", XRP(1), USD(1));
        n_offers (env, 999, "evita", XRP(1), USD(1));

        // Bob offers to buy 1000 XRP for 1000 USD. He takes all 400 USD from
        // Alice's offers, 1 USD from Carol's and then removes 599 of Carol's
        // offers as unfunded, before hitting the step limit.
        env(offer("bob", USD(1000), XRP(1000)));
        env.require (balance("bob", USD(401)));
        env.require (balance("alice", USD(600)));
        env.require (owners("alice", 1));
        env.require (balance("carol", USD(0)));
        env.require (owners("carol", 101));
        env.require (balance("evita", USD(1000)));
        env.require (owners("evita", 1000));

        // Dan offers to buy 900 XRP for 900 USD. He removes all 100 of Carol's
        // offers as unfunded, then takes 850 USD from Evita's, hitting the
        // crossing limit.
        env(offer("dan", USD(900), XRP(900)));
        env.require (balance("dan", USD(850)));
        env.require (balance("alice", USD(600)));
        env.require (owners("alice", 1));
        env.require (balance("carol", USD(0)));
        env.require (owners("carol", 1));
        env.require (balance("evita", USD(150)));
        env.require (owners("evita", 150));
    }

    void testAutoBridgedLimits (FeatureBitset features)
    {
        testcase ("Auto Bridged Limits");

        using namespace jtx;
        Env env(*this, features);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        env.fund(XRP(100000000), gw, "alice", "bob", "carol", "dan", "evita");

        env.trust(USD(2000), "alice");
        env(pay(gw, "alice", USD(2000)));
        env.trust(USD(1000), "carol");
        env(pay(gw, "carol", USD(3)));
        env.trust(USD(1000), "evita");
        env(pay(gw, "evita", USD(1000)));

        n_offers (env,  302, "alice", EUR(2), XRP(1));
        n_offers (env,  300, "alice", XRP(1), USD(4));
        n_offers (env,  497, "carol", XRP(1), USD(3));
        n_offers (env, 1001, "evita", EUR(1), USD(1));

        // Bob offers to buy 2000 USD for 2000 EUR, even though he only has
        // 1000 EUR.
        //  1. He spends 600 EUR taking Alice's auto-bridged offers and
        //     gets 1200 USD for that.
        //  2. He spends another 2 EUR taking one of Alice's EUR->XRP and
        //     one of Carol's XRP-USD offers.  He gets 3 USD for that.
        //  3. The remainder of Carol's offers are now unfunded.  We've
        //     consumed 602 offers so far.  We now chew through 398 more
        //     of Carol's unfunded offers until we hit the 1000 offer limit.
        //     This sets have_bridge to false -- we will handle no more
        //     bridged offers.
        //  4. However, have_direct is still true.  So we go around one more
        //     time and take one of Evita's offers.
        //  5. After taking one of Evita's offers we notice (again) that our
        //     offer count was exceeded.  So we completely stop after taking
        //     one of Evita's offers.
        env.trust(EUR(10000), "bob");
        env.close();
        env(pay(gw, "bob", EUR(1000)));
        env.close();
        env(offer("bob", USD(2000), EUR(2000)));
        env.require (balance("bob", USD(1204)));
        env.require (balance("bob", EUR( 397)));

        env.require (balance("alice", USD(800)));
        env.require (balance("alice", EUR(602)));
        env.require (offers("alice", 1));
        env.require (owners("alice", 3));

        env.require (balance("carol", USD(0)));
        env.require (balance("carol", EUR(none)));
        env.require (offers("carol", 100));
        env.require (owners("carol", 101));

        env.require (balance("evita", USD(999)));
        env.require (balance("evita", EUR(1)));
        env.require (offers("evita", 1000));
        env.require (owners("evita", 1002));

        // Dan offers to buy 900 EUR for 900 USD.
        //  1. He removes all 100 of Carol's remaining unfunded offers.
        //  2. Then takes 850 USD from Evita's offers.
        //  3. Consuming 850 of Evita's funded offers hits the crossing
        //     limit.  So Dan's offer crossing stops even though he would
        //     be willing to take another 50 of Evita's offers.
        env.trust(EUR(10000), "dan");
        env.close();
        env(pay(gw, "dan", EUR(1000)));
        env.close();

        env(offer("dan", USD(900), EUR(900)));
        env.require (balance("dan", USD(850)));
        env.require (balance("dan", EUR(150)));

        env.require (balance("alice", USD(800)));
        env.require (balance("alice", EUR(602)));
        env.require (offers("alice", 1));
        env.require (owners("alice", 3));

        env.require (balance("carol", USD(0)));
        env.require (balance("carol", EUR(none)));
        env.require (offers("carol", 0));
        env.require (owners("carol", 1));

        env.require (balance("evita", USD(149)));
        env.require (balance("evita", EUR(851)));
        env.require (offers("evita", 150));
        env.require (owners("evita", 152));
    }

    void
    run() override
    {
        auto testAll = [this](FeatureBitset features) {
            testStepLimit(features);
            testCrossingLimit(features);
            testStepAndCrossingLimit(features);
            testAutoBridgedLimits(features);
        };
        using namespace jtx;
        auto const sa = supported_amendments();
        testAll(sa - featureFlow - fix1373 - featureFlowCross);
        testAll(sa               - fix1373 - featureFlowCross);
        testAll(sa                         - featureFlowCross);
//      testAll(sa);// Does not pass with FlowCross enabled.
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(CrossingLimits,tx,ripple);

} // test
} // ripple
