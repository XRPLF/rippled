
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TER.h>

namespace xrpl::test {

class CrossingLimitsMPT_test : public beast::unit_test::Suite
{
public:
    void
    testStepLimit(FeatureBitset features)
    {
        testcase("Step Limit");

        using namespace jtx;
        Env env(*this, features);

        auto const gw = Account("gateway");

        env.fund(XRP(100'000'000), gw, "alice", "bob", "carol", "dan");
        MPT const usd =
            MPTTester({.env = env, .issuer = gw, .holders = {"bob", "dan"}, .maxAmt = 2});
        env(pay(gw, "bob", usd(1)));
        env(pay(gw, "dan", usd(1)));
        nOffers(env, 2'000, "bob", XRP(1), usd(1));
        nOffers(env, 1, "dan", XRP(1), usd(1));

        // Alice offers to buy 1000 XRP for 1000 USD. She takes Bob's first
        // offer, removes 999 more as unfunded, then hits the step limit.
        env(offer("alice", usd(1'000), XRP(1'000)));
        env.require(Balance("alice", usd(1)));
        env.require(Owners("alice", 2));
        env.require(Balance("bob", usd(0)));
        env.require(Owners("bob", 1'001));
        env.require(Balance("dan", usd(1)));
        env.require(Owners("dan", 2));

        // Carol offers to buy 1000 XRP for 1000 USD. She removes Bob's next
        // 1000 offers as unfunded and hits the step limit.
        env(offer("carol", usd(1'000), XRP(1'000)));
        env.require(Balance("carol", usd(kNone)));
        env.require(Owners("carol", 1));
        env.require(Balance("bob", usd(0)));
        env.require(Owners("bob", 1));
        env.require(Balance("dan", usd(1)));
        env.require(Owners("dan", 2));
    }

    void
    testCrossingLimit(FeatureBitset features)
    {
        testcase("Crossing Limit");

        using namespace jtx;
        Env env(*this, features);

        auto const gw = Account("gateway");

        int const maxConsumed = 1'000;

        env.fund(XRP(100'000'000), gw, "alice", "bob", "carol");
        int const bobsOfferCount = maxConsumed + 150;
        MPT const usd =
            MPTTester({.env = env, .issuer = gw, .holders = {"bob"}, .maxAmt = bobsOfferCount});
        env(pay(gw, "bob", usd(bobsOfferCount)));
        env.close();
        nOffers(env, bobsOfferCount, "bob", XRP(1), usd(1));

        // Alice offers to buy Bob's offers. However, she hits the offer
        // crossing limit, so she can't buy them all at once.
        env(offer("alice", usd(bobsOfferCount), XRP(bobsOfferCount)));
        env.close();
        env.require(Balance("alice", usd(maxConsumed)));
        env.require(Balance("bob", usd(150)));
        env.require(Owners("bob", 150 + 1));

        // Carol offers to buy 1000 XRP for 1000 USD. She takes Bob's
        // remaining 150 offers without hitting a limit.
        env(offer("carol", usd(1'000), XRP(1'000)));
        env.close();
        env.require(Balance("carol", usd(150)));
        env.require(Balance("bob", usd(0)));
        env.require(Owners("bob", 1));
    }

    void
    testStepAndCrossingLimit(FeatureBitset features)
    {
        testcase("Step And Crossing Limit");

        using namespace jtx;
        Env env(*this, features);

        auto const gw = Account("gateway");

        env.fund(XRP(100'000'000), gw, "alice", "bob", "carol", "dan", "evita");

        int const maxConsumed = 1'000;
        int const evitaOfferCount{maxConsumed + 49};

        MPT const usd = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {"bob", "alice", "carol", "evita"},
             .maxAmt = 2'000 + evitaOfferCount + 1});

        env(pay(gw, "alice", usd(1000)));
        env(pay(gw, "carol", usd(1)));
        env(pay(gw, "evita", usd(evitaOfferCount + 1)));

        // Give carol an extra 150 (unfunded) offers when we're using Taker
        // to accommodate that difference.
        int const carolOfferCount{700};
        nOffers(env, 400, "alice", XRP(1), usd(1));
        nOffers(env, carolOfferCount, "carol", XRP(1), usd(1));
        nOffers(env, evitaOfferCount, "evita", XRP(1), usd(1));

        // Bob offers to buy 1000 XRP for 1000 USD. He takes all 400 USD from
        // Alice's offers, 1 USD from Carol's and then removes 599 of Carol's
        // offers as unfunded, before hitting the step limit.
        env(offer("bob", usd(1000), XRP(1000)));
        env.require(Balance("bob", usd(401)));
        env.require(Balance("alice", usd(600)));
        env.require(Owners("alice", 1));
        env.require(Balance("carol", usd(0)));
        env.require(Owners("carol", carolOfferCount - 599));
        env.require(Balance("evita", usd(evitaOfferCount + 1)));
        env.require(Owners("evita", evitaOfferCount + 1));

        // Dan offers to buy maxConsumed + 50 XRP USD. He removes all of
        // Carol's remaining offers as unfunded, then takes
        // (maxConsumed - 100) USD from Evita's, hitting the crossing limit.
        env(offer("dan", usd(maxConsumed + 50), XRP(maxConsumed + 50)));
        env.require(Balance("dan", usd(maxConsumed - 100)));
        env.require(Owners("dan", 2));
        env.require(Balance("alice", usd(600)));
        env.require(Owners("alice", 1));
        env.require(Balance("carol", usd(0)));
        env.require(Owners("carol", 1));
        env.require(Balance("evita", usd(150)));
        env.require(Owners("evita", 150));
    }

    void
    testAutoBridgedLimits(FeatureBitset features)
    {
        testcase("Auto Bridged Limits");

        // Extracts as much as possible in one book at one Quality
        // before proceeding to the other book.  This reduces the number of
        // times we change books.

        // If any book step in a payment strand consumes 1000 offers, the
        // liquidity from the offers is used, but that strand will be marked as
        // dry for the remainder of the transaction.

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

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
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(100'000'000), gw, alice, bob, carol);

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = kMaxMpTokenAmount});
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {bob},
                     .limit = kMaxMpTokenAmount});

