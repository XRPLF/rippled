//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <initializer_list>

namespace ripple {
namespace test {

class ReducedOffer_test : public beast::unit_test::suite
{
    static auto
    ledgerEntryOffer(
        jtx::Env& env,
        jtx::Account const& acct,
        std::uint32_t offer_seq)
    {
        Json::Value jvParams;
        jvParams[jss::offer][jss::account] = acct.human();
        jvParams[jss::offer][jss::seq] = offer_seq;
        return env.rpc(
            "json", "ledger_entry", to_string(jvParams))[jss::result];
    }

    static bool
    offerInLedger(
        jtx::Env& env,
        jtx::Account const& acct,
        std::uint32_t offerSeq)
    {
        Json::Value ledgerOffer = ledgerEntryOffer(env, acct, offerSeq);
        return !(
            ledgerOffer.isMember(jss::error) &&
            ledgerOffer[jss::error].asString() == "entryNotFound");
    }

    // Common code to clean up unneeded offers.
    static void
    cleanupOldOffers(
        jtx::Env& env,
        std::initializer_list<std::pair<jtx::Account const&, std::uint32_t>>
            list)
    {
        for (auto [acct, offerSeq] : list)
            env(offer_cancel(acct, offerSeq));
        env.close();
    }

public:
    void
    testPartialCrossNewXrpIouQChange()
    {
        testcase("exercise partial cross new XRP/IOU offer Q change");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];

        // Make one test run without fixReducedOffersV1 and one with.
        for (FeatureBitset features :
             {supported_amendments() - fixReducedOffersV1,
              supported_amendments() | fixReducedOffersV1})
        {
            Env env{*this, features};

            // Make sure none of the offers we generate are under funded.
            env.fund(XRP(10'000'000), gw, alice, bob);
            env.close();

            env(trust(alice, USD(10'000'000)));
            env(trust(bob, USD(10'000'000)));
            env.close();

            env(pay(gw, bob, USD(10'000'000)));
            env.close();

            // Lambda that:
            //  1. Exercises one offer pair,
            //  2. Collects the results, and
            //  3. Cleans up for the next offer pair.
            // Returns 1 if the crossed offer has a bad rate for the book.
            auto exerciseOfferPair =
                [this, &env, &alice, &bob](
                    Amounts const& inLedger,
                    Amounts const& newOffer) -> unsigned int {
                // Put inLedger offer in the ledger so newOffer can cross it.
                std::uint32_t const aliceOfferSeq = env.seq(alice);
                env(offer(alice, inLedger.in, inLedger.out));
                env.close();

                // Now alice's offer will partially cross bob's offer.
                STAmount const initialRate = Quality(newOffer).rate();
                std::uint32_t const bobOfferSeq = env.seq(bob);
                STAmount const bobInitialBalance = env.balance(bob);
                STAmount const bobsFee = drops(10);
                env(offer(bob, newOffer.in, newOffer.out, tfSell),
                    fee(bobsFee));
                env.close();
                STAmount const bobFinalBalance = env.balance(bob);

                // alice's offer should be fully crossed and so gone from
                // the ledger.
                if (!BEAST_EXPECT(!offerInLedger(env, alice, aliceOfferSeq)))
                    // If the in-ledger offer was not consumed then further
                    // results are meaningless.
                    return 1;

                // bob's offer should be in the ledger, but reduced in size.
                unsigned int badRate = 1;
                {
                    Json::Value bobOffer =
                        ledgerEntryOffer(env, bob, bobOfferSeq);

                    STAmount const reducedTakerGets = amountFromJson(
                        sfTakerGets, bobOffer[jss::node][sfTakerGets.jsonName]);
                    STAmount const reducedTakerPays = amountFromJson(
                        sfTakerPays, bobOffer[jss::node][sfTakerPays.jsonName]);
                    STAmount const bobGot =
                        env.balance(bob) + bobsFee - bobInitialBalance;
                    BEAST_EXPECT(reducedTakerPays < newOffer.in);
                    BEAST_EXPECT(reducedTakerGets < newOffer.out);
                    STAmount const inLedgerRate =
                        Quality(Amounts{reducedTakerPays, reducedTakerGets})
                            .rate();

                    badRate = inLedgerRate > initialRate ? 1 : 0;

                    // If the inLedgerRate is less than initial rate, then
                    // incrementing the mantissa of the reduced taker pays
                    // should result in a rate higher than initial.  Check
                    // this to verify that the largest allowable TakerPays
                    // was computed.
                    if (badRate == 0)
                    {
                        STAmount const tweakedTakerPays =
                            reducedTakerPays + drops(1);
                        STAmount const tweakedRate =
                            Quality(Amounts{tweakedTakerPays, reducedTakerGets})
                                .rate();
                        BEAST_EXPECT(tweakedRate > initialRate);
                    }
#if 0
                    std::cout << "Placed rate: " << initialRate
                              << "; in-ledger rate: " << inLedgerRate
                              << "; TakerPays: " << reducedTakerPays
                              << "; TakerGets: " << reducedTakerGets
                              << "; bob already got: " << bobGot << std::endl;
// #else
                    std::string_view filler =
                        inLedgerRate > initialRate ? "**" : "  ";
                    std::cout << "| `" << reducedTakerGets << "` | `"
                              << reducedTakerPays << "` | `" << initialRate
                              << "` | " << filler << "`" << inLedgerRate << "`"
                              << filler << " |`" << std::endl;
#endif
                }

                // In preparation for the next iteration make sure the two
                // offers are gone from the ledger.
                cleanupOldOffers(
                    env, {{alice, aliceOfferSeq}, {bob, bobOfferSeq}});
                return badRate;
            };

            // bob's offer (the new offer) is the same every time:
            Amounts const bobsOffer{
                STAmount(XRP(1)), STAmount(USD.issue(), 1, 0)};

            // alice's offer has a slightly smaller TakerPays with each
            // iteration.  This should mean that the size of the offer bob
            // places in the ledger should increase with each iteration.
            unsigned int blockedCount = 0;
            for (std::uint64_t mantissaReduce = 1'000'000'000ull;
                 mantissaReduce <= 5'000'000'000ull;
                 mantissaReduce += 20'000'000ull)
            {
                STAmount aliceUSD{
                    bobsOffer.out.issue(),
                    bobsOffer.out.mantissa() - mantissaReduce,
                    bobsOffer.out.exponent()};
                STAmount aliceXRP{
                    bobsOffer.in.issue(), bobsOffer.in.mantissa() - 1};
                Amounts alicesOffer{aliceUSD, aliceXRP};
                blockedCount += exerciseOfferPair(alicesOffer, bobsOffer);
            }

            // If fixReducedOffersV1 is enabled, then none of the test cases
            // should produce a potentially blocking rate.
            //
            // Also verify that if fixReducedOffersV1 is not enabled then
            // some of the test cases produced a potentially blocking rate.
            if (features[fixReducedOffersV1])
            {
                BEAST_EXPECT(blockedCount == 0);
            }
            else
            {
                BEAST_EXPECT(blockedCount >= 170);
            }
        }
    }

