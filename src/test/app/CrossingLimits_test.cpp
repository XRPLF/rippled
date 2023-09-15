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

#include <ripple/beast/unit_test.h>
#include <ripple/protocol/Feature.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class CrossingLimits_test : public beast::unit_test::suite
{
public:
    void
    testStepLimit(FeatureBitset features)
    {
        testcase("Step Limit");

        using namespace jtx;
        Env env(*this, features);

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        env.fund(XRP(100000000), gw, "alice", "bob", "carol", "dan");
        env.trust(USD(1), "bob");
        env(pay(gw, "bob", USD(1)));
        env.trust(USD(1), "dan");
        env(pay(gw, "dan", USD(1)));
        n_offers(env, 2000, "bob", XRP(1), USD(1));
        n_offers(env, 1, "dan", XRP(1), USD(1));

        // Alice offers to buy 1000 XRP for 1000 USD. She takes Bob's first
        // offer, removes 999 more as unfunded, then hits the step limit.
        env(offer("alice", USD(1000), XRP(1000)));
        env.require(balance("alice", USD(1)));
        env.require(owners("alice", 2));
        env.require(balance("bob", USD(0)));
        env.require(owners("bob", 1001));
        env.require(balance("dan", USD(1)));
        env.require(owners("dan", 2));

        // Carol offers to buy 1000 XRP for 1000 USD. She removes Bob's next
        // 1000 offers as unfunded and hits the step limit.
        env(offer("carol", USD(1000), XRP(1000)));
        env.require(balance("carol", USD(none)));
        env.require(owners("carol", 1));
        env.require(balance("bob", USD(0)));
        env.require(owners("bob", 1));
        env.require(balance("dan", USD(1)));
        env.require(owners("dan", 2));
    }

    void
    testCrossingLimit(FeatureBitset features)
    {
        testcase("Crossing Limit");

        using namespace jtx;
        Env env(*this, features);

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        // The number of allowed offers to cross is different between
        // Taker and FlowCross.  Taker allows 850 and FlowCross allows 1000.
        // Accommodate that difference in the test.
        int const maxConsumed = features[featureFlowCross] ? 1000 : 850;

        env.fund(XRP(100000000), gw, "alice", "bob", "carol");
        int const bobsOfferCount = maxConsumed + 150;
        env.trust(USD(bobsOfferCount), "bob");
        env(pay(gw, "bob", USD(bobsOfferCount)));
        env.close();
        n_offers(env, bobsOfferCount, "bob", XRP(1), USD(1));

        // Alice offers to buy Bob's offers. However she hits the offer
        // crossing limit, so she can't buy them all at once.
        env(offer("alice", USD(bobsOfferCount), XRP(bobsOfferCount)));
        env.close();
        env.require(balance("alice", USD(maxConsumed)));
        env.require(balance("bob", USD(150)));
        env.require(owners("bob", 150 + 1));

        // Carol offers to buy 1000 XRP for 1000 USD. She takes Bob's
        // remaining 150 offers without hitting a limit.
        env(offer("carol", USD(1000), XRP(1000)));
        env.close();
        env.require(balance("carol", USD(150)));
        env.require(balance("bob", USD(0)));
        env.require(owners("bob", 1));
    }

    void
    testStepAndCrossingLimit(FeatureBitset features)
    {
        testcase("Step And Crossing Limit");

        using namespace jtx;
        Env env(*this, features);

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        env.fund(XRP(100000000), gw, "alice", "bob", "carol", "dan", "evita");

        // The number of offers allowed to cross is different between
        // Taker and FlowCross.  Taker allows 850 and FlowCross allows 1000.
        // Accommodate that difference in the test.
        bool const isFlowCross{features[featureFlowCross]};
        int const maxConsumed = isFlowCross ? 1000 : 850;

        int const evitasOfferCount{maxConsumed + 49};
        env.trust(USD(1000), "alice");
        env(pay(gw, "alice", USD(1000)));
        env.trust(USD(1000), "carol");
        env(pay(gw, "carol", USD(1)));
        env.trust(USD(evitasOfferCount + 1), "evita");
        env(pay(gw, "evita", USD(evitasOfferCount + 1)));

        // Taker and FlowCross have another difference we must accommodate.
        // Taker allows a total of 1000 unfunded offers to be consumed
        // beyond the 850 offers it can take.  FlowCross draws no such
        // distinction; its limit is 1000 funded or unfunded.
        //
        // Give carol an extra 150 (unfunded) offers when we're using Taker
        // to accommodate that difference.
        int const carolsOfferCount{isFlowCross ? 700 : 850};
        n_offers(env, 400, "alice", XRP(1), USD(1));
        n_offers(env, carolsOfferCount, "carol", XRP(1), USD(1));
        n_offers(env, evitasOfferCount, "evita", XRP(1), USD(1));

        // Bob offers to buy 1000 XRP for 1000 USD. He takes all 400 USD from
        // Alice's offers, 1 USD from Carol's and then removes 599 of Carol's
        // offers as unfunded, before hitting the step limit.
        env(offer("bob", USD(1000), XRP(1000)));
        env.require(balance("bob", USD(401)));
        env.require(balance("alice", USD(600)));
        env.require(owners("alice", 1));
        env.require(balance("carol", USD(0)));
        env.require(owners("carol", carolsOfferCount - 599));
        env.require(balance("evita", USD(evitasOfferCount + 1)));
        env.require(owners("evita", evitasOfferCount + 1));

        // Dan offers to buy maxConsumed + 50 XRP USD. He removes all of
        // Carol's remaining offers as unfunded, then takes
        // (maxConsumed - 100) USD from Evita's, hitting the crossing limit.
        env(offer("dan", USD(maxConsumed + 50), XRP(maxConsumed + 50)));
        env.require(balance("dan", USD(maxConsumed - 100)));
        env.require(owners("dan", 2));
        env.require(balance("alice", USD(600)));
        env.require(owners("alice", 1));
        env.require(balance("carol", USD(0)));
        env.require(owners("carol", 1));
        env.require(balance("evita", USD(150)));
        env.require(owners("evita", 150));
    }

    void
    testAutoBridgedLimitsTaker(FeatureBitset features)
    {
        testcase("Auto Bridged Limits Taker");

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

        n_offers(env, 302, "alice", EUR(2), XRP(1));
        n_offers(env, 300, "alice", XRP(1), USD(4));
        n_offers(env, 497, "carol", XRP(1), USD(3));
        n_offers(env, 1001, "evita", EUR(1), USD(1));

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
        env.require(balance("bob", USD(1204)));
        env.require(balance("bob", EUR(397)));

        env.require(balance("alice", USD(800)));
        env.require(balance("alice", EUR(602)));
        env.require(offers("alice", 1));
        env.require(owners("alice", 3));

        env.require(balance("carol", USD(0)));
        env.require(balance("carol", EUR(none)));
        env.require(offers("carol", 100));
        env.require(owners("carol", 101));

        env.require(balance("evita", USD(999)));
        env.require(balance("evita", EUR(1)));
        env.require(offers("evita", 1000));
        env.require(owners("evita", 1002));

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
        env.require(balance("dan", USD(850)));
        env.require(balance("dan", EUR(150)));

        env.require(balance("alice", USD(800)));
        env.require(balance("alice", EUR(602)));
        env.require(offers("alice", 1));
        env.require(owners("alice", 3));

        env.require(balance("carol", USD(0)));
        env.require(balance("carol", EUR(none)));
        env.require(offers("carol", 0));
        env.require(owners("carol", 1));

        env.require(balance("evita", USD(149)));
        env.require(balance("evita", EUR(851)));
        env.require(offers("evita", 150));
        env.require(owners("evita", 152));
    }

    void
    testAutoBridgedLimitsFlowCross(FeatureBitset features)
    {
        testcase("Auto Bridged Limits FlowCross");

        // If any book step in a payment strand consumes 1000 offers, the
        // liquidity from the offers is used, but that strand will be marked as
        // dry for the remainder of the transaction.

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        // There are two almost identical tests. There is a strand with a large
        // number of unfunded offers that will cause the strand to be marked dry
        // even though there will still be liquidity available on that strand.
        // In the first test, the strand has the best initial quality. In the
        // second test the strand does not have the best quality (the
        // implementation has to handle this case correct and not mark the
        // strand dry until the liquidity is actually used)

        // The implementation allows any single step to consume at most 1000
        // offers. With the `FlowSortStrands` feature enabled, if the total
        // number of offers consumed by all the steps combined exceeds 1500, the
        // payment stops.
        {
            Env env(*this, features);

            env.fund(XRP(100000000), gw, alice, bob, carol);

            env.trust(USD(4000), alice);
            env(pay(gw, alice, USD(4000)));
            env.trust(USD(1000), carol);
            env(pay(gw, carol, USD(3)));

            // Notice the strand with the 800 unfunded offers has the initial
            // best quality
            n_offers(env, 2000, alice, EUR(2), XRP(1));
            n_offers(env, 100, alice, XRP(1), USD(4));
            n_offers(
                env, 801, carol, XRP(1), USD(3));  // only one offer is funded
            n_offers(env, 1000, alice, XRP(1), USD(3));

            n_offers(env, 1, alice, EUR(500), USD(500));

            // Bob offers to buy 2000 USD for 2000 EUR; He starts with 2000 EUR
            //  1. The best quality is the autobridged offers that take 2 EUR
            //  and give 4 USD.
            //     Bob spends 200 EUR and receives 400 USD.
            //     100 EUR->XRP offers consumed.
            //     100 XRP->USD offers consumed.
            //     200 total offers consumed.
            //
            //  2. The best quality is the autobridged offers that take 2 EUR
            //  and give 3 USD.
            //     a. One of Carol's offers is taken. This leaves her other
            //     offers unfunded.
            //     b. Carol's remaining 800 offers are consumed as unfunded.
            //     c. 199 of alice's XRP(1) to USD(3) offers are consumed.
            //        A book step is allowed to consume a maxium of 1000 offers
            //        at a given quality, and that limit is now reached.
            //     d. Now the strand is dry, even though there are still funded
            //     XRP(1) to USD(3) offers available.
            //        Bob has spent 400 EUR and received 600 USD in this step.
            //        200 EUR->XRP offers consumed
            //        800 unfunded XRP->USD offers consumed
            //        200 funded XRP->USD offers consumed (1 carol, 199 alice)
            //        1400 total offers consumed so far (100 left before the
            //        limit)
            //  3. The best is the non-autobridged offers that takes 500 EUR and
            //  gives 500 USD.
            //     Bob started with 2000 EUR
            //     Bob spent 500 EUR (100+400)
            //     Bob has 1500 EUR left
            //     In this step:
            //     Bob spents 500 EUR and receives 500 USD.
            // In total:
            //           Bob spent 1100 EUR (200 + 400 + 500)
            //           Bob has 900 EUR remaining (2000 - 1100)
            //           Bob received 1500 USD (400 + 600 + 500)
            //           Alice spent 1497 USD (100*4 + 199*3 + 500)
            //           Alice has 2503 remaining (4000 - 1497)
            //           Alice received 1100 EUR (200 + 400 + 500)
            env.trust(EUR(10000), bob);
            env.close();
            env(pay(gw, bob, EUR(2000)));
            env.close();
            env(offer(bob, USD(4000), EUR(4000)));
            env.close();

            env.require(balance(bob, USD(1500)));
            env.require(balance(bob, EUR(900)));
            env.require(offers(bob, 1));
            env.require(owners(bob, 3));

            env.require(balance(alice, USD(2503)));
            env.require(balance(alice, EUR(1100)));
            auto const numAOffers =
                2000 + 100 + 1000 + 1 - (2 * 100 + 2 * 199 + 1 + 1);
            env.require(offers(alice, numAOffers));
            env.require(owners(alice, numAOffers + 2));

            env.require(offers(carol, 0));
        }
        {
            Env env(*this, features);

            env.fund(XRP(100000000), gw, alice, bob, carol);

            env.trust(USD(4000), alice);
            env(pay(gw, alice, USD(4000)));
            env.trust(USD(1000), carol);
            env(pay(gw, carol, USD(3)));

            // Notice the strand with the 800 unfunded offers does not have the
            // initial best quality
            n_offers(env, 1, alice, EUR(1), USD(10));
            n_offers(env, 2000, alice, EUR(2), XRP(1));
            n_offers(env, 100, alice, XRP(1), USD(4));
            n_offers(
                env, 801, carol, XRP(1), USD(3));  // only one offer is funded
            n_offers(env, 1000, alice, XRP(1), USD(3));

            n_offers(env, 1, alice, EUR(499), USD(499));

            // Bob offers to buy 2000 USD for 2000 EUR; He starts with 2000 EUR
            //  1. The best quality is the offer that takes 1 EUR and gives 10
            //  USD
            //     Bob spends 1 EUR and receives 10 USD.
            //
            //  2. The best quality is the autobridged offers that takes 2 EUR
            //  and gives 4 USD.
            //     Bob spends 200 EUR and receives 400 USD.
            //
            //  3. The best quality is the autobridged offers that takes 2 EUR
            //  and gives 3 USD.
            //     a. One of Carol's offers is taken. This leaves her other
            //     offers unfunded.
            //     b. Carol's remaining 800 offers are consumed as unfunded.
            //     c. 199 of alice's XRP(1) to USD(3) offers are consumed.
            //        A book step is allowed to consume a maxium of 1000 offers
            //        at a given quality, and that limit is now reached.
            //     d. Now the strand is dry, even though there are still funded
            //     XRP(1) to USD(3) offers available. Bob has spent 400 EUR and
            //     received 600 USD in this step. (200 funded offers consumed
            //     800 unfunded offers)
            //  4. The best is the non-autobridged offers that takes 499 EUR and
            //  gives 499 USD.
            //     Bob has 2000 EUR, and has spent 1+200+400=601 EUR. He has
            //     1399 left. Bob spent 499 EUR and receives 499 USD.
            // In total: Bob spent EUR(1 + 200 + 400 + 499) = EUR(1100). He
            // started with 2000 so has 900 remaining
            //           Bob received USD(10 + 400 + 600 + 499) = USD(1509).
            //           Alice spent 10 + 100*4 + 199*3 + 499 = 1506 USD. She
            //           started with 4000 so has 2494 USD remaining. Alice
            //           received 200 + 400 + 500 = 1100 EUR
            env.trust(EUR(10000), bob);
            env.close();
            env(pay(gw, bob, EUR(2000)));
            env.close();
            env(offer(bob, USD(4000), EUR(4000)));
            env.close();

            env.require(balance(bob, USD(1509)));
            env.require(balance(bob, EUR(900)));
            env.require(offers(bob, 1));
            env.require(owners(bob, 3));

            env.require(balance(alice, USD(2494)));
            env.require(balance(alice, EUR(1100)));
            auto const numAOffers =
                1 + 2000 + 100 + 1000 + 1 - (1 + 2 * 100 + 2 * 199 + 1 + 1);
            env.require(offers(alice, numAOffers));
            env.require(owners(alice, numAOffers + 2));

            env.require(offers(carol, 0));
        }
    }

    void
    testAutoBridgedLimits(FeatureBitset features)
    {
        // Taker and FlowCross are too different in the way they handle
        // autobridging to make one test suit both approaches.
        //
        //  o Taker alternates between books, completing one full increment
        //    before returning to make another pass.
        //
        //  o FlowCross extracts as much as possible in one book at one Quality
        //    before proceeding to the other book.  This reduces the number of
        //    times we change books.
        //
        // So the tests for the two forms of autobridging are separate.
        if (features[featureFlowCross])
            testAutoBridgedLimitsFlowCross(features);
        else
            testAutoBridgedLimitsTaker(features);
    }

    void
    testOfferOverflow(FeatureBitset features)
    {
        testcase("Offer Overflow");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        auto const USD = gw["USD"];

        Env env(*this, features);

        env.fund(XRP(100000000), gw, alice, bob);

        env.trust(USD(8000), alice);
        env.trust(USD(8000), bob);
        env.close();

        env(pay(gw, alice, USD(8000)));
        env.close();

        // The new flow cross handles consuming excessive offers differently
        // than the old offer crossing code. In the old code, the total number
        // of consumed offers is tracked, and the crossings will stop after this
        // limit is hit. In the new code, the number of offers is tracked per
        // offerbook and per quality. This test shows how they can differ. Set
        // up a book with many offers. At each quality keep the number of offers
        // below the limit. However, if all the offers are consumed it would
        // create a tecOVERSIZE error.

        // The featureFlowSortStrands introduces a way of tracking the total
        // number of consumed offers; with this feature the transaction no
        // longer fails with a tecOVERSIZE error.
        // The implementation allows any single step to consume at most 1000
        // offers. With the `FlowSortStrands` feature enabled, if the total
        // number of offers consumed by all the steps combined exceeds 1500, the
        // payment stops. Since the first set of offers consumes 998 offers, the
        // second set will consume 998, which is not over the limit and the
        // payment stops. So 2*998, or 1996 is the expected value when
        // `FlowSortStrands` is enabled.
        n_offers(env, 998, alice, XRP(1.00), USD(1));
        n_offers(env, 998, alice, XRP(0.99), USD(1));
        n_offers(env, 998, alice, XRP(0.98), USD(1));
        n_offers(env, 998, alice, XRP(0.97), USD(1));
        n_offers(env, 998, alice, XRP(0.96), USD(1));
        n_offers(env, 998, alice, XRP(0.95), USD(1));

        bool const withFlowCross = features[featureFlowCross];
        bool const withSortStrands = features[featureFlowSortStrands];

        auto const expectedTER = [&]() -> TER {
            if (withFlowCross && !withSortStrands)
                return TER{tecOVERSIZE};
            return tesSUCCESS;
        }();

        env(offer(bob, USD(8000), XRP(8000)), ter(expectedTER));
        env.close();

        auto const expectedUSD = [&] {
            if (!withFlowCross)
                return USD(850);
            if (!withSortStrands)
                return USD(0);
            return USD(1996);
        }();

        env.require(balance(bob, expectedUSD));
    }

    void
    run() override
    {
        auto testAll = [this](FeatureBitset features) {
            testStepLimit(features);
            testCrossingLimit(features);
            testStepAndCrossingLimit(features);
            testAutoBridgedLimits(features);
            testOfferOverflow(features);
        };
        using namespace jtx;
        auto const sa = supported_amendments();
        testAll(sa);
        testAll(sa - featureFlowSortStrands);
        testAll(sa - featureFlowCross - featureFlowSortStrands);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(CrossingLimits, tx, ripple, 10);

}  // namespace test
}  // namespace ripple