                env(pay(gw, alice, usd(4'000)));
                env(pay(gw, carol, usd(3)));

                // Notice the strand with the 800 unfunded offers has the
                // initial best quality
                nOffers(env, 2'000, alice, eur(2), XRP(1));
                nOffers(env, 100, alice, XRP(1), usd(4));
                nOffers(env, 801, carol, XRP(1),
                        usd(3));  // only one offer is funded
                nOffers(env, 1'000, alice, XRP(1), usd(3));

                nOffers(env, 1, alice, eur(500), usd(500));

                // Bob offers to buy 2000 USD for 2000 EUR; He starts with 2000
                // EUR
                //  1. The best quality is the autobridged offers that take 2
                //  EUR and give 4 USD.
                //     Bob spends 200 EUR and receives 400 USD.
                //     100 EUR->XRP offers consumed.
                //     100 XRP->USD offers consumed.
                //     200 total offers consumed.
                //
                //  2. The best quality is the autobridged offers that take 2
                //  EUR and give 3 USD.
                //     a. One of Carol's offers is taken. This leaves her other
                //     offers unfunded.
                //     b. Carol's remaining 800 offers are consumed as unfunded.
                //     c. 199 of alice's XRP(1) to USD(3) offers are consumed.
                //        A book step is allowed to consume a maximum of 1000
                //        offers at a given quality, and that limit is now
                //        reached.
                //     d. Now the strand is dry, even though there are still
                //     funded XRP(1) to USD(3) offers available.
                //        Bob has spent 400 EUR and received 600 USD in this
                //        step. 200 EUR->XRP offers consumed 800 unfunded
                //        XRP->USD offers consumed 200 funded XRP->USD offers
                //        consumed (1 carol, 199 alice) 1400 total offers
                //        consumed so far (100 left before the limit)
                //  3. The best is the non-autobridged offers that takes 500 EUR
                //  and gives 500 USD.
                //     Bob started with 2000 EUR
                //     Bob spent 500 EUR (100+400)
                //     Bob has 1500 EUR left
                //     In this step:
                //     Bob spends 500 EUR and receives 500 USD.
                // In total:
                //           Bob spent 1100 EUR (200 + 400 + 500)
                //           Bob has 900 EUR remaining (2000 - 1100)
                //           Bob received 1500 USD (400 + 600 + 500)
                //           Alice spent 1497 USD (100*4 + 199*3 + 500)
                //           Alice has 2503 remaining (4000 - 1497)
                //           Alice received 1100 EUR (200 + 400 + 500)
                env(pay(gw, bob, eur(2'000)));
                env.close();
                env(offer(bob, usd(4'000), eur(4'000)));
                env.close();

                env.require(Balance(bob, usd(1'500)));
                env.require(Balance(bob, eur(900)));
                env.require(offers(bob, 1));
                env.require(Owners(bob, 3));

                env.require(Balance(alice, usd(2'503)));
                env.require(Balance(alice, eur(1'100)));
                auto const numAOffers = 2'000 + 100 + 1'000 + 1 - (2 * 100 + 2 * 199 + 1 + 1);
                env.require(offers(alice, numAOffers));
                env.require(Owners(alice, numAOffers + 2));

                env.require(offers(carol, 0));
            };
            testHelper2TokensMix(test);
        }
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(100'000'000), gw, alice, bob, carol);

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = kMaxMpTokenAmount});
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {bob},
                     .limit = kMaxMpTokenAmount});

                env(pay(gw, alice, usd(4'000)));
                env(pay(gw, carol, usd(3)));

                // Notice the strand with the 800 unfunded offers does not have
                // the initial best quality
                nOffers(env, 1, alice, eur(1), usd(10));
                nOffers(env, 2'000, alice, eur(2), XRP(1));
                nOffers(env, 100, alice, XRP(1), usd(4));
                nOffers(env, 801, carol, XRP(1),
                        usd(3));  // only one offer is funded
                nOffers(env, 1'000, alice, XRP(1), usd(3));

                nOffers(env, 1, alice, eur(499), usd(499));

                // Bob offers to buy 2000 USD for 2000 EUR; He starts with 2000
                // EUR
                //  1. The best quality is the offer that takes 1 EUR and gives
                //  10 USD
                //     Bob spends 1 EUR and receives 10 USD.
                //
                //  2. The best quality is the autobridged offers that takes 2
                //  EUR and gives 4 USD.
                //     Bob spends 200 EUR and receives 400 USD.
                //
                //  3. The best quality is the autobridged offers that takes 2
                //  EUR and gives 3 USD.
                //     a. One of Carol's offers is taken. This leaves her other
                //     offers unfunded.
                //     b. Carol's remaining 800 offers are consumed as unfunded.
                //     c. 199 of alice's XRP(1) to USD(3) offers are consumed.
                //        A book step is allowed to consume a maximum of 1000
                //        offers at a given quality, and that limit is now
                //        reached.
                //     d. Now the strand is dry, even though there are still
                //     funded XRP(1) to USD(3) offers available. Bob has spent
                //     400 EUR and received 600 USD in this step. (200 funded
                //     offers consumed 800 unfunded offers)
                //  4. The best is the non-autobridged offers that takes 499 EUR
                //  and gives 499 USD.
                //     Bob has 2000 EUR, and has spent 1+200+400=601 EUR. He has
                //     1399 left. Bob spent 499 EUR and receives 499 USD.
                // In total: Bob spent EUR(1 + 200 + 400 + 499) = EUR(1100). He
                // started with 2000 so has 900 remaining
                //           Bob received USD(10 + 400 + 600 + 499) = USD(1509).
                //           Alice spent 10 + 100*4 + 199*3 + 499 = 1506 USD.
                //           She started with 4000 so has 2494 USD remaining.
                //           Alice received 200 + 400 + 500 = 1100 EUR
                env.close();
                env(pay(gw, bob, eur(2'000)));
                env.close();
                env(offer(bob, usd(4'000), eur(4'000)));
                env.close();

                env.require(Balance(bob, usd(1'509)));
                env.require(Balance(bob, eur(900)));
                env.require(offers(bob, 1));
                env.require(Owners(bob, 3));

                env.require(Balance(alice, usd(2'494)));
                env.require(Balance(alice, eur(1'100)));
                auto const numAOffers =
                    1 + 2'000 + 100 + 1'000 + 1 - (1 + 2 * 100 + 2 * 199 + 1 + 1);
                env.require(offers(alice, numAOffers));
                env.require(Owners(alice, numAOffers + 2));

                env.require(offers(carol, 0));
            };
            testHelper2TokensMix(test);
        }
    }

    void
    testOfferOverflow(FeatureBitset features)
    {
        testcase("Offer Overflow");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env(*this, features);

        env.fund(XRP(100'000'000), gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        env(pay(gw, alice, usd(8'000)));
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
        nOffers(env, 998, alice, XRP(1.00), usd(1));
        nOffers(env, 998, alice, XRP(0.99), usd(1));
        nOffers(env, 998, alice, XRP(0.98), usd(1));
        nOffers(env, 998, alice, XRP(0.97), usd(1));
        nOffers(env, 998, alice, XRP(0.96), usd(1));
        nOffers(env, 998, alice, XRP(0.95), usd(1));

        auto const expectedTER = tesSUCCESS;

        env(offer(bob, usd(8'000), XRP(8'000)), Ter(expectedTER));
        env.close();

        auto const expectedUSD = usd(1'996);

        env.require(Balance(bob, expectedUSD));
    }

    void
    run() override
    {
        using namespace jtx;
        auto const features = testableAmendments();
        testStepLimit(features);
        testCrossingLimit(features);
        testStepAndCrossingLimit(features);
        testAutoBridgedLimits(features);
        testOfferOverflow(features);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(CrossingLimitsMPT, tx, xrpl, 10);

}  // namespace xrpl::test