    void
    testPartialCrossOldXrpIouQChange()
    {
        testcase("exercise partial cross old XRP/IOU offer Q change");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];

        // Make one test run without fixReducedOffersV1 and one with.
        for (FeatureBitset features :
             {supported_amendments() - fixReducedOffersV1,
              supported_amendments() | fixReducedOffersV1})
        {
            // Make sure none of the offers we generate are under funded.
            Env env{*this, features};
            env.fund(XRP(10'000'000), gw, alice, bob);
            env.close();

            env(trust(alice, USD(10'000'000)));
            env(trust(bob, USD(10'000'000)));
            env.close();

            env(pay(gw, alice, USD(10'000'000)));
            env.close();

            // Lambda that:
            //  1. Exercises one offer pair,
            //  2. Collects the results, and
            //  3. Cleans up for the next offer pair.
            auto exerciseOfferPair =
                [this, &env, &alice, &bob](
                    Amounts const& inLedger,
                    Amounts const& newOffer) -> unsigned int {
                // Get the inLedger offer into the ledger so newOffer can cross
                // it.
                STAmount const initialRate = Quality(inLedger).rate();
                std::uint32_t const aliceOfferSeq = env.seq(alice);
                env(offer(alice, inLedger.in, inLedger.out));
                env.close();

                // Now bob's offer will partially cross alice's offer.
                std::uint32_t const bobOfferSeq = env.seq(bob);
                STAmount const aliceInitialBalance = env.balance(alice);
                env(offer(bob, newOffer.in, newOffer.out));
                env.close();
                STAmount const aliceFinalBalance = env.balance(alice);

                // bob's offer should not have made it into the ledger.
                if (!BEAST_EXPECT(!offerInLedger(env, bob, bobOfferSeq)))
                {
                    // If the in-ledger offer was not consumed then further
                    // results are meaningless.
                    cleanupOldOffers(
                        env, {{alice, aliceOfferSeq}, {bob, bobOfferSeq}});
                    return 1;
                }
                // alice's offer should still be in the ledger, but reduced in
                // size.
                unsigned int badRate = 1;
                {
                    Json::Value aliceOffer =
                        ledgerEntryOffer(env, alice, aliceOfferSeq);

                    STAmount const reducedTakerGets = amountFromJson(
                        sfTakerGets,
                        aliceOffer[jss::node][sfTakerGets.jsonName]);
                    STAmount const reducedTakerPays = amountFromJson(
                        sfTakerPays,
                        aliceOffer[jss::node][sfTakerPays.jsonName]);
                    STAmount const aliceGot =
                        env.balance(alice) - aliceInitialBalance;
                    BEAST_EXPECT(reducedTakerPays < inLedger.in);
                    BEAST_EXPECT(reducedTakerGets < inLedger.out);
                    STAmount const inLedgerRate =
                        Quality(Amounts{reducedTakerPays, reducedTakerGets})
                            .rate();
                    badRate = inLedgerRate > initialRate ? 1 : 0;

                    // If the inLedgerRate is less than initial rate, then
                    // incrementing the mantissa of the reduced taker pays
                    // should result in a rate higher than initial.  Check
                    // this to verify that the largest allowable TakerPays
                    // was computed.
                    if (badRate == 0)
                    {
                        STAmount const tweakedTakerPays =
                            reducedTakerPays + drops(1);
                        STAmount const tweakedRate =
                            Quality(Amounts{tweakedTakerPays, reducedTakerGets})
                                .rate();
                        BEAST_EXPECT(tweakedRate > initialRate);
                    }
#if 0
                    std::cout << "Placed rate: " << initialRate
                              << "; in-ledger rate: " << inLedgerRate
                              << "; TakerPays: " << reducedTakerPays
                              << "; TakerGets: " << reducedTakerGets
                              << "; alice already got: " << aliceGot
                              << std::endl;
// #else
                    std::string_view filler = badRate ? "**" : "  ";
                    std::cout << "| `" << reducedTakerGets << "` | `"
                              << reducedTakerPays << "` | `" << initialRate
                              << "` | " << filler << "`" << inLedgerRate << "`"
                              << filler << " | `" << aliceGot << "` |"
                              << std::endl;
#endif
                }

                // In preparation for the next iteration make sure the two
                // offers are gone from the ledger.
                cleanupOldOffers(
                    env, {{alice, aliceOfferSeq}, {bob, bobOfferSeq}});
                return badRate;
            };

            // alice's offer (the old offer) is the same every time:
            Amounts const aliceOffer{
                STAmount(XRP(1)), STAmount(USD.issue(), 1, 0)};

            // bob's offer has a slightly smaller TakerPays with each iteration.
            // This should mean that the size of the offer alice leaves in the
            // ledger should increase with each iteration.
            unsigned int blockedCount = 0;
            for (std::uint64_t mantissaReduce = 1'000'000'000ull;
                 mantissaReduce <= 4'000'000'000ull;
                 mantissaReduce += 20'000'000ull)
            {
                STAmount bobUSD{
                    aliceOffer.out.issue(),
                    aliceOffer.out.mantissa() - mantissaReduce,
                    aliceOffer.out.exponent()};
                STAmount bobXRP{
                    aliceOffer.in.issue(), aliceOffer.in.mantissa() - 1};
                Amounts bobsOffer{bobUSD, bobXRP};

                blockedCount += exerciseOfferPair(aliceOffer, bobsOffer);
            }

            // If fixReducedOffersV1 is enabled, then none of the test cases
            // should produce a potentially blocking rate.
            //
            // Also verify that if fixReducedOffersV1 is not enabled then
            // some of the test cases produced a potentially blocking rate.
            if (features[fixReducedOffersV1])
            {
                BEAST_EXPECT(blockedCount == 0);
            }
            else
            {
                BEAST_EXPECT(blockedCount > 10);
            }
        }
    }

    void
    testUnderFundedXrpIouQChange()
    {
        testcase("exercise underfunded XRP/IOU offer Q change");

        // Bob places an offer that is not fully funded.
        //
        // This unit test compares the behavior of this situation before and
        // after applying the fixReducedOffersV1 amendment.

        using namespace jtx;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];

        // Make one test run without fixReducedOffersV1 and one with.
        for (FeatureBitset features :
             {supported_amendments() - fixReducedOffersV1,
              supported_amendments() | fixReducedOffersV1})
        {
            Env env{*this, features};

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(1000), alice, bob);

            int blockedOrderBookCount = 0;
            for (STAmount initialBobUSD = USD(0.45); initialBobUSD <= USD(1);
                 initialBobUSD += USD(0.025))
            {
                // underfund bob's offer
                env(pay(gw, bob, initialBobUSD));
                env.close();

                std::uint32_t const bobOfferSeq = env.seq(bob);
                env(offer(bob, drops(2), USD(1)));
                env.close();

                // alice places an offer that would cross bob's if bob's were
                // well funded.
                std::uint32_t const aliceOfferSeq = env.seq(alice);
                env(offer(alice, USD(1), drops(2)));
                env.close();

                // We want to detect order book blocking.  If:
                //  1. bob's offer is still in the ledger and
                //  2. alice received no USD
                // then we use that as evidence that bob's offer blocked the
                // order book.
                {
                    bool const bobsOfferGone =
                        !offerInLedger(env, bob, bobOfferSeq);
                    STAmount const aliceBalanceUSD = env.balance(alice, USD);

                    // Sanity check the ledger if alice got USD.
                    if (aliceBalanceUSD.signum() > 0)
                    {
                        BEAST_EXPECT(aliceBalanceUSD == initialBobUSD);
                        BEAST_EXPECT(env.balance(bob, USD) == USD(0));
                        BEAST_EXPECT(bobsOfferGone);
                    }

                    // Track occurrences of order book blocking.
                    if (!bobsOfferGone && aliceBalanceUSD.signum() == 0)
                    {
                        ++blockedOrderBookCount;
                    }

                    // In preparation for the next iteration clean up any
                    // leftover offers.
                    cleanupOldOffers(
                        env, {{alice, aliceOfferSeq}, {bob, bobOfferSeq}});

                    // Zero out alice's and bob's USD balances.
                    if (STAmount const aliceBalance = env.balance(alice, USD);
                        aliceBalance.signum() > 0)
                        env(pay(alice, gw, aliceBalance));

                    if (STAmount const bobBalance = env.balance(bob, USD);
                        bobBalance.signum() > 0)
                        env(pay(bob, gw, bobBalance));

                    env.close();
                }
            }

            // If fixReducedOffersV1 is enabled, then none of the test cases
            // should produce a potentially blocking rate.
            //
            // Also verify that if fixReducedOffersV1 is not enabled then
            // some of the test cases produced a potentially blocking rate.
            if (features[fixReducedOffersV1])
            {
                BEAST_EXPECT(blockedOrderBookCount == 0);
            }
            else
            {
                BEAST_EXPECT(blockedOrderBookCount > 15);
            }
        }
    }

    void
    testUnderFundedIouIouQChange()
    {
        testcase("exercise underfunded IOU/IOU offer Q change");

        // Bob places an IOU/IOU offer that is not fully funded.
        //
        // This unit test compares the behavior of this situation before and
        // after applying the fixReducedOffersV1 amendment.

        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gw"};

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        STAmount const tinyUSD(USD.issue(), /*mantissa*/ 1, /*exponent*/ -81);

        // Make one test run without fixReducedOffersV1 and one with.
        for (FeatureBitset features :
             {supported_amendments() - fixReducedOffersV1,
              supported_amendments() | fixReducedOffersV1})
        {
            Env env{*this, features};

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(1000), alice, bob);
            env.trust(EUR(1000), alice, bob);

            STAmount const eurOffer(
                EUR.issue(), /*mantissa*/ 2957, /*exponent*/ -76);
            STAmount const usdOffer(
                USD.issue(), /*mantissa*/ 7109, /*exponent*/ -76);

            STAmount const endLoop(
                USD.issue(), /*mantissa*/ 50, /*exponent*/ -81);

            int blockedOrderBookCount = 0;
            for (STAmount initialBobUSD = tinyUSD; initialBobUSD <= endLoop;
                 initialBobUSD += tinyUSD)
            {
                // underfund bob's offer
                env(pay(gw, bob, initialBobUSD));
                env(pay(gw, alice, EUR(100)));
                env.close();

                // This offer is underfunded
                std::uint32_t bobOfferSeq = env.seq(bob);
                env(offer(bob, eurOffer, usdOffer));
                env.close();
                env.require(offers(bob, 1));

                // alice places an offer that crosses bob's.
                std::uint32_t aliceOfferSeq = env.seq(alice);
                env(offer(alice, usdOffer, eurOffer));
                env.close();

                // Examine the aftermath of alice's offer.
                {
                    bool const bobsOfferGone =
                        !offerInLedger(env, bob, bobOfferSeq);
                    STAmount aliceBalanceUSD = env.balance(alice, USD);
#if 0
                    std::cout
                        << "bobs initial: " << initialBobUSD
                        << "; alice final: " << aliceBalanceUSD
                        << "; bobs offer: " << bobsOfferJson.toStyledString()
                        << std::endl;
#endif
                    // Sanity check the ledger if alice got USD.
                    if (aliceBalanceUSD.signum() > 0)
                    {
                        BEAST_EXPECT(aliceBalanceUSD == initialBobUSD);
                        BEAST_EXPECT(env.balance(bob, USD) == USD(0));
                        BEAST_EXPECT(bobsOfferGone);
                    }

                    // Track occurrences of order book blocking.
                    if (!bobsOfferGone && aliceBalanceUSD.signum() == 0)
                    {
                        ++blockedOrderBookCount;
                    }
                }

                // In preparation for the next iteration clean up any
                // leftover offers.
                cleanupOldOffers(
                    env, {{alice, aliceOfferSeq}, {bob, bobOfferSeq}});

                // Zero out alice's and bob's IOU balances.
                auto zeroBalance = [&env, &gw](
                                       Account const& acct, IOU const& iou) {
                    if (STAmount const balance = env.balance(acct, iou);
                        balance.signum() > 0)
                        env(pay(acct, gw, balance));
                };

                zeroBalance(alice, EUR);
                zeroBalance(alice, USD);
                zeroBalance(bob, EUR);
                zeroBalance(bob, USD);
                env.close();
            }

            // If fixReducedOffersV1 is enabled, then none of the test cases
            // should produce a potentially blocking rate.
            //
            // Also verify that if fixReducedOffersV1 is not enabled then
            // some of the test cases produced a potentially blocking rate.
            if (features[fixReducedOffersV1])
            {
                BEAST_EXPECT(blockedOrderBookCount == 0);
            }
            else
            {
                BEAST_EXPECT(blockedOrderBookCount > 20);
            }
        }
    }

    Amounts
    jsonOfferToAmounts(Json::Value const& json)
    {
        STAmount const in =
            amountFromJson(sfTakerPays, json[sfTakerPays.jsonName]);
        STAmount const out =
            amountFromJson(sfTakerGets, json[sfTakerGets.jsonName]);
        return {in, out};
    }

    void
    testSellPartialCrossOldXrpIouQChange()
    {
        // This test case was motivated by Issue #4937.  It recreates
        // the specific failure identified in that issue and samples some other
        // cases in the same vicinity to make sure that the new behavior makes
        // sense.
        testcase("exercise tfSell partial cross old XRP/IOU offer Q change");

        using namespace jtx;

        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        auto const USD = gw["USD"];

        // Make one test run without fixReducedOffersV2 and one with.
        for (FeatureBitset features :
             {supported_amendments() - fixReducedOffersV2,
              supported_amendments() | fixReducedOffersV2})
        {
            // Make sure none of the offers we generate are under funded.
            Env env{*this, features};
            env.fund(XRP(10'000'000), gw, alice, bob, carol);
            env.close();

            env(trust(alice, USD(10'000'000)));
            env(trust(bob, USD(10'000'000)));
            env(trust(carol, USD(10'000'000)));
            env.close();

            env(pay(gw, alice, USD(10'000'000)));
            env(pay(gw, bob, USD(10'000'000)));
            env(pay(gw, carol, USD(10'000'000)));
            env.close();

            // Lambda that:
            //  1. Exercises one offer trio,
            //  2. Collects the results, and
            //  3. Cleans up for the next offer trio.
            auto exerciseOfferTrio =
                [this, &env, &alice, &bob, &carol, &USD](
                    Amounts const& carolOffer) -> unsigned int {
                // alice submits an offer that may become a blocker.
                std::uint32_t const aliceOfferSeq = env.seq(alice);
                static Amounts const aliceInitialOffer(USD(2), drops(3382562));
                env(offer(alice, aliceInitialOffer.in, aliceInitialOffer.out));
                env.close();
                STAmount const initialRate =
                    Quality(jsonOfferToAmounts(ledgerEntryOffer(
                                env, alice, aliceOfferSeq)[jss::node]))
                        .rate();

                // bob submits an offer that is more desirable than alice's
                std::uint32_t const bobOfferSeq = env.seq(bob);
                env(offer(bob, USD(0.97086565812384), drops(1642020)));
                env.close();

                // Now carol's offer consumes bob's and partially crosses
                // alice's.  The tfSell flag is important.
                std::uint32_t const carolOfferSeq = env.seq(carol);
                env(offer(carol, carolOffer.in, carolOffer.out),
                    txflags(tfSell));
                env.close();

                // carol's offer should not have made it into the ledger and
                // bob's offer should be fully consumed.
                if (!BEAST_EXPECT(
                        !offerInLedger(env, carol, carolOfferSeq) &&
                        !offerInLedger(env, bob, bobOfferSeq)))
                {
                    // If carol's or bob's offers are still in the ledger then
                    // further results are meaningless.
                    cleanupOldOffers(
                        env,
                        {{alice, aliceOfferSeq},
                         {bob, bobOfferSeq},
                         {carol, carolOfferSeq}});
                    return 1;
                }
                // alice's offer should still be in the ledger, but reduced in
                // size.
                unsigned int badRate = 1;
                {
                    Json::Value aliceOffer =
                        ledgerEntryOffer(env, alice, aliceOfferSeq);

                    Amounts aliceReducedOffer =
                        jsonOfferToAmounts(aliceOffer[jss::node]);

                    BEAST_EXPECT(aliceReducedOffer.in < aliceInitialOffer.in);
                    BEAST_EXPECT(aliceReducedOffer.out < aliceInitialOffer.out);
                    STAmount const inLedgerRate =
                        Quality(aliceReducedOffer).rate();
                    badRate = inLedgerRate > initialRate ? 1 : 0;

                    // If the inLedgerRate is less than initial rate, then
                    // incrementing the mantissa of the reduced TakerGets
                    // should result in a rate higher than initial.  Check
                    // this to verify that the largest allowable TakerGets
                    // was computed.
                    if (badRate == 0)
                    {
                        STAmount const tweakedTakerGets(
                            aliceReducedOffer.in.issue(),
                            aliceReducedOffer.in.mantissa() + 1,
                            aliceReducedOffer.in.exponent(),
                            aliceReducedOffer.in.negative());
                        STAmount const tweakedRate =
                            Quality(
                                Amounts{aliceReducedOffer.in, tweakedTakerGets})
                                .rate();
                        BEAST_EXPECT(tweakedRate > initialRate);
                    }
#if 0
                    std::cout << "Placed rate: " << initialRate
                              << "; in-ledger rate: " << inLedgerRate
                              << "; TakerPays: " << aliceReducedOffer.in
                              << "; TakerGets: " << aliceReducedOffer.out
                              << std::endl;
// #else
                    std::string_view filler = badRate ? "**" : "  ";
                    std::cout << "| " << aliceReducedOffer.in << "` | `"
                              << aliceReducedOffer.out << "` | `" << initialRate
                              << "` | " << filler << "`" << inLedgerRate << "`"
                              << filler << std::endl;
#endif
                }

                // In preparation for the next iteration make sure all three
                // offers are gone from the ledger.
                cleanupOldOffers(
                    env,
                    {{alice, aliceOfferSeq},
                     {bob, bobOfferSeq},
                     {carol, carolOfferSeq}});
                return badRate;
            };

            constexpr int loopCount = 100;
            unsigned int blockedCount = 0;
            {
                STAmount increaseGets = USD(0);
                STAmount const step(increaseGets.issue(), 1, -8);
                for (unsigned int i = 0; i < loopCount; ++i)
                {
                    blockedCount += exerciseOfferTrio(
                        Amounts(drops(1642020), USD(1) + increaseGets));
                    increaseGets += step;
                }
            }

            // If fixReducedOffersV2 is enabled, then none of the test cases
            // should produce a potentially blocking rate.
            //
            // Also verify that if fixReducedOffersV2 is not enabled then
            // some of the test cases produced a potentially blocking rate.
            if (features[fixReducedOffersV2])
            {
                BEAST_EXPECT(blockedCount == 0);
            }
            else
            {
                BEAST_EXPECT(blockedCount > 80);
            }
        }
    }

    void
    run() override
    {
        testPartialCrossNewXrpIouQChange();
        testPartialCrossOldXrpIouQChange();
        testUnderFundedXrpIouQChange();
        testUnderFundedIouIouQChange();
        testSellPartialCrossOldXrpIouQChange();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(ReducedOffer, tx, ripple, 2);

}  // namespace test
}  // namespace ripple
