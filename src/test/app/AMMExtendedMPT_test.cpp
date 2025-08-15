//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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
#include <test/jtx/PathSet.h>

#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/paths/AMMOffer.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/paths/detail/StrandFlow.h>
#include <xrpld/ledger/PaymentSandbox.h>

#include <xrpl/protocol/Feature.h>

namespace ripple {
namespace test {

/**
 * Tests of AMM MPT that use offers.
 */
struct AMMExtendedMPT_test : public jtx::AMMTest
{
private:
    void
    testRmFundedOffer(FeatureBitset features)
    {
        testcase("Incorrect Removal of Funded Offers");

        // We need at least two paths. One at good quality and one at bad
        // quality.  The bad quality path needs two offer books in a row.
        // Each offer book should have two offers at the same quality, the
        // offers should be completely consumed, and the payment should
        // require both offers to be satisfied. The first offer must
        // be "taker gets" XRP. Ensure that the payment engine does not remove
        // the first "taker gets" xrp offer, because the offer is still
        // funded and not used for the payment.

        using namespace jtx;
        Env env{*this, features};

        fund(env, gw, {alice, bob, carol}, XRP(10'000));

        MPTTester ETH(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .pay = 200'000'000'000'000'000,
             .flags = MPTDEXFlags});

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .pay = 2'000'000'000'000'000,
             .flags = MPTDEXFlags});

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env(offer(carol, BTC(49'000'000'000'000), XRP(49)));
        env(offer(carol, BTC(51'000'000'000'000), XRP(51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env(offer(carol, XRP(50), ETH(50'000'000'000'000)));
        env(offer(carol, XRP(50), ETH(50'000'000'000'000)));

        // Good quality path
        AMM ammCarol(
            env,
            carol,
            BTC(1'000'000'000'000'000),
            ETH(100'100'000'000'000'000));

        PathSet paths(Path(XRP, MPT(ETH)), Path(MPT(ETH)));

        env(pay(alice, bob, ETH(100'000'000'000'000)),
            json(paths.json()),
            sendmax(BTC(1'000'000'000'000'000)),
            txflags(tfPartialPayment));

        BEAST_EXPECT(ammCarol.expectBalances(
            BTC(1'001'000'000'374'815),
            ETH(100'000'000'000'000'000),
            ammCarol.tokens()));

        env.require(balance(bob, ETH(200'100'000'000'000'000)));
        BEAST_EXPECT(isOffer(env, carol, BTC(49'000'000'000'000), XRP(49)));
    }

    void
    testFillModes(FeatureBitset features)
    {
        testcase("Fill Modes");
        using namespace jtx;

        auto const startBalance = XRP(1'000'000);

        // Fill or Kill - unless we fully cross, just charge a fee and don't
        // place the offer on the books.  But also clean up expired offers
        // that are discovered along the way.
        //
        // fix1578 changes the return code.  Verify expected behavior
        // without and with fix1578.
        for (auto const& tweakedFeatures :
             {features - fix1578, features | fix1578})
        {
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    auto const& BTC = MPT(ammAlice[1]);
                    auto const baseFee = env.current()->fees().base;
                    auto carolBTC = env.balance(carol, BTC);
                    auto carolXRP = env.balance(carol, XRP);
                    // Order that can't be filled
                    TER const killedCode{
                        tweakedFeatures[fix1578] ? TER{tecKILLED}
                                                 : TER{tesSUCCESS}};
                    env(offer(carol, BTC(100), XRP(100)),
                        txflags(tfFillOrKill),
                        ter(killedCode));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'100), BTC(10'000), ammAlice.tokens()));
                    // fee = AMM
                    env.require(balance(carol, carolXRP - baseFee));
                    env.require(balance(carol, carolBTC));

                    BEAST_EXPECT(expectOffers(env, carol, 0));
                    carolXRP = env.balance(carol, XRP);

                    // Order that can be filled
                    env(offer(carol, XRP(100), BTC(100)),
                        txflags(tfFillOrKill),
                        ter(tesSUCCESS));
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'000), BTC(10'100), ammAlice.tokens()));
                    env.require(balance(carol, carolXRP + XRP(100) - baseFee));
                    env.require(balance(carol, carolBTC - BTC(100)));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                },
                {{XRP(10'100), AMMMPT(10'000)}},
                0,
                std::nullopt,
                {tweakedFeatures});

            // Immediate or Cancel - cross as much as possible
            // and add nothing on the books.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    auto const& BTC = MPT(ammAlice[1]);
                    auto const baseFee = env.current()->fees().base;
                    auto carolBTC = env.balance(carol, BTC);
                    auto carolXRP = env.balance(carol, XRP);
                    env(offer(carol, XRP(200), BTC(200)),
                        txflags(tfImmediateOrCancel),
                        ter(tesSUCCESS));

                    // AMM generates a synthetic offer of 100BTC/100XRP
                    // to match the CLOB offer quality.
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'000), BTC(10'100), ammAlice.tokens()));
                    // +AMM - offer * fee
                    env.require(balance(carol, carolXRP + XRP(100) - baseFee));
                    env.require(balance(carol, carolBTC - BTC(100)));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                },
                {{XRP(10'100), AMMMPT(10'000)}},
                0,
                std::nullopt,
                {tweakedFeatures});

            // tfPassive -- place the offer without crossing it.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    // Carol creates a passive offer that could cross AMM.
                    // Carol's offer should stay in the ledger.
                    auto const& BTC = MPT(ammAlice[1]);
                    env(offer(carol, XRP(100), BTC(100), tfPassive));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'100), BTC(10'000), ammAlice.tokens()));
                    BEAST_EXPECT(
                        expectOffers(env, carol, 1, {{{XRP(100), BTC(100)}}}));
                },
                {{XRP(10'100), AMMMPT(10'000)}},
                0,
                std::nullopt,
                {tweakedFeatures});

            // tfPassive -- cross only offers of better quality.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    auto const& BTC = MPT(ammAlice[1]);
                    env(offer(alice, BTC(110), XRP(100)));
                    env.close();

                    // Carol creates a passive offer. That offer should cross
                    // AMM and leave Alice's offer untouched.
                    env(offer(carol, XRP(100), BTC(100), tfPassive));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'900), BTC(9083), ammAlice.tokens()));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                    BEAST_EXPECT(expectOffers(env, alice, 1));
                },
                {{XRP(11'000), AMMMPT(9'000)}},
                0,
                std::nullopt,
                {tweakedFeatures});
        }
    }

    void
    testOfferCrossWithXRP(FeatureBitset features)
    {
        testcase("Offer Crossing with XRP, Normal order");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {bob, alice}, XRP(300'000));

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .pay = 100'000'000,
             .flags = MPTDEXFlags});

        AMM ammAlice(env, alice, XRP(150'000), BTC(50'000'000));

        // Existing offer pays better than this wants.
        // Partially consume existing offer.
        // Pay 1'000'000 BTC, get 3061224490 Drops.
        auto const xrpTransferred = XRPAmount{3'061'224'490};
        env(offer(bob, BTC(1'000'000), XRP(4'000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(150'000) + xrpTransferred,
            BTC(49'000'000),
            IOUAmount{273'861'278752583, -5}));

        env.require(balance(bob, BTC(101'000'000)));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, bob, XRP(300'000) - xrpTransferred - 2 * txfee(env, 1)));
        BEAST_EXPECT(expectOffers(env, bob, 0));
    }

    void
    testOfferCrossWithLimitOverride(FeatureBitset features)
    {
        testcase("Offer Crossing with Limit Override");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(200'000), gw, alice, bob);
        env.close();

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = MPTDEXFlags});
        env(pay(gw, alice, BTC(500'000'000)));

        AMM ammAlice(env, alice, XRP(150'000), BTC(51'000'000));
        env(offer(bob, BTC(1'000'000), XRP(3'000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(153'000), BTC(50'000'000), ammAlice.tokens()));

        env.require(balance(bob, BTC(1'000'000)));
        env.require(balance(
            bob, XRP(200'000) - XRP(3'000) - env.current()->fees().base * 2));
    }

    void
    testCurrencyConversionEntire(FeatureBitset features)
    {
        testcase("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, XRP(10'000));
        env.require(owners(bob, 0));

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = MPTDEXFlags});
        env(pay(gw, bob, BTC(1'000'000'000)));

        env.require(owners(alice, 1), owners(bob, 1));

        env(pay(gw, alice, BTC(100'000'000)));
        AMM ammBob(env, bob, BTC(200'000'000), XRP(1'500));

        env(pay(alice, alice, XRP(500)), sendmax(BTC(100'000'000)));

        BEAST_EXPECT(ammBob.expectBalances(
            BTC(300'000'000), XRP(1'000), ammBob.tokens()));
        env.require(balance(alice, BTC(0)));

        auto jrr = ledgerEntryRoot(env, alice);
        env.require(balance(
            alice, XRP(10'000) + XRP(500) - env.current()->fees().base * 2));
    }

    void
    testCurrencyConversionInParts(FeatureBitset features)
    {
        testcase("Currency Conversion: In Parts");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw, bob);
        env.fund(XRP(40'000), alice);

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .pay = 30'000'000'000,
             .flags = MPTDEXFlags});
        env(pay(gw, alice, BTC(10'000'000'000)));

        AMM ammAlice(env, alice, XRP(10'000), BTC(10'000'000'000));
        env.close();

        // Alice converts BTC to XRP which should fail
        // due to PartialPayment.
        env(pay(alice, alice, XRP(100)),
            sendmax(BTC(100'000'000)),
            ter(tecPATH_PARTIAL));

        // Alice converts BTC to XRP, should succeed because
        // we permit partial payment
        env(pay(alice, alice, XRP(100)),
            sendmax(BTC(100'000'000)),
            txflags(tfPartialPayment));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount{9'900'990'100}, BTC(10'100'000'000), ammAlice.tokens()));
        // initial 40,000'000'000 - 10,000'000'000AMM - 100'000'000pay
        env.require(balance(alice, BTC(29'900'000'000)));
        // initial 40,000 - 10,0000AMM + 99.009900pay - fee*3
        BEAST_EXPECT(expectLedgerEntryRoot(
            env,
            alice,
            XRP(40'000) - XRP(10'000) + XRPAmount{99'009'900} - ammCrtFee(env) -
                txfee(env, 3)));
    }

    void
    testCrossCurrencyStartXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Start with XRP");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw);
        env.fund(XRP(40'000), alice);
        env.fund(XRP(1'000), bob);

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = MPTDEXFlags});
        env(pay(gw, alice, BTC(10'100'000'000)));

        AMM ammAlice(env, alice, XRP(10'000), BTC(10'100'000'000));
        env.close();

        env(pay(alice, bob, BTC(100'000'000)), sendmax(XRP(100)));
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'100), BTC(10'000'000'000), ammAlice.tokens()));
        env.require(balance(bob, BTC(100'000'000)));
    }

    void
    testCrossCurrencyEndXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: End with XRP");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw);
        env.fund(XRP(40'100), alice);
        env.fund(XRP(1'000), bob);

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = MPTDEXFlags});
        env(pay(gw, alice, BTC(40'000'000'000)));

        AMM ammAlice(env, alice, XRP(10'100), BTC(10'000'000'000));
        env.close();

        env(pay(alice, bob, XRP(100)), sendmax(BTC(100'000'000)));
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), BTC(10'100'000'000), ammAlice.tokens()));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, bob, XRP(1'000) + XRP(100) - txfee(env, 1)));
    }

    void
    testCrossCurrencyBridged(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Bridged");

        using namespace jtx;

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env(*this);
            auto const dan = Account{"dan"};
            env.fund(XRP(60'000), alice, bob, carol, gw, dan);
            env.close();
            auto const ETH = issue1(
                {.env = env,
                 .token = "ETH",
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan},
                 .limit = 10'000'000'000'000'000});
            auto const BTC = issue2(
                {.env = env,
                 .token = "BTC",
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan},
                 .limit = 10'000'000'000'000'000});
            env(pay(gw, alice, BTC(500'000'000'000'000)));
            env(pay(gw, carol, BTC(6'000'000'000'000'000)));
            env(pay(gw, dan, ETH(400'000'000'000'000)));
            env.close();
            env.close();
            AMM ammCarol(env, carol, BTC(5'000'000'000'000'000), XRP(50'000));

            env(offer(dan, XRP(500), ETH(50'000'000'000'000)));
            env.close();

            Json::Value jtp{Json::arrayValue};
            jtp[0u][0u][jss::currency] = "XRP";
            env(pay(alice, bob, ETH(30'000'000'000'000)),
                json(jss::Paths, jtp),
                sendmax(BTC(333'000'000'000'000)));
            env.close();
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(49'700), BTC(5'030'181'086'519'115), ammCarol.tokens()));
            BEAST_EXPECT(expectOffers(
                env, dan, 1, {{Amounts{XRP(200), ETH(20'000'000'000'000)}}}));
            env.require(balance(bob, ETH(30'000'000'000'000)));
        };
        testHelper2TokensMix(test);
    }

    void
    testOfferFeesConsumeFunds(FeatureBitset features)
    {
        testcase("Offer Fees Consume Funds");

        using namespace jtx;

        Env env{*this, features};

        // Provide micro amounts to compensate for fees to make results round
        // nice.
        auto const starting_xrp = XRP(100) +
            env.current()->fees().accountReserve(2) +
            env.current()->fees().base * 3;

        env.fund(starting_xrp, gw, alice);
        env.fund(XRP(2'000), bob);
        env.close();

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = MPTDEXFlags});

        // Created only to increase one reserve count for alice
        MPTTester ETH(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .flags = MPTDEXFlags});

        env(pay(gw, bob, BTC(1'200'000'000'000'000)));

        AMM ammBob(env, bob, XRP(1'000), BTC(1'200'000'000'000'000));
        // Alice has 400 - (2 reserve of 50 = 300 reserve) = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(alice, BTC(200'000'000'000'000), XRP(200)));

        // The pool gets only 100XRP for ~109.09e12BTC, even though
        // it can exchange more.
        BEAST_EXPECT(ammBob.expectBalances(
            XRP(1'100), BTC(1'090'909'090'909'091), ammBob.tokens()));

        env.require(balance(alice, BTC(109'090'909'090'909)));
        env.require(balance(alice, XRP(300)));
    }

    void
    testOfferCreateThenCross(FeatureBitset features)
    {
        testcase("Offer Create, then Cross");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, XRP(200'000));

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .transferFee = 500,
             .flags = MPTDEXFlags});

        env(pay(gw, bob, BTC(1'000'000'000'000)));
        env(pay(gw, alice, BTC(200'000'000'000'000)));

        AMM ammAlice(env, alice, BTC(150'000'000'000'000), XRP(150'100));
        env(offer(bob, XRP(100), BTC(100'000'000'000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            BTC(150'100'000'000'000), XRP(150'000), ammAlice.tokens()));

        // Bob pays 0.005 transfer fee.
        env.require(balance(bob, BTC(899'500'000'000)));
    }

    void
    testSellFlagBasic(FeatureBitset features)
    {
        testcase("Offer tfSell: Basic Sell");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw, bob, carol);
        env.fund(XRP(39'900), alice);

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .pay = 30'000,
             .flags = MPTDEXFlags});
        env(pay(gw, alice, BTC(10'100)));

        AMM ammAlice(env, alice, XRP(9'900), BTC(10'100));

        env(offer(carol, BTC(100), XRP(100)), json(jss::Flags, tfSell));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), BTC(9'999), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, carol, 0));
        env.require(balance(carol, BTC(30'101)));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, carol, XRP(30'000) - XRP(100) - 2 * txfee(env, 1)));
    }

    void
    testSellFlagExceedLimit(FeatureBitset features)
    {
        testcase("Offer tfSell: 2x Sell Exceed Limit");

        using namespace jtx;

        Env env{*this, features};

        auto const starting_xrp =
            XRP(100) + reserve(env, 1) + env.current()->fees().base * 2;

        env.fund(starting_xrp, gw, alice);
        env.fund(XRP(2'000), bob);
        env.close();

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = MPTDEXFlags});
        env(pay(gw, bob, BTC(2'200'000'000)));

        AMM ammBob(env, bob, XRP(1'000), BTC(2'200'000'000));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100'000'000 BTC for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more BTC than asked for.
        env(offer(alice, BTC(100'000'000), XRP(200)), json(jss::Flags, tfSell));
        BEAST_EXPECT(ammBob.expectBalances(
            XRP(1'100), BTC(2'000'000'000), ammBob.tokens()));
        env.require(balance(alice, BTC(200'000'000)));
        BEAST_EXPECT(expectLedgerEntryRoot(env, alice, XRP(250)));
        BEAST_EXPECT(expectOffers(env, alice, 0));
    }

    void
    testGatewayCrossCurrency(FeatureBitset features)
    {
        testcase("Client Issue: Gateway Cross Currency");

        using namespace jtx;

        Env env{*this, features};

        auto const starting_xrp =
            XRP(100.1) + reserve(env, 1) + env.current()->fees().base * 2;
        env.fund(starting_xrp, gw, alice, bob);

        MPTTester XTS(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .pay = 1'000'000'000'000'000,
             .flags = MPTDEXFlags});
        MPTTester XXX(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .pay = 1'000'000'000'000'000,
             .flags = MPTDEXFlags});

        AMM ammAlice(
            env, alice, XTS(1'000'000'000'000'000), XXX(1'000'000'000'000'000));

        Json::Value payment;
        payment[jss::secret] = toBase58(generateSeed("bob"));
        payment[jss::id] = env.seq(bob);
        payment[jss::build_path] = true;
        payment[jss::tx_json] = pay(bob, bob, XXX(10'000'000'000'000));
        payment[jss::tx_json][jss::Sequence] =
            env.current()
                ->read(keylet::account(bob.id()))
                ->getFieldU32(sfSequence);
        payment[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
        payment[jss::tx_json][jss::SendMax] =
            XTS(15'000'000'000'000).value().getJson(JsonOptions::none);
        payment[jss::tx_json][jss::Flags] = tfPartialPayment;
        auto const jrr = env.rpc("json", "submit", to_string(payment));
        BEAST_EXPECT(jrr[jss::result][jss::status] == "success");
        BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");

        BEAST_EXPECT(ammAlice.expectBalances(
            XTS(1'010'101'010'101'011),
            XXX(990'000'000'000'000),
            ammAlice.tokens()));
        env.require(balance(bob, XTS(989'898'989'898'989)));
        env.require(balance(bob, XXX(1'010'000'000'000'000)));
    }

    void
    testBridgedCross(FeatureBitset features)
    {
        testcase("Bridged Crossing");

        using namespace jtx;

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 15'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 15'000'000'000,
                 .flags = MPTDEXFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP AMM is created.
            //   o carol has ETH but wants BTC.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'100'000'000));
            AMM ammBob(env, bob, ETH(10'000'000'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, BTC(100'000'000), ETH(100'000'000)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), BTC(10'000'000'000), ammAlice.tokens()));
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(10'000), ETH(10'100'000'000), ammBob.tokens()));
            env.require(balance(carol, BTC(15'100'000'000)));
            env.require(balance(carol, ETH(14'900'000'000)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 15'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 15'000'000'000,
                 .flags = MPTDEXFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP offer is created.
            //   o carol has ETH but wants BTC.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and bob's offer are created, then autobridging
            // will not occur.
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'100'000'000));
            env(offer(bob, ETH(100'000'000), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, BTC(100'000'000), ETH(100'000'000)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), BTC(10'000'000'000), ammAlice.tokens()));
            env.require(balance(carol, BTC(15'100'000'000)));
            env.require(balance(carol, ETH(14'900'000'000)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 15'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 15'000'000'000,
                 .flags = MPTDEXFlags});

            // The scenario:
            //   o BTC/XRP offer is created.
            //   o ETH/XRP AMM is created.
            //   o carol has ETH but wants BTC.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and alice's offer are created, then
            // autobridging will not occur.
            env(offer(alice, XRP(100), BTC(100'000'000)));
            env.close();
            AMM ammBob(env, bob, ETH(10'000'000'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, BTC(100'000'000), ETH(100'000'000)));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(10'000), ETH(10'100'000'000), ammBob.tokens()));
            env.require(balance(carol, BTC(15'100'000'000)));
            env.require(balance(carol, ETH(14'900'000'000)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
    }

    void
    testSellWithFillOrKill(FeatureBitset features)
    {
        // Test a number of different corner cases regarding offer crossing
        // when both the tfSell flag and tfFillOrKill flags are set.
        testcase("Combine tfSell with tfFillOrKill");

        using namespace jtx;

        // Code returned if an offer is killed.
        TER const killedCode{
            features[fix1578] ? TER{tecKILLED} : TER{tesSUCCESS}};

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 20'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammBob(env, bob, XRP(20'000), BTC(200'000'000));
            // alice submits a tfSell | tfFillOrKill offer that does not cross.
            env(offer(alice, BTC(2'100'000), XRP(210), tfSell | tfFillOrKill),
                ter(killedCode));

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(20'000), BTC(200'000'000), ammBob.tokens()));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }
        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammBob(env, bob, XRP(20'000), BTC(200'000'000'000'000));
            // alice submits a tfSell | tfFillOrKill offer that crosses.
            // Even though tfSell is present it doesn't matter this time.
            env(offer(
                alice,
                BTC(2'000'000'000'000),
                XRP(220),
                tfSell | tfFillOrKill));
            env.close();
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(20'220), BTC(197'823'936'696'341), ammBob.tokens()));
            env.require(balance(alice, BTC(1'002'176'063'303'659)));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses and
            // returns more than was asked for (because of the tfSell flag).
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammBob(env, bob, XRP(20'000), BTC(200'000'000'000'000));

            env(offer(
                alice,
                BTC(10'000'000'000'000),
                XRP(1'500),
                tfSell | tfFillOrKill));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(21'500), BTC(186'046'511'627'907), ammBob.tokens()));
            env.require(balance(alice, BTC(1'013'953'488'372'093)));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that doesn't cross.
            // This would have succeeded with a regular tfSell, but the
            // fillOrKill prevents the transaction from crossing since not
            // all of the offer is consumed because AMM generated offer,
            // which matches alice's offer quality is ~ 10XRP/0.01996e3BTC.
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 10'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammBob(env, bob, XRP(5000), BTC(10'000'000));

            env(offer(alice, BTC(1'000'000), XRP(501), tfSell | tfFillOrKill),
                ter(tecKILLED));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }
    }

    void
    testTransferRateOffer(FeatureBitset features)
    {
        testcase("Transfer Rate Offer");

        using namespace jtx;

        // AMM XRP/BTC. Alice places BTC/XRP offer.
        {
            Env env(*this, features);
            env.fund(XRP(30'000), gw, bob, carol);
            env.fund(XRP(40'000), alice);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 30'000'000,
                 .flags = MPTDEXFlags});
            env(pay(gw, alice, BTC(10'100'000)));

            AMM ammAlice(env, alice, XRP(10'000), BTC(10'100'000));
            env.close();

            env(offer(carol, BTC(100'000), XRP(100)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), BTC(10'000'000), ammAlice.tokens()));
            env.require(balance(carol, BTC(30'100'000)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        {
            Env env(*this, features);
            env.fund(XRP(30'000), gw, bob, carol);
            env.fund(XRP(40'100), alice);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 30'000'000,
                 .flags = MPTDEXFlags});
            env(pay(gw, alice, BTC(10'000'000)));

            AMM ammAlice(env, alice, XRP(10'100), BTC(10'000'000));
            env.close();

            env(offer(carol, XRP(100), BTC(100'000)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'000), BTC(10'100'000), ammAlice.tokens()));
            // Carol pays 25% transfer fee
            env.require(balance(carol, BTC(29'875'000)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        {
            // Bridged crossing.
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = MPTDEXFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP Offer is created.
            //   o carol has ETH but wants BTC.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'100'000));
            env(offer(bob, ETH(100'000), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol, BTC(100'000), ETH(100'000)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), BTC(10'000'000), ammAlice.tokens()));
            env.require(balance(carol, BTC(15'100'000)));
            // Carol pays 25% transfer fee.
            env.require(balance(carol, ETH(14'875'000)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = MPTDEXFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP Offer is created.
            //   o carol has ETH but wants BTC.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'050'000));
            env(offer(bob, ETH(100'000), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // partially consumes Bob's offer.
            env(offer(carol, BTC(50'000), ETH(50'000)));
            env.close();
            // This test verifies that the amount removed from an offer
            // accounts for the transfer fee that is removed from the
            // account but not from the remaining offer.

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'050), BTC(10'000'000), ammAlice.tokens()));
            env.require(balance(carol, BTC(15'050'000)));
            // Carol pays 25% transfer fee.
            env.require(balance(carol, ETH(14'937'500)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(
                expectOffers(env, bob, 1, {{Amounts{ETH(50'000), XRP(50)}}}));
        }
    }

    void
    testSelfIssueOffer(FeatureBitset features)
    {
        // This test is not the same as corresponding testSelfIssueOffer()
        // in the Offer_test. It simply tests AMM with self issue and
        // offer crossing.
        using namespace jtx;

        Env env{*this, features};

        auto const f = env.current()->fees().base;

        env.fund(XRP(30'000) + f, alice, bob);
        env.close();

        MPTTester BTC(
            {.env = env,
             .issuer = bob,
             .holders = {alice},
             .flags = MPTDEXFlags});

        AMM ammBob(env, bob, XRP(10'000), BTC(10'100));

        env(offer(alice, BTC(100), XRP(100)));
        env.close();

        BEAST_EXPECT(
            ammBob.expectBalances(XRP(10'100), BTC(10'000), ammBob.tokens()));
        BEAST_EXPECT(expectOffers(env, alice, 0));
        env.require(balance(alice, BTC(100)));
    }

    void
    testDirectToDirectPath(FeatureBitset features)
    {
        // The offer crossing code expects that a DirectStep is always
        // preceded by a BookStep.  In one instance the default path
        // was not matching that assumption.  Here we recreate that case
        // so we can prove the bug stays fixed.
        testcase("Direct to Direct path");

        using namespace jtx;

        Env env{*this, features};

        auto const ann = Account("ann");
        auto const bob = Account("bob");
        auto const cam = Account("cam");
        auto const carol = Account("carol");

        auto const fee = env.current()->fees().base;
        env.fund(XRP(1'000), carol);
        env.fund(reserve(env, 4) + (fee * 5), ann, bob, cam);
        env.close();

        MPTTester A_BUX(
            {.env = env,
             .issuer = ann,
             .holders = {bob, cam, carol},
             .flags = MPTDEXFlags});

        MPTTester B_BUX(
            {.env = env,
             .issuer = bob,
             .holders = {ann, cam, carol},
             .flags = MPTDEXFlags});

        env(pay(ann, cam, A_BUX(350'000'000'000'000)));
        env(pay(bob, cam, B_BUX(350'000'000'000'000)));
        env(pay(bob, carol, B_BUX(4'000'000'000'000'000)));
        env(pay(ann, carol, A_BUX(4'000'000'000'000'000)));

        AMM ammCarol(
            env,
            carol,
            A_BUX(3'000'000'000'000'000),
            B_BUX(3'300'000'000'000'000));

        // cam puts an offer on the books that her upcoming offer could cross.
        // But this offer should be deleted, not crossed, by her upcoming
        // offer.
        env(offer(
            cam,
            A_BUX(290'000'000'000'000),
            B_BUX(300'000'000'000'000),
            tfPassive));
        env.close();
        env.require(balance(cam, A_BUX(350'000'000'000'000)));
        env.require(balance(cam, B_BUX(350'000'000'000'000)));
        env.require(offers(cam, 1));

        // This offer caused the assert.
        env(offer(cam, B_BUX(300'000'000'000'000), A_BUX(300'000'000'000'000)));

        // AMM is consumed up to the first cam Offer quality
        BEAST_EXPECT(ammCarol.expectBalances(
            A_BUX(3'093'541'659'651'604),
            B_BUX(3'200'215'509'984'418),
            ammCarol.tokens()));
        BEAST_EXPECT(expectOffers(
            env,
            cam,
            1,
            {{Amounts{
                B_BUX(200'215'509'984'418), A_BUX(200'215'509'984'418)}}}));
    }

    void
    testRequireAuth(FeatureBitset features)
    {
        testcase("RequireAuth");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(400'000), gw, alice, bob);

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = tfMPTRequireAuth | MPTDEXFlags});

        // Authorize bob and alice
        BTC.authorize({.holder = alice});
        BTC.authorize({.holder = bob});

        env(pay(gw, alice, BTC(1'000)));
        env.close();

        // Alice is able to create AMM since the GW has authorized her
        AMM ammAlice(env, alice, BTC(1'000), XRP(1'050));

        env(pay(gw, bob, BTC(50)));
        env.close();

        env.require(balance(bob, BTC(50)));

        // Bob's offer should cross Alice's AMM
        env(offer(bob, XRP(50), BTC(50)));
        env.close();

        BEAST_EXPECT(
            ammAlice.expectBalances(BTC(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob, 0));
        env.require(balance(bob, BTC(0)));
    }

    void
    testMissingAuth(FeatureBitset features)
    {
        testcase("Missing Auth");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(400'000), gw, alice, bob);
        env.close();

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .flags = tfMPTRequireAuth | MPTDEXFlags});

        // Alice doesn't have the funds
        {
            AMM ammAlice(env, alice, BTC(1'000), XRP(1'000), ter(tecNO_AUTH));
        }

        BTC.authorize({.holder = bob});
        env(pay(gw, bob, BTC(50)));
        env.close();
        env.require(balance(bob, BTC(50)));

        // Alice should not be able to create AMM without authorization.
        {
            AMM ammAlice(env, alice, BTC(1'000), XRP(1'000), ter(tecNO_AUTH));
        }

        // Finally, authorize alice. Now alice's AMM create should succeed.
        BTC.authorize({.holder = alice});
        env(pay(gw, alice, BTC(1'000)));
        env.close();

        AMM ammAlice(env, alice, BTC(1'000), XRP(1'050));

        // Authorize AMM.
        // BTC.authorize({.account = ammAlice.ammAccount()});
        // env.close();

        // Now bob creates his offer again, which crosses with alice's AMM.
        env(offer(bob, XRP(50), BTC(50)));
        env.close();

        BEAST_EXPECT(
            ammAlice.expectBalances(BTC(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob, 0));
        env.require(balance(bob, BTC(0)));
    }

    void
    testOffers()
    {
        using namespace jtx;
        FeatureBitset const all{testable_amendments()};
        testRmFundedOffer(all);
        testFillModes(all);
        testOfferCrossWithXRP(all);
        testOfferCrossWithLimitOverride(all);
        testCurrencyConversionEntire(all);
        testCurrencyConversionInParts(all);
        testCrossCurrencyStartXRP(all);
        testCrossCurrencyEndXRP(all);
        testCrossCurrencyBridged(all);
        testOfferFeesConsumeFunds(all);
        testOfferCreateThenCross(all);
        testSellFlagExceedLimit(all);
        testGatewayCrossCurrency(all);
        testBridgedCross(all);
        testSellWithFillOrKill(all);
        testTransferRateOffer(all);
        testSelfIssueOffer(all);
        testSellFlagBasic(all);
        testDirectToDirectPath(all);
        testRequireAuth(all);
        testMissingAuth(all);
    }

    void
    path_find_consume_all()
    {
        testcase("path find consume all");
        using namespace jtx;

        Env env = pathTestEnv();
        env.fund(XRP(100'000'260), alice);
        env.fund(XRP(30'000), gw, bob, carol);

        MPTTester ETH(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .pay = 100'000'000'000'000,
             .flags = MPTDEXFlags});

        AMM ammCarol(env, carol, XRP(100), ETH(100'000'000'000'000));

        STPathSet st;
        STAmount sa;
        STAmount da;
        std::tie(st, sa, da) = find_paths(
            env,
            alice,
            bob,
            bob["AUD"](-1),
            std::optional<STAmount>(XRP(100'000'000)));
        BEAST_EXPECT(st.empty());
        std::tie(st, sa, da) = find_paths(
            env,
            alice,
            bob,
            ETH(-1),
            std::optional<STAmount>(XRP(100'000'000)));
        // Alice sends all requested 100,000,000XRP
        BEAST_EXPECT(sa == XRP(100'000'000));
        // Bob gets ~99.99e12ETH. This is the amount Bob
        // can get out of AMM for 100,000,000XRP.
        BEAST_EXPECT(equal(da, ETH(99'999'900'000'100)));
    }

    // carol holds ETH, sells ETH for XRP
    // bob will hold ETH
    // alice pays bob ETH using XRP
    void
    via_offers_via_gateway()
    {
        testcase("via gateway");
        using namespace jtx;

        Env env = pathTestEnv();
        env.fund(XRP(10'000), alice, bob, carol, gw);
        env.close();

        MPTTester ETH(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .transferFee = 10'000,
             .flags = MPTDEXFlags});

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .transferFee = 10'000,
             .flags = MPTDEXFlags});

        env(pay(gw, carol, ETH(51)));
        env.close();
        AMM ammCarol(env, carol, XRP(40), ETH(51));
        env(pay(alice, bob, ETH(10)), sendmax(XRP(100)), paths(XRP));
        env.close();
        // AMM offer is 51.282052XRP/11ETH, 11ETH/1.1 = 10ETH to bob
        BEAST_EXPECT(
            ammCarol.expectBalances(XRP(51), ETH(40), ammCarol.tokens()));
        env.require(balance(bob, ETH(10)));

        auto const result = find_paths(env, alice, bob, BTC(25));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    receive_max()
    {
        testcase("Receive max");
        using namespace jtx;
        auto const charlie = Account("charlie");
        {
            // XRP -> MPT receive max
            Env env = pathTestEnv();
            env.fund(XRP(30'000), alice, bob, charlie, gw);

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, charlie},
                 .pay = 11'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM ammCharlie(env, charlie, XRP(10), ETH(11'000'000'000'000));
            auto [st, sa, da] =
                find_paths(env, alice, bob, ETH(-1), XRP(1).value());
            BEAST_EXPECT(sa == XRP(1));
            BEAST_EXPECT(equal(da, ETH(1'000'000'000'000)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getMPTID() == ETH.issuanceID());
            }
        }
        {
            // MPT -> XRP receive max
            Env env = pathTestEnv();
            env.fund(XRP(30'000), alice, bob, charlie, gw);

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, charlie},
                 .pay = 11'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM ammCharlie(env, charlie, XRP(11), ETH(10'000'000'000'000));
            env.close();
            auto [st, sa, da] = find_paths(
                env, alice, bob, drops(-1), ETH(1'000'000'000'000).value());
            BEAST_EXPECT(sa == ETH(1'000'000'000'000));
            BEAST_EXPECT(equal(da, XRP(1)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() &&
                    pathElem.getIssuerID() == xrpAccount() &&
                    pathElem.getCurrency() == xrpCurrency());
            }
        }
    }

    void
    path_find_01()
    {
        testcase("Path Find: XRP -> XRP and XRP -> MPT");
        using namespace jtx;
        Env env = pathTestEnv();
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account G1{"G1"};
        Account G2{"G2"};
        Account G3{"G3"};
        Account M1{"M1"};

        env.fund(XRP(100'000), A1);
        env.fund(XRP(10'000), A2);
        env.fund(XRP(1'000), A3, G1, G2, G3);
        env.fund(XRP(20'000), M1);
        env.close();

        MPTTester XYZ_G1(
            {.env = env,
             .issuer = G1,
             .holders = {A1, M1, A2},
             .flags = MPTDEXFlags});

        MPTTester XYZ_G2(
            {.env = env,
             .issuer = G2,
             .holders = {A2, M1, A1},
             .flags = MPTDEXFlags});

        MPTTester ABC_G3(
            {.env = env,
             .issuer = G3,
             .holders = {A1, A2, M1, A3},
             .flags = MPTDEXFlags});

        MPTTester ABC_A2(
            {.env = env,
             .issuer = A2,
             .holders = {G3, A1},
             .flags = MPTDEXFlags});

        env(pay(G1, A1, XYZ_G1(3'500'000'000)));
        env(pay(G3, A1, ABC_G3(1'200'000'000)));
        env(pay(G1, M1, XYZ_G1(25'000'000'000)));
        env(pay(G2, M1, XYZ_G2(25'000'000'000)));
        env(pay(G3, M1, ABC_G3(25'000'000'000)));
        env(pay(A2, G3, ABC_A2(101'000'000)));
        env.close();

        AMM ammM1_XYZ_G1_XYZ_G2(
            env, M1, XYZ_G1(1'000'000'000), XYZ_G2(1'000'000'000));
        AMM ammM1_XRP_ABC_G3(env, M1, XRP(10'000), ABC_G3(1'000'000'000));
        AMM ammG3_ABC_G3_ABC_A2(
            env, G3, ABC_G3(100'000'000), ABC_A2(101'000'000));
        env.close();

        STPathSet st;
        STAmount sa, da;

        {
            auto const& send_amt = XRP(10);
            std::tie(st, sa, da) =
                find_paths(env, A1, A2, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(st.empty());
        }

        {
            // no path should exist for this since dest account
            // does not exist.
            auto const& send_amt = XRP(200);
            std::tie(st, sa, da) = find_paths(
                env, A1, Account{"A0"}, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(st.empty());
        }

        {
            auto const& send_amt = ABC_G3(10'000'000);
            std::tie(st, sa, da) =
                find_paths(env, A2, G3, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{101'010'102}));
            BEAST_EXPECT(same(st, stpath(IPE(MPT(ABC_G3)))));
        }

        {
            auto const& send_amt = ABC_A2(1'000'000);
            std::tie(st, sa, da) =
                find_paths(env, A1, A2, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{10'010'011}));
            BEAST_EXPECT(same(st, stpath(IPE(MPT(ABC_G3)), IPE(MPT(ABC_A2)))));
        }
    }

    void
    path_find_02()
    {
        testcase("Path Find: non-XRP -> XRP");
        using namespace jtx;
        Env env = pathTestEnv();
        Account A1{"A1"};
        Account A2{"A2"};
        Account G3{"G3"};
        Account M1{"M1"};

        env.fund(XRP(1'000), A1, A2, G3);
        env.fund(XRP(11'000), M1);
        env.close();

        MPTTester ETH(
            {.env = env,
             .issuer = G3,
             .holders = {A1, A2, M1},
             .pay = 1'000'000'000,
             .flags = MPTDEXFlags});

        AMM ammM1(env, M1, ETH(1'000'000'000), XRP(10'010));

        STPathSet st;
        STAmount sa, da;

        auto const& send_amt = XRP(10);

        std::tie(st, sa, da) = find_paths_by_element(
            env, A1, A2, send_amt, std::nullopt, IPE(MPT(ETH)));
        BEAST_EXPECT(equal(da, send_amt));
        BEAST_EXPECT(equal(sa, ETH(1'000'000)));
        BEAST_EXPECT(same(st, stpath(IPE(xrpIssue()))));
    }

    void
    path_find_06()
    {
        testcase("Path Find: non-XRP -> non-XRP, same issuanceID");
        using namespace jtx;
        {
            Env env = pathTestEnv();
            Account A1{"A1"};
            Account A2{"A2"};
            Account A3{"A3"};
            Account G1{"G1"};
            Account G2{"G2"};
            Account M1{"M1"};

            env.fund(XRP(11'000), M1);
            env.fund(XRP(1'000), A1, A2, A3, G1, G2);
            env.close();

            MPTTester HKD_G1(
                {.env = env,
                 .issuer = G1,
                 .holders = {A1, M1},
                 .pay = 5'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester HKD_G2(
                {.env = env,
                 .issuer = G2,
                 .holders = {A2, M1},
                 .pay = 5'000'000'000,
                 .flags = MPTDEXFlags});

            AMM ammM1(env, M1, HKD_G1(1'000'000'000), HKD_G2(1'010'000'000));

            auto const& send_amt = HKD_G2(10'000'000);
            STPathSet st;
            STAmount sa, da;
            std::tie(st, sa, da) = jtx::find_paths(
                env,
                G1,
                A2,
                send_amt,
                std::nullopt,
                HKD_G1.issuanceID(),
                std::nullopt,
                std::nullopt);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, HKD_G1(10'000'000)));
            BEAST_EXPECT(same(st, stpath(IPE(MPT(HKD_G2)))));
        }
    }

    void
    testFalseDry(FeatureBitset features)
    {
        testcase("falseDryChanges");

        using namespace jtx;

        Env env(*this, features);
        env.memoize(bob);

        env.fund(XRP(10'000), alice, gw);
        fund(env, gw, {carol}, XRP(10'000), {}, Fund::Acct);
        auto const AMMXRPPool = env.current()->fees().increment * 2;
        env.fund(reserve(env, 5) + ammCrtFee(env) + AMMXRPPool, bob);
        env.close();

        MPTTester ETH(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .flags = MPTDEXFlags});

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .flags = MPTDEXFlags});

        env(pay(gw, alice, ETH(50'000)));
        env(pay(gw, bob, BTC(150'000)));

        // Bob has _just_ slightly less than 50 xrp available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incoming xrp to the XRP/BTC offer will require two
        // recursive calls to the ETH/XRP offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry.
        // This is the second error case to test (when flowV1 is used).
        env(offer(bob, ETH(50'000), XRP(50)));
        AMM ammBob(env, bob, AMMXRPPool, BTC(150'000));

        env(pay(alice, carol, BTC(1'000'000'000)),
            path(~XRP, ~MPT(BTC)),
            sendmax(ETH(500'000)),
            txflags(tfNoRippleDirect | tfPartialPayment));

        auto const carolBTC = env.balance(carol, MPT(BTC));
        BEAST_EXPECT(carolBTC > BTC(0) && carolBTC < BTC(50'000));
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("Book Step");

        using namespace jtx;

        // simple MPT/IOU mix offer
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, carol, gw);
                env.close();
                auto const ETH = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000'000});
                env(pay(gw, alice, BTC(500000)));
                env(pay(gw, bob, BTC(500000)));
                env(pay(gw, carol, BTC(500000)));
                env(pay(gw, alice, ETH(500000)));
                env(pay(gw, bob, ETH(500000)));
                env(pay(gw, carol, ETH(500000)));
                env.close();
                AMM ammBob(env, bob, BTC(100'000), ETH(150'000));

                env(pay(alice, carol, ETH(50'000)),
                    path(~ETH),
                    sendmax(BTC(50'000)));

                env.require(balance(alice, BTC(450'000)));
                env.require(balance(bob, BTC(400'000)));
                env.require(balance(bob, ETH(350'000)));
                env.require(balance(carol, ETH(550'000)));
                BEAST_EXPECT(ammBob.expectBalances(
                    BTC(150'000), ETH(100'000), ammBob.tokens()));
            };
            testHelper2TokensMix(test);
        }

        {
            // simple MPT/XRP XRP/MPT offer
            Env env(*this, features);
            env.fund(XRP(10'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 100'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 150'000,
                 .flags = MPTDEXFlags});

            AMM ammBobBTC_XRP(env, bob, BTC(100'000), XRP(150));
            AMM ammBobXRP_ETH(env, bob, XRP(100), ETH(150'000));

            env(pay(alice, carol, ETH(50'000)),
                path(~XRP, ~MPT(ETH)),
                sendmax(BTC(50'000)));

            env.require(balance(alice, BTC(50'000)));
            env.require(balance(bob, BTC(0)));
            env.require(balance(bob, ETH(0)));
            env.require(balance(carol, ETH(200'000)));
            BEAST_EXPECT(ammBobBTC_XRP.expectBalances(
                BTC(150'000), XRP(100), ammBobBTC_XRP.tokens()));
            BEAST_EXPECT(ammBobXRP_ETH.expectBalances(
                XRP(150), ETH(100'000), ammBobXRP_ETH.tokens()));
        }
        {
            // simple XRP -> MPT through offer and sendmax
            Env env(*this, features);
            XRPAmount const baseFee{env.current()->fees().base};
            env.fund(XRP(10'000), gw, alice, bob, carol);

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 150'000,
                 .flags = MPTDEXFlags});

            AMM ammBob(env, bob, XRP(100), ETH(150'000));

            env(pay(alice, carol, ETH(50'000)),
                path(~MPT(ETH)),
                sendmax(XRP(50)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, XRP(10'000) - XRP(50) - 2 * baseFee));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(10'000) - XRP(100) - ammCrtFee(env) - baseFee));
            env.require(balance(bob, ETH(0)));
            env.require(balance(carol, ETH(200'000)));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(150), ETH(100'000), ammBob.tokens()));
        }
        {
            // simple MPT -> XRP through offer and sendmax
            Env env(*this, features);
            XRPAmount const baseFee{env.current()->fees().base};
            env.fund(XRP(10'000), gw, alice, bob, carol);

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 100'000,
                 .flags = MPTDEXFlags});

            AMM ammBob(env, bob, ETH(100'000), XRP(150));

            env(pay(alice, carol, XRP(50)), path(~XRP), sendmax(ETH(50'000)));

            env.require(balance(alice, ETH(50'000)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(10'000) - XRP(150) - ammCrtFee(env) - baseFee));
            env.require(balance(bob, ETH(0)));
            BEAST_EXPECT(
                expectLedgerEntryRoot(env, carol, XRP(10'000 + 50) - baseFee));
            BEAST_EXPECT(
                ammBob.expectBalances(ETH(150'000), XRP(100), ammBob.tokens()));
        }

        // test unfunded offers are removed when payment succeeds
        {
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);
                env.fund(XRP(10'000), alice, bob, carol, gw);
                env.close();
                auto const BTC = issue1(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1000'000000});
                auto const ETH = issue2(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1000'000000});
                auto const GBP = issue3(
                    {.env = env,
                     .token = "GBP",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1000'000000});

                env(pay(gw, alice, BTC(60'000)));
                env(pay(gw, bob, ETH(200'000)));
                env(pay(gw, bob, GBP(150'000)));
                env(offer(bob, BTC(50'000), ETH(50'000)));
                env(offer(bob, BTC(40'000), GBP(50'000)));
                env.close();
                AMM ammBob(env, bob, GBP(100'000), ETH(150'000));

                // unfund offer
                env(pay(bob, gw, GBP(50'000)));
                BEAST_EXPECT(isOffer(env, bob, BTC(50'000), ETH(50'000)));
                BEAST_EXPECT(isOffer(env, bob, BTC(40'000), GBP(50'000)));
                env(pay(alice, carol, ETH(50'000)),
                    path(~ETH),
                    path(~GBP, ~ETH),
                    sendmax(BTC(60'000)));
                env.require(balance(alice, BTC(10'000)));
                env.require(balance(bob, BTC(50'000)));
                env.require(balance(bob, ETH(0)));
                env.require(balance(bob, GBP(0)));
                env.require(balance(carol, ETH(50'000)));
                // used in the payment
                BEAST_EXPECT(!isOffer(env, bob, BTC(50'000), ETH(50'000)));
                // found unfunded
                BEAST_EXPECT(!isOffer(env, bob, BTC(40'000), GBP(50'000)));
                // unchanged
                BEAST_EXPECT(ammBob.expectBalances(
                    GBP(100'000), ETH(150'000), ammBob.tokens()));
            };
            testHelper3TokensMix(test);
        }

        {
            // test unfunded offers are removed when the payment fails.
            // bob makes two offers: a funded 50'000'000 ETH for 50'000'000 BTC
            // and an unfunded 50'000'000 GBP for 60'000'000 BTC. alice pays
            // carol 61'000'000 ETH with 61'000'000 BTC. alice only has
            // 60'000'000 BTC, so the payment will fail. The payment uses two
            // paths: one through bob's funded offer and one through his
            // unfunded offer. When the payment fails `flow` should return the
            // unfunded offer. This test is intentionally similar to the one
            // that removes unfunded offers when the payment succeeds.
            Env env(*this, features);

            env.fund(XRP(10'000), bob, carol, gw);
            env.close();
            // Sets rippling on, this is different from
            // the original test
            fund(env, gw, {alice}, XRP(10'000), {}, Fund::Acct);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .flags = MPTDEXFlags});

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .flags = MPTDEXFlags});

            env(pay(gw, alice, BTC(60'000'000)));
            env(pay(gw, bob, BTC(100'000'000)));
            env(pay(gw, bob, ETH(100'000'000)));
            env(pay(gw, bob, GBP(50'000'000)));
            env(pay(gw, carol, GBP(1'000'000)));
            env.close();

            // This is multiplath, which generates limited # of offers
            AMM ammBobBTC_ETH(env, bob, BTC(50'000'000), ETH(50'000'000));
            env(offer(bob, BTC(60'000'000), GBP(50'000'000)));
            env(offer(carol, BTC(1'000'000'000), GBP(1'000'000)));
            env(offer(bob, GBP(50'000'000), ETH(50'000'000)));

            // unfund offer
            env(pay(bob, gw, GBP(50'000'000)));
            BEAST_EXPECT(ammBobBTC_ETH.expectBalances(
                BTC(50'000'000), ETH(50'000'000), ammBobBTC_ETH.tokens()));
            BEAST_EXPECT(isOffer(env, bob, BTC(60'000'000), GBP(50'000'000)));
            BEAST_EXPECT(
                isOffer(env, carol, BTC(1'000'000'000), GBP(1'000'000)));
            BEAST_EXPECT(isOffer(env, bob, GBP(50'000'000), ETH(50'000'000)));

            auto flowJournal = env.app().logs().journal("Flow");
            auto const flowResult = [&] {
                STAmount deliver(ETH(51'000'000));
                STAmount smax(BTC(61'000'000));
                PaymentSandbox sb(env.current().get(), tapNONE);
                STPathSet paths;
                auto IPE = [](MPTTester const& iss) {
                    return STPathElement(
                        STPathElement::typeMPT | STPathElement::typeIssuer,
                        xrpAccount(),
                        PathAsset{iss.issuanceID()},
                        iss.issuer());
                };
                {
                    // BTC -> ETH
                    STPath p1({IPE(ETH)});
                    paths.push_back(p1);
                    // BTC -> GBP -> ETH
                    STPath p2({IPE(GBP), IPE(ETH)});
                    paths.push_back(p2);
                }

                return flow(
                    sb,
                    deliver,
                    alice,
                    carol,
                    paths,
                    false,
                    false,
                    true,
                    OfferCrossing::no,
                    std::nullopt,
                    smax,
                    std::nullopt,
                    flowJournal);
            }();

            BEAST_EXPECT(flowResult.removableOffers.size() == 1);
            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    if (flowResult.removableOffers.empty())
                        return false;
                    Sandbox sb(&view, tapNONE);
                    for (auto const& o : flowResult.removableOffers)
                        if (auto ok = sb.peek(keylet::offer(o)))
                            offerDelete(sb, ok, flowJournal);
                    sb.apply(view);
                    return true;
                });

            // used in payment, but since payment failed should be untouched
            BEAST_EXPECT(ammBobBTC_ETH.expectBalances(
                BTC(50'000'000), ETH(50'000'000), ammBobBTC_ETH.tokens()));
            BEAST_EXPECT(
                isOffer(env, carol, BTC(1'000'000'000), GBP(1'000'000)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, BTC(60'000'000), GBP(50'000'000)));
        }
        {
            // Do not produce more in the forward pass than the reverse pass
            // This test uses a path that whose reverse pass will compute a
            // 500 ETH input required for a 1'000 BTC output. It sets a sendmax
            // of 400 ETH, so the payment engine will need to do a forward
            // pass. Without limits, the 400 ETH would produce 1'000 BTC in
            // the forward pass. This test checks that the payment produces
            // 1'000 BTC, as expected.

            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, carol, gw);
                env.close();
                auto const ETH = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 10'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 10'000'000});

                env(pay(gw, alice, ETH(1'000'000)));
                env(pay(gw, bob, BTC(1'000'000)));
                env(pay(gw, bob, ETH(1'000'000)));
                env.close();

                AMM ammBob(env, bob, ETH(8'000), XRPAmount{21});
                env(offer(bob, drops(1), BTC(1'000'000)), txflags(tfPassive));

                env(pay(alice, carol, BTC(1'000)),
                    path(~XRP, ~BTC),
                    sendmax(ETH(400)),
                    txflags(tfNoRippleDirect | tfPartialPayment));

                env.require(balance(carol, BTC(1'000)));
                BEAST_EXPECT(ammBob.expectBalances(
                    ETH(8400), XRPAmount{20}, ammBob.tokens()));
            };
            testHelper2TokensMix(test);
        }
    }

    void
    testTransferRateNoOwnerFee(FeatureBitset features)
    {
        testcase("No Owner Fee");
        using namespace jtx;

        {
            // payment via AMM
            Env env(*this, features);
            env.fund(XRP(1'000), gw, alice, bob, carol);

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM amm(
                env,
                bob,
                GBP(1'000'000'000'000'000),
                BTC(1'000'000'000'000'000));

            env(pay(alice, carol, BTC(100'000'000'000'000)),
                path(~MPT(BTC)),
                sendmax(GBP(150'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice buys 107.1428e12BTC with 120e12GBP and pays 25% tr fee on
            // 120e12GBP 1,000e12 - 120e12*1.25 = 850e12GBP
            env.require(balance(alice, GBP(850'000'000'000'000)));

            BEAST_EXPECT(amm.expectBalances(
                GBP(1'120'000'000'000'000),
                BTC(892'857'142'857'143),
                amm.tokens()));

            // 25% of 85.7142e12BTC is paid in tr fee
            // 85.7142e12*1.25 = 107.1428e12BTC
            env.require(balance(carol, BTC(1'085'714'285'714'285)));
        }
        {
            // Payment via offer and AMM
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw, alice, bob, carol, ed);

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            env(offer(
                    ed, GBP(1'000'000'000'000'000), ETH(1'000'000'000'000'000)),
                txflags(tfPassive));
            env.close();

            AMM amm(
                env,
                bob,
                ETH(1'000'000'000'000'000),
                BTC(1'000'000'000'000'000));

            env(pay(alice, carol, BTC(100'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(GBP(150'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice buys 120e12ETH with 120e12GBP via the offer
            // and pays 25% tr fee on 120e12GBP
            // 1,000e12 - 120e12*1.25 = 850e12GBP
            env.require(balance(alice, GBP(850'000'000'000'000)));
            // consumed offer is 120e12GBP/120e12ETH
            // ed doesn't pay tr fee
            env.require(balance(ed, ETH(880'000'000'000'000)));
            env.require(balance(ed, GBP(1'120'000'000'000'000)));
            BEAST_EXPECT(expectOffers(
                env,
                ed,
                1,
                {Amounts{GBP(880'000'000'000'000), ETH(880'000'000'000'000)}}));
            // 25% on 96e12ETH is paid in tr fee 96e12*1.25 = 120e12ETH
            // 96e12ETH is swapped in for 87.5912e12BTC
            BEAST_EXPECT(amm.expectBalances(
                ETH(1'096'000'000'000'000),
                BTC(912'408'759'124'088),
                amm.tokens()));
            // 25% on 70.0729e12BTC is paid in tr fee 70.0729e12*1.25
            // = 87.5912e12BTC
            env.require(balance(carol, BTC(1'070'072'992'700'729)));
        }
        {
            // Payment via AMM, AMM
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw, alice, bob, carol, ed);

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM amm1(
                env,
                bob,
                GBP(1'000'000'000'000'000),
                ETH(1'000'000'000'000'000));
            AMM amm2(
                env,
                ed,
                ETH(1'000'000'000'000'000),
                BTC(1'000'000'000'000'000));

            env(pay(alice, carol, BTC(100'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(GBP(150'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            env.require(balance(alice, GBP(850'000'000'000'000)));

            // alice buys 107.1428e12ETH with 120e12GBP and pays 25% tr fee on
            // 120e12GBP 1,000e12 - 120e12*1.25 = 850e12GBP 120e12GBP is swapped
            // in for 107.1428e12ETH
            BEAST_EXPECT(amm1.expectBalances(
                GBP(1'120'000'000'000'000),
                ETH(892'857'142'857'143),
                amm1.tokens()));
            // 25% on 85.7142e12ETH is paid in tr fee 85.7142e12*1.25 =
            // 107.1428e12ETH 85.7142e12ETH is swapped in for 78.9473e12BTC
            BEAST_EXPECT(amm2.expectBalances(
                ETH(1'085'714'285'714'285),
                BTC(921'052'631'578'948),
                amm2.tokens()));

            // 25% on 63.1578e12BTC is paid in tr fee 63.1578e12*1.25
            // = 78.9473e12BTC
            env.require(balance(carol, BTC(1'063'157'894'736'841)));
        }
        {
            // AMM offer crossing
            Env env(*this, features);

            env.fund(XRP(1'000), gw, alice, bob);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .transferFee = 25'000,
                 .pay = 1'100'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .transferFee = 25'000,
                 .pay = 1'100'000,
                 .flags = MPTDEXFlags});

            AMM amm(env, bob, BTC(1'000'000), ETH(1'100'000));
            env(offer(alice, ETH(100'000), BTC(100'000)));
            env.close();

            // 100e3BTC is swapped in for 100e3ETH
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'100'000), ETH(1'000'000), amm.tokens()));
            // alice pays 25% tr fee on 100e3BTC 1100e3-100e3*1.25 = 975e3BTC
            env.require(balance(alice, BTC(975'000)));
            env.require(balance(alice, ETH(1'200'000)));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
        {
            // Payment via AMM with limit quality
            Env env(*this, features);

            env.fund(XRP(1'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM amm(
                env,
                bob,
                GBP(1'000'000'000'000'000),
                BTC(1'000'000'000'000'000));

            // requested quality limit is 100e12BTC/178.58e12GBP = 0.55997
            // trade quality is 100e12BTC/178.5714 = 0.55999e12
            env(pay(alice, carol, BTC(100'000'000'000'000)),
                path(~MPT(BTC)),
                sendmax(GBP(178'580'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 125e12BTC with 142.8571e12GBP and pays 25% tr fee
            // on 142.8571e12GBP
            // 1,000e12 - 142.8571e12*1.25 = 821.4285e12GBP
            env.require(balance(alice, GBP(821'428'571'428'571)));
            // 142.8571e12GBP is swapped in for 125e12BTC
            BEAST_EXPECT(amm.expectBalances(
                GBP(1'142'857'142'857'143),
                BTC(875'000'000'000'000),
                amm.tokens()));
            // 25% on 100e12BTC is paid in tr fee
            // 100e12*1.25 = 125e12BTC
            env.require(balance(carol, BTC(1'100'000'000'000'000)));
        }
        {
            // Payment via AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);

            env.fund(XRP(1'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'200'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'200'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM amm(
                env,
                bob,
                GBP(1'000'000'000'000'000),
                BTC(1'200'000'000'000'000));

            // requested quality limit is 90e12BTC/120e12GBP = 0.75
            // trade quality is 22.5e12BTC/30e12GBP = 0.75
            env(pay(alice, carol, BTC(90'000'000'000'000)),
                path(~MPT(BTC)),
                sendmax(GBP(120'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 28.125e12BTC with 24e12GBP and pays 25% tr fee
            // on 24e12GBP
            // 1,200e12 - 24e12*1.25 =~ 1,170e12GBP
            env.require(balance(alice, GBP(1'170'000'000'000'000)));
            // 24e12GBP is swapped in for 28.125e12BTC
            BEAST_EXPECT(amm.expectBalances(
                GBP(1'024'000'000'000'000),
                BTC(1'171'875'000'000'000),
                amm.tokens()));

            // 25% on 22.5e12BTC is paid in tr fee
            // 22.5*1.25 = 28.125e12BTC
            env.require(balance(carol, BTC(1'222'500'000'000'000)));
        }
        {
            // Payment via offer and AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw, alice, bob, carol, ed);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            env(offer(
                    ed, GBP(1'000'000'000'000'000), ETH(1'000'000'000'000'000)),
                txflags(tfPassive));
            env.close();

            AMM amm(
                env,
                bob,
                ETH(1'000'000'000'000'000),
                BTC(1'400'000'000'000'000));

            // requested quality limit is 95e12BTC/140e12GBP = 0.6785
            // trade quality is 59.7321e12BTC/88.0262e12GBP = 0.6785
            env(pay(alice, carol, BTC(95'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(GBP(140'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 70.4210e12ETH with 70.4210e12GBP via the offer
            // and pays 25% tr fee on 70.4210e12GBP
            // 1,400e12 - 70.4210e12*1.25 = 1400e12 - 88.0262e12 =
            // 1311.9736e12GBP
            env.require(balance(alice, GBP(1'311'973'684'210'525)));
            // ed doesn't pay tr fee, the balances reflect consumed offer
            // 70.4210e12GBP/70.4210e12ETH
            env.require(balance(ed, ETH(1'329'578'947'368'420)));
            env.require(balance(ed, GBP(1'470'421'052'631'580)));
            BEAST_EXPECT(expectOffers(
                env,
                ed,
                1,
                {Amounts{GBP(929'578'947'368'420), ETH(929'578'947'368'420)}}));
            // 25% on 56.3368e12ETH is paid in tr fee 56.3368e12*1.25
            // = 70.4210e12ETH 56.3368e12ETH is swapped in for 74.6651e12BTC
            BEAST_EXPECT(amm.expectBalances(
                ETH(1'056'336'842'105'264),
                BTC(1'325'334'821'428'571),
                amm.tokens()));

            // 25% on 59.7321e12BTC is paid in tr fee 59.7321e12*1.25
            // = 74.6651e12BTC
            env.require(balance(carol, BTC(1'459'732'142'857'143)));
        }
        {
            // Payment via AMM and offer with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw, alice, bob, carol, ed);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM amm(
                env,
                bob,
                GBP(1'000'000'000'000'000),
                ETH(1'000'000'000'000'000));

            env(offer(
                    ed, ETH(1'000'000'000'000'000), BTC(1'400'000'000'000'000)),
                txflags(tfPassive));
            env.close();

            // requested quality limit is 95e12BTC/140e12GBP = 0.6785
            // trade quality is 47.7857e12BTC/70.4210e12GBP = 0.6785
            env(pay(alice, carol, BTC(95'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(GBP(140'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 53.3322e12ETH with 56.3368e12GBP via the amm
            // and pays 25% tr fee on 56.3368e12GBP
            // 1,400e12 - 56.3368e12*1.25 = 1400e12 - 70.4210e12 =
            // 1329.5789e12GBP
            env.require(balance(alice, GBP(1'329'578'947'368'421)));
            //// 25% on 56.3368e12ETH is paid in tr fee 56.3368e12*1.25
            ///= 70.4210e12ETH
            // 56.3368e12GBP is swapped in for 53.3322e12ETH
            BEAST_EXPECT(amm.expectBalances(
                GBP(1'056'336'842'105'263),
                ETH(946'667'729'591'837),
                amm.tokens()));

            // 25% on 42.6658e12ETH is paid in tr fee 42.6658e12*1.25
            // = 53.3322e12ETH 42.6658e12ETH/59.7321e12BTC
            env.require(balance(ed, BTC(1'340'267'857'142'857)));
            env.require(balance(ed, ETH(1'442'665'816'326'530)));
            BEAST_EXPECT(expectOffers(
                env,
                ed,
                1,
                {Amounts{
                    ETH(957'334'183'673'470), BTC(1'340'267'857'142'857)}}));
            // 25% on 47.7857e12BTC is paid in tr fee 47.7857e12*1.25
            // = 59.7321e12BTC
            env.require(balance(carol, BTC(1'447'785714285714)));
        }
        {
            // Payment via AMM, AMM  with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw, alice, bob, carol, ed);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM amm1(
                env,
                bob,
                GBP(1'000'000'000'000'000),
                ETH(1'000'000'000'000'000));
            AMM amm2(
                env,
                ed,
                ETH(1'000'000'000'000'000),
                BTC(1'400'000'000'000'000));

            // requested quality limit is 90e12BTC/145e12GBP = 0.6206
            // trade quality is 66.7432e12BTC/107.5308e12GBP = 0.6206
            env(pay(alice, carol, BTC(90'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(GBP(145'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 53.3322e12ETH with 107.5308e12GBP
            // 25% on 86.0246e12GBP is paid in tr fee
            // 1,400e12 - 86.0246e12*1.25 = 1400e12 - 107.5308e12 =
            // 1229.4691e12GBP
            env.require(balance(alice, GBP(1'292'469'135'802'465)));
            // 86.0246e12GBP is swapped in for 79.2106e12ETH
            BEAST_EXPECT(amm1.expectBalances(
                GBP(1'086'024'691'358'028),
                ETH(920'789'377'955'618),
                amm1.tokens()));
            // 25% on 63.3684e12ETH is paid in tr fee 63.3684e12*1.25
            // = 79.2106e12ETH 63.3684e12ETH is swapped in for 83.4291e12BTC
            BEAST_EXPECT(amm2.expectBalances(
                ETH(1'063'368'497'635'505),
                BTC(1'316'570'881'226'053),
                amm2.tokens()));

            // 25% on 66.7432e12BTC is paid in tr fee 66.7432e12*1.25
            // = 83.4291e12BTC
            env.require(balance(carol, BTC(1'466'743'295'019'157)));
        }
        {
            // Payment by the issuer via AMM, AMM  with limit quality,
            // deliver less than requested
            Env env(*this, features);

            env.fund(XRP(1'000), gw, alice, bob, carol);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = MPTDEXFlags});

            AMM amm1(
                env,
                alice,
                GBP(1'000'000'000'000'000),
                ETH(1'000'000'000'000'000));
            AMM amm2(
                env,
                bob,
                ETH(1'000'000'000'000'000),
                BTC(1'400'000'000'000'000));

            // requested quality limit is 90e12BTC/120e12GBP = 0.75
            // trade quality is 81.1111e12BTC/108.1481e12GBP = 0.75
            env(pay(gw, carol, BTC(90'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(GBP(120'000'000'000'000)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // 108.1481e12GBP is swapped in for 97.5935e12ETH
            BEAST_EXPECT(amm1.expectBalances(
                GBP(1'108'148'148'148'150),
                ETH(902'406'417'112'298),
                amm1.tokens()));
            // 25% on 78.0748e12ETH is paid in tr fee 78.0748e12*1.25
            // = 97.5935e12ETH 78.0748e12ETH is swapped in for 101.3888e12BTC
            BEAST_EXPECT(amm2.expectBalances(
                ETH(1'078'074'866'310'161),
                BTC(1'298'611'111'111'111),
                amm2.tokens()));

            // 25% on 81.1111e12BTC is paid in tr fee 81.1111e12*1.25 =
            // 101.3888e12BTC
            env.require(balance(carol, BTC(1'481'111'111'111'111)));
        }
    }

    void
    testLimitQuality()
    {
        // Single path with amm, offer, and limit quality. The quality limit
        // is such that the first offer should be taken but the second
        // should not. The total amount delivered should be the sum of the
        // two offers and sendMax should be more than the first offer.
        testcase("limitQuality");
        using namespace jtx;

        {
            Env env(*this);
            env.fund(XRP(10'000), gw, alice, bob, carol);

            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 2'000'000,
                 .flags = MPTDEXFlags});

            AMM ammBob(env, bob, XRP(1'000), ETH(1'050'000));
            env(offer(bob, XRP(100), ETH(50'000)));

            env(pay(alice, carol, ETH(100'000)),
                path(~MPT(ETH)),
                sendmax(XRP(100)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(1'050), ETH(1'000'000), ammBob.tokens()));
            env.require(balance(carol, ETH(2'050'000)));
            BEAST_EXPECT(
                expectOffers(env, bob, 1, {{{XRP(100), ETH(50'000)}}}));
        }
    }

    void
    testXRPPathLoop()
    {
        testcase("Circular XRP");

        using namespace jtx;

        // Payment path starting with XRP
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, gw);
                env.close();
                auto const ETH = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob},
                     .limit = 2000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob},
                     .limit = 2000'000});

                env(pay(gw, alice, BTC(200'000)));
                env(pay(gw, bob, BTC(200'000)));
                env(pay(gw, alice, ETH(200'000)));
                env(pay(gw, bob, ETH(200'000)));
                env.close();

                AMM ammAliceXRP_BTC(env, alice, XRP(100), BTC(101'000));
                AMM ammAliceXRP_ETH(env, alice, XRP(100), ETH(101'000));
                env(pay(alice, bob, ETH(1'000)),
                    path(~BTC, ~XRP, ~ETH),
                    sendmax(XRP(1)),
                    txflags(tfNoRippleDirect),
                    ter(temBAD_PATH_LOOP));
            };
            testHelper2TokensMix(test);
        }

        // Payment path ending with XRP
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, gw);
                env.close();
                auto const ETH = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob},
                     .limit = 2000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob},
                     .limit = 2000'000});

                env(pay(gw, alice, BTC(200'000)));
                env(pay(gw, bob, BTC(200'000)));
                env(pay(gw, alice, ETH(200'000)));
                env(pay(gw, bob, ETH(200'000)));
                env.close();

                AMM ammAliceXRP_BTC(env, alice, XRP(100), BTC(100'000));
                AMM ammAliceXRP_ETH(env, alice, XRP(100), ETH(100'000));
                // ETH -> //XRP -> //BTC ->XRP
                env(pay(alice, bob, XRP(1)),
                    path(~XRP, ~BTC, ~XRP),
                    sendmax(ETH(1'000)),
                    txflags(tfNoRippleDirect),
                    ter(temBAD_PATH_LOOP));
            };
            testHelper2TokensMix(test);
        }

        // Payment where loop is formed in the middle of the path, not
        // on an endpoint
        {
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this);
                env.fund(XRP(10'000), gw, alice, bob);
                env.close();
                auto const ETH = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob},
                     .limit = 2000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob},
                     .limit = 2000'000});
                auto const JPY = issue2(
                    {.env = env,
                     .token = "JPY",
                     .issuer = gw,
                     .holders = {alice, bob},
                     .limit = 2000'000});

                env(pay(gw, alice, BTC(200'000)));
                env(pay(gw, bob, BTC(200'000)));
                env(pay(gw, alice, ETH(200'000)));
                env(pay(gw, bob, ETH(200'000)));
                env(pay(gw, alice, JPY(200'000)));
                env(pay(gw, bob, JPY(200'000)));
                env.close();

                AMM ammAliceXRP_BTC(env, alice, XRP(100), BTC(100'000));
                AMM ammAliceXRP_ETH(env, alice, XRP(100), ETH(100'000));
                AMM ammAliceXRP_JPY(env, alice, XRP(100), JPY(100'000));

                env(pay(alice, bob, JPY(1'000)),
                    path(~XRP, ~ETH, ~XRP, ~JPY),
                    sendmax(BTC(1'000)),
                    txflags(tfNoRippleDirect),
                    ter(temBAD_PATH_LOOP));
            };
            testHelper3TokensMix(test);
        }
    }

    void
    testStepLimit(FeatureBitset features)
    {
        testcase("Step Limit");

        using namespace jtx;
        {
            Env env(*this, features);
            auto const dan = Account("dan");
            auto const ed = Account("ed");

            env.fund(XRP(100'000'000), gw, alice, bob, carol, dan, ed);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {bob, dan, ed},
                 .flags = MPTDEXFlags});

            env(pay(gw, ed, BTC(11'000'000'000'000)));
            env(pay(gw, bob, BTC(1'000'000'000'000)));
            env(pay(gw, dan, BTC(1'000'000'000'000)));

            n_offers(env, 2'000, bob, XRP(1), BTC(1'000'000'000'000));
            n_offers(env, 1, dan, XRP(1), BTC(1'000'000'000'000));
            AMM ammEd(env, ed, XRP(9), BTC(11'000'000'000'000));

            // Alice offers to buy 1000 XRP for 1000e12 BTC. She takes Bob's
            // first offer, removes 999 more as unfunded, then hits the step
            // limit.
            env(offer(alice, BTC(1'000'000'000'000'000), XRP(1'000)));
            env.require(balance(alice, BTC(2'050'125'257'867)));
            env.require(owners(alice, 2));
            env.require(balance(bob, BTC(0)));
            env.require(owners(bob, 1'001));
            env.require(balance(dan, BTC(1'000'000'000'000)));
            env.require(owners(dan, 2));

            // Carol offers to buy 1000 XRP for 1000e12 BTC. She removes Bob's
            // next 1000 offers as unfunded and hits the step limit.
            env(offer(carol, BTC(1'000'000'000'000'000), XRP(1'000)));
            env.require(balance(carol, MPT(BTC)(none)));
            env.require(owners(carol, 1));
            env.require(balance(bob, BTC(0)));
            env.require(owners(bob, 1));
            env.require(balance(dan, BTC(1'000'000'000'000)));
            env.require(owners(dan, 2));
        }

        // MPT/IOU, similar to the case above
        {
            Env env(*this, features);
            auto const dan = Account("dan");
            auto const ed = Account("ed");

            env.fund(XRP(100'000), gw, alice, bob, carol, dan, ed);
            env.close();

            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan, ed},
                 .pay = 10000'000'000,
                 .flags = MPTDEXFlags});

            env.trust(BTC(11'000'000'000'000), ed);
            env(pay(gw, ed, BTC(11'000'000'000'000)));
            env.trust(BTC(1'000'000'000'000), bob);
            env(pay(gw, bob, BTC(1'000'000'000'000)));
            env.trust(BTC(1'000'000'000'000), dan);
            env(pay(gw, dan, BTC(1'000'000'000'000)));
            env.close();

            n_offers(env, 2'000, bob, USD(1000000), BTC(1'000'000'000'000));
            n_offers(env, 1, dan, USD(1000000), BTC(1'000'000'000'000));
            AMM ammEd(env, ed, USD(9000000), BTC(11'000'000'000'000));
            env(offer(alice, BTC(1'000'000'000'000'000), USD(1'000000000)));

            env.require(
                balance(alice, STAmount{BTC, UINT64_C(2050125257867'587), -3}));
            env.require(owners(alice, 3));
            env.require(balance(bob, BTC(0)));
            env.require(owners(bob, 1'002));
            env.require(balance(dan, BTC(1000000000000)));
            env.require(owners(dan, 3));
        }

        // IOU/MPT, similar to the case above
        {
            Env env(*this, features);
            auto const dan = Account("dan");
            auto const ed = Account("ed");

            env.fund(XRP(100'000), gw, alice, bob, carol, dan, ed);
            env.close();

            env.trust(USD(10000'000'000), alice);
            env(pay(gw, alice, USD(10000'000'000)));
            env.trust(USD(10000'000'000), bob);
            env(pay(gw, bob, USD(10000'000'000)));
            env.trust(USD(10000'000'000), carol);
            env(pay(gw, carol, USD(10000'000'000)));
            env.trust(USD(10000'000'000), dan);
            env(pay(gw, dan, USD(10000'000'000)));
            env.trust(USD(10000'000'000), ed);
            env(pay(gw, ed, USD(10000'000'000)));
            env.close();

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {bob, dan, ed},
                 .flags = MPTDEXFlags});

            env(pay(gw, ed, BTC(11'000'000'000'000)));
            env(pay(gw, bob, BTC(1'000'000'000'000)));
            env(pay(gw, dan, BTC(1'000'000'000'000)));
            env.close();

            n_offers(env, 2'000, bob, USD(1000000), BTC(1'000'000'000'000));
            n_offers(env, 1, dan, USD(1000000), BTC(1'000'000'000'000));
            AMM ammEd(env, ed, USD(9000000), BTC(11'000'000'000'000));
            env(offer(alice, BTC(1'000'000'000'000'000), USD(1'000000000)));

            env.require(balance(alice, BTC(2050125628933)));
            env.require(owners(alice, 3));
            env.require(balance(bob, BTC(0)));
            env.require(owners(bob, 1'002));
            env.require(balance(dan, BTC(1000000000000)));
            env.require(owners(dan, 3));
        }

        // MPT/MPT, similar to the case above
        {
            Env env(*this, features);
            auto const dan = Account("dan");
            auto const ed = Account("ed");

            env.fund(XRP(100'000), gw, alice, bob, carol, dan, ed);
            env.close();

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {bob, dan, ed},
                 .flags = MPTDEXFlags});
            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan, ed},
                 .pay = 10000'000'000,
                 .flags = MPTDEXFlags});

            env(pay(gw, ed, BTC(11'000'000'000'000)));
            env(pay(gw, bob, BTC(1'000'000'000'000)));
            env(pay(gw, dan, BTC(1'000'000'000'000)));
            env.close();

            n_offers(env, 2'000, bob, USD(1000000), BTC(1'000'000'000'000));
            n_offers(env, 1, dan, USD(1000000), BTC(1'000'000'000'000));
            AMM ammEd(env, ed, USD(9000000), BTC(11'000'000'000'000));
            env(offer(alice, BTC(1'000'000'000'000'000), USD(1'000000000)));

            env.require(balance(alice, BTC(2050125257867)));
            env.require(owners(alice, 3));
            env.require(balance(bob, BTC(0)));
            env.require(owners(bob, 1'002));
            env.require(balance(dan, BTC(1000000000000)));
            env.require(owners(dan, 3));
        }
    }

    void
    test_convert_all_of_an_asset(FeatureBitset features)
    {
        testcase("Convert all of an asset using DeliverMin");

        using namespace jtx;

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol}, XRP(10'000));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .flags = MPTDEXFlags});

            env(pay(alice, bob, BTC(10'000)),
                delivermin(BTC(10'000)),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, BTC(10'000)),
                delivermin(BTC(-5'000)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, BTC(10'000)),
                delivermin(XRP(5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, BTC(10'000)),
                delivermin(BTC(5'000)),
                txflags(tfPartialPayment),
                ter(tecPATH_DRY));
            env(pay(alice, bob, BTC(10'000)),
                delivermin(BTC(15'000)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(gw, carol, BTC(50'000)));
            AMM ammCarol(env, carol, XRP(10), BTC(15'000));
            env(pay(alice, bob, BTC(10'000)),
                paths(XRP),
                delivermin(BTC(7'000)),
                txflags(tfPartialPayment),
                sendmax(XRP(5)),
                ter(tecPATH_PARTIAL));
            env.require(balance(
                alice,
                drops(
                    10'000'000'000 - 3 * env.current()->fees().base.drops())));
            env.require(balance(
                bob,
                drops(10'000'000'000 - env.current()->fees().base.drops())));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob}, XRP(10'000));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .flags = MPTDEXFlags});

            env(pay(gw, bob, BTC(1'100'000)));
            AMM ammBob(env, bob, XRP(1'000), BTC(1'100'000));
            env(pay(alice, alice, BTC(10'000'000)),
                paths(XRP),
                delivermin(BTC(100'000)),
                txflags(tfPartialPayment),
                sendmax(XRP(100)));
            env.require(balance(alice, BTC(100'000)));
        }

        // IOU/MPT mix, similar to the above case
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 3000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000'000});

                env(pay(gw, alice, USD(10'000)));
                env(pay(gw, bob, USD(10'000)));
                env(pay(gw, bob, BTC(1'200)));
                env.close();

                AMM ammBob(env, bob, USD(1'000), BTC(1'100));
                env(pay(alice, alice, BTC(10'000)),
                    paths(USD),
                    delivermin(BTC(100)),
                    txflags(tfPartialPayment),
                    sendmax(USD(100)));
                env.require(balance(alice, BTC(100)));
            };
            testHelper2TokensMix(test);
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol}, XRP(10'000));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {bob, carol},
                 .flags = MPTDEXFlags});

            env(pay(gw, bob, BTC(1'200'000)));
            AMM ammBob(env, bob, XRP(5'500), BTC(1'200'000));
            env(pay(alice, carol, BTC(10'000'000)),
                paths(XRP),
                delivermin(BTC(200'000)),
                txflags(tfPartialPayment),
                sendmax(XRP(1'000)),
                ter(tecPATH_PARTIAL));
            env(pay(alice, carol, BTC(10'000'000)),
                paths(XRP),
                delivermin(BTC(200'000)),
                txflags(tfPartialPayment),
                sendmax(XRP(1'100)));
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(6'600), BTC(1'000'000), ammBob.tokens()));
            env.require(balance(carol, BTC(200'000)));
        }

        // IOU/MPT mix, similar to the above case
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 3000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000'000});

                env(pay(gw, alice, USD(100'000)));
                env(pay(gw, bob, USD(100'000)));
                env(pay(gw, carol, USD(100'000)));

                env(pay(gw, bob, BTC(1'200)));
                env.close();

                AMM ammBob(env, bob, USD(5'500), BTC(1'200));
                env(pay(alice, carol, BTC(10'000)),
                    paths(USD),
                    delivermin(BTC(200)),
                    txflags(tfPartialPayment),
                    sendmax(USD(1'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, BTC(10'000)),
                    paths(USD),
                    delivermin(BTC(200)),
                    txflags(tfPartialPayment),
                    sendmax(USD(1'100)));
                BEAST_EXPECT(ammBob.expectBalances(
                    USD(6'600), BTC(1'000), ammBob.tokens()));
                env.require(balance(carol, BTC(200)));
            };
            testHelper2TokensMix(test);
        }

        {
            auto const dan = Account("dan");
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol, dan}, XRP(10'000));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {bob, carol, dan},
                 .flags = MPTDEXFlags});

            env(pay(gw, bob, BTC(100'000'000)));
            env(pay(gw, dan, BTC(1'100'000'000)));
            env(offer(bob, XRP(100), BTC(100'000'000)));
            env(offer(bob, XRP(1'000), BTC(100'000'000)));
            AMM ammDan(env, dan, XRP(1'000), BTC(1'100'000'000));

            env(pay(alice, carol, BTC(10'000'000'000)),
                paths(XRP),
                delivermin(BTC(200'000'000)),
                txflags(tfPartialPayment),
                sendmax(XRPAmount(200'000'001)));
            env.require(balance(bob, BTC(0)));
            env.require(balance(carol, BTC(200'000'000)));
            BEAST_EXPECT(ammDan.expectBalances(
                XRPAmount{1'100'000'001}, BTC(1000'000000), ammDan.tokens()));
        }
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace jtx;
        Account const becky{"becky"};

        bool const supportsPreauth = {features[featureDepositPreauth]};

        Env env(*this, features);
        fund(env, gw, {alice, becky}, XRP(5'000));

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, becky},
             .flags = MPTDEXFlags});

        env(pay(gw, alice, BTC(500'000)));
        env.close();

        AMM ammAlice(env, alice, XRP(100), BTC(140'000));

        // becky pays herself BTC (10'000) by consuming part of alice's offer.
        // Make sure the payment works if PaymentAuth is not involved.
        env(pay(becky, becky, BTC(10'000)), path(~MPT(BTC)), sendmax(XRP(10)));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount(107'692'308), BTC(130'000), ammAlice.tokens()));

        // becky decides to require authorization for deposits.
        env(fset(becky, asfDepositAuth));
        env.close();

        // becky pays herself again.  Whether it succeeds depends on
        // whether featureDepositPreauth is enabled.
        TER const expect{
            supportsPreauth ? TER{tesSUCCESS} : TER{tecNO_PERMISSION}};

        env(pay(becky, becky, BTC(10'000)),
            path(~MPT(BTC)),
            sendmax(XRP(10)),
            ter(expect));

        env.close();
    }

    void
    testPayMPT()
    {
        // Exercise MPT payments and non-direct XRP payments to an account
        // that has the lsfDepositAuth flag set.
        testcase("Pay MPT");

        using namespace jtx;

        Env env(*this);

        fund(env, gw, {alice, bob, carol}, XRP(10'000));

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol},
             .flags = MPTDEXFlags});

        env(pay(gw, alice, BTC(150'000)));
        env(pay(gw, carol, BTC(150'000)));
        AMM ammCarol(env, carol, BTC(100'000), XRPAmount(101));

        env(pay(alice, bob, BTC(50'000)));
        env.close();

        // bob sets the lsfDepositAuth flag.
        env(fset(bob, asfDepositAuth), require(flags(bob, asfDepositAuth)));
        env.close();

        // None of the following payments should succeed.
        auto failedMptPayments = [this, &env, &BTC]() {
            env.require(flags(bob, asfDepositAuth));

            // Capture bob's balances before hand to confirm they don't
            // change.
            PrettyAmount const bobXrpBalance{env.balance(bob, XRP)};
            PrettyAmount const bobBTCBalance{env.balance(bob, MPT(BTC))};

            env(pay(alice, bob, BTC(50'000)), ter(tecNO_PERMISSION));
            env.close();

            // Note that even though alice is paying bob in XRP, the payment
            // is still not allowed since the payment passes through an
            // offer.
            env(pay(alice, bob, drops(1)),
                sendmax(BTC(1'000)),
                ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(bobXrpBalance == env.balance(bob, XRP));
            BEAST_EXPECT(bobBTCBalance == env.balance(bob, MPT(BTC)));
        };

        //  Test when bob has an XRP balance > base reserve.
        failedMptPayments();

        // Set bob's XRP balance == base reserve.  Also demonstrate that
        // bob can make payments while his lsfDepositAuth flag is set.
        env(pay(bob, alice, BTC(25'000)));
        env.close();

        {
            STAmount const bobPaysXRP{env.balance(bob, XRP) - reserve(env, 1)};
            XRPAmount const bobPaysFee{reserve(env, 1) - reserve(env, 0)};
            env(pay(bob, alice, bobPaysXRP), fee(bobPaysFee));
            env.close();
        }

        // Test when bob's XRP balance == base reserve.
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0));
        BEAST_EXPECT(env.balance(bob, MPT(BTC)) == BTC(25'000));
        failedMptPayments();

        // Test when bob has an XRP balance == 0.
        env(noop(bob), fee(reserve(env, 0)));
        env.close();

        BEAST_EXPECT(env.balance(bob, XRP) == XRP(0));
        failedMptPayments();

        // Give bob enough XRP for the fee to clear the lsfDepositAuth flag.
        env(pay(alice, bob, drops(env.current()->fees().base)));

        // bob clears the lsfDepositAuth and the next payment succeeds.
        env(fclear(bob, asfDepositAuth));
        env.close();

        env(pay(alice, bob, BTC(50'000)));
        env.close();

        env(pay(alice, bob, drops(1)), sendmax(BTC(1'000)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(
            BTC(101'000), XRPAmount(100), ammCarol.tokens()));
    }

    void
    testIndividualLock(FeatureBitset features)
    {
        testcase("Individual Lock");

        using namespace test::jtx;
        Env env(*this, features);

        Account const G1{"G1"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1'000), G1, alice, bob);

        MPTTester BTC(
            {.env = env,
             .issuer = G1,
             .holders = {alice, bob},
             .flags = tfMPTCanLock | MPTDEXFlags});

        env(pay(G1, bob, BTC(10)));
        env(pay(G1, alice, BTC(205)));
        env.close();

        AMM ammAlice(env, alice, XRP(500), BTC(105));

        env.require(balance(bob, BTC(10)));
        env.require(balance(alice, BTC(100)));

        // Account with MPT unlocked (proving operations normally work)
        // can make Payment
        env(pay(alice, bob, BTC(1)));

        // can receive Payment
        env(pay(bob, alice, BTC(1)));
        env.close();

        // Lock MPT for bob
        BTC.set({.holder = bob, .flags = tfMPTLock});

        {
            // different from IOU.
            // with MPT locked,
            // can not buy more assets
            env(offer(bob, BTC(5), XRP(25)), ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(
                ammAlice.expectBalances(XRP(500), BTC(105), ammAlice.tokens()));
        }

        {
            // can not sell assets
            env(offer(bob, XRP(1), BTC(5)), ter(tecLOCKED));

            // different from IOU
            // can not receive Payment when locked
            env(pay(alice, bob, BTC(1)), ter(tecLOCKED));

            // can not make Payment when locked
            env(pay(bob, alice, BTC(1)), ter(tecLOCKED));

            env.require(balance(bob, BTC(10)));
        }

        {
            // Unlock
            BTC.set({.holder = bob, .flags = tfMPTUnlock});
            env(offer(bob, XRP(1), BTC(5)));
            env(pay(bob, alice, BTC(1)));
            env(pay(alice, bob, BTC(1)));
            env.close();
        }
    }

    void
    testGlobalLock(FeatureBitset features)
    {
        testcase("Global Lock");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};

        env.fund(XRP(12'000), G1);
        env.fund(XRP(1'000), A1);
        env.fund(XRP(20'000), A2, A3, A4);

        MPTTester ETH(
            {.env = env,
             .issuer = G1,
             .holders = {A1, A2, A3, A4},
             .flags = tfMPTCanLock | MPTDEXFlags});

        MPTTester BTC(
            {.env = env,
             .issuer = G1,
             .holders = {A1, A2, A3, A4},
             .flags = tfMPTCanLock | MPTDEXFlags});

        env(pay(G1, A1, ETH(1'000)));
        env(pay(G1, A2, ETH(100)));
        env(pay(G1, A3, BTC(100)));
        env(pay(G1, A4, BTC(100)));
        env.close();

        AMM ammG1(env, G1, XRP(10'000), ETH(100));
        env(offer(A1, XRP(10'000), ETH(100)), txflags(tfPassive));
        env(offer(A2, ETH(100), XRP(10'000)), txflags(tfPassive));
        env.close();

        {
            // Account without Global Lock (proving operations normally
            // work)
            // visible offers where taker_pays is unlocked issuer
            auto offers = getAccountOffers(env, A2)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;

            // visible offers where taker_gets is unlocked issuer
            offers = getAccountOffers(env, A1)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
        }

        {
            // Offers/Payments
            // assets can be bought on the market
            AMM ammA3(env, A3, BTC(1), XRP(1));

            // assets can be sold on the market
            // AMM is bidirectional
            env(pay(G1, A2, ETH(1)));
            env(pay(A2, G1, ETH(1)));
            env(pay(A2, A1, ETH(1)));
            env(pay(A1, A2, ETH(1)));
            ammA3.withdrawAll(std::nullopt);
        }

        {
            // Account with Global Lock
            //  set Global Lock first
            BTC.set({.flags = tfMPTLock});

            // assets can't be bought on the market
            AMM ammA3(env, A3, BTC(1), XRP(1), ter(tecFROZEN));

            // direct issues can be sent
            env(pay(G1, A2, BTC(1)));
            env(pay(A2, G1, BTC(1)));
            // locked
            env(pay(A2, A1, BTC(1)), ter(tecLOCKED));
            env(pay(A1, A2, BTC(1)), ter(tecLOCKED));
        }

        {
            auto offers = getAccountOffers(env, A2)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;

            offers = getAccountOffers(env, A1)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
        }
    }

    void
    testOffersWhenLocked(FeatureBitset features)
    {
        testcase("Offers for Locked MPTs");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};

        env.fund(XRP(2'000), G1, A3, A4);
        env.fund(XRP(2'000), A2);
        env.close();

        MPTTester BTC(
            {.env = env,
             .issuer = G1,
             .holders = {A2, A3, A4},
             .flags = tfMPTCanLock | MPTDEXFlags});

        env(pay(G1, A3, BTC(2'000)));
        env(pay(G1, A4, BTC(2'001)));
        env.close();

        AMM ammA3(env, A3, XRP(1'000), BTC(1'001));

        // removal after successful payment
        //    test: make a payment with partially consuming offer
        env(pay(A2, G1, BTC(1)), paths(MPT(BTC)), sendmax(XRP(1)));
        env.close();

        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1'001), BTC(1'000), ammA3.tokens()));

        //    test: someone else creates an offer providing liquidity
        env(offer(A4, XRP(999), BTC(999)));
        env.close();
        // The offer consumes AMM offer
        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1'000), BTC(1'001), ammA3.tokens()));

        //    test: AMM is Locked
        BTC.set({.holder = ammA3.ammAccount(), .flags = tfMPTLock});
        auto const info = ammA3.ammRpcInfo();
        BEAST_EXPECT(info[jss::amm][jss::asset2_frozen].asBool());
        env.close();

        //    test: Can make a payment via the new offer
        env(pay(A2, G1, BTC(1)), paths(MPT(BTC)), sendmax(XRP(1)));
        env.close();
        // AMM is not consumed
        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1'000), BTC(1'001), ammA3.tokens()));

        // removal buy successful OfferCreate
        //    test: lock the new offer
        BTC.set({.holder = A4, .flags = tfMPTUnlock});
        env.close();

        //    test: can no longer create a crossing offer
        env(offer(A2, BTC(999), XRP(999)));
        env.close();

        //    test: offer was removed by offer_create
        auto offers = getAccountOffers(env, A4)[jss::offers];
        if (!BEAST_EXPECT(checkArraySize(offers, 0u)))
            return;
    }

    void
    testTxMultisign(FeatureBitset features)
    {
        testcase("Multisign AMM Transactions");

        using namespace jtx;
        Env env{*this, features};
        Account const bogie{"bogie", KeyType::secp256k1};
        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const zelda{"zelda", KeyType::secp256k1};
        fund(env, gw, {alice, becky, zelda}, XRP(20'000));

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice, becky, zelda},
             .pay = 20'000'000'000,
             .flags = MPTDEXFlags});

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), sig(alie));
        env.close();
        int const signerListOwners{features[featureMultiSignReserve] ? 2 : 5};
        env.require(owners(alice, signerListOwners + 0));

        msig const ms{becky, bogie};

        // Multisign all AMM transactions
        AMM ammAlice(
            env,
            alice,
            XRP(10'000),
            BTC(10'000),
            false,
            0,
            ammCrtFee(env).drops(),
            std::nullopt,
            std::nullopt,
            ms,
            ter(tesSUCCESS));
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), BTC(10'000), ammAlice.tokens()));

        ammAlice.deposit(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(11'000), BTC(11'000), IOUAmount{11'000'000, 0}));

        ammAlice.withdraw(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), BTC(10'000), ammAlice.tokens()));

        ammAlice.vote({}, 1'000);
        BEAST_EXPECT(ammAlice.expectTradingFee(1'000));

        env(ammAlice.bid({.account = alice, .bidMin = 100}), ms).close();
        BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, IOUAmount{4'000}));
        // 4000 tokens burnt
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), BTC(10'000), IOUAmount{9'996'000, 0}));
    }

    void
    testToStrand(FeatureBitset features)
    {
        testcase("To Strand");

        using namespace jtx;

        // cannot have more than one offer with the same output issue
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, carol, gw);
                env.close();
                auto const ETH = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, bob, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, ETH(50000)));
                env(pay(gw, bob, ETH(50000)));
                env(pay(gw, carol, ETH(50000)));
                env.close();
                AMM bobXRP_BTC(env, bob, XRP(1'000), BTC(1'000));
                AMM bobBTC_ETH(env, bob, BTC(1'000), ETH(1'000));

                // payment path: XRP -> XRP/BTC -> BTC/ETH -> ETH/BTC
                env(pay(alice, carol, BTC(100)),
                    path(~BTC, ~ETH, ~BTC),
                    sendmax(XRP(200)),
                    txflags(tfNoRippleDirect),
                    ter(temBAD_PATH_LOOP));
            };
            testHelper2TokensMix(test);
        }
    }

    void
    testRIPD1373(FeatureBitset features)
    {
        using namespace jtx;
        testcase("RIPD1373");

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob}, XRP(10'000));

            MPTTester BTC(
                {.env = env,
                 .issuer = bob,
                 .holders = {alice, gw},
                 .pay = 100'000'000,
                 .flags = MPTDEXFlags});

            MPTTester ETH(
                {.env = env,
                 .issuer = bob,
                 .holders = {alice, gw},
                 .pay = 100'000'000,
                 .flags = MPTDEXFlags});

            AMM ammXRP_BTC(env, bob, XRP(100), BTC(100'000));
            env(offer(gw, XRP(100), BTC(100'000)), txflags(tfPassive));

            AMM ammBTC_ETH(env, bob, BTC(100'000), ETH(100'000));
            env(offer(gw, BTC(100'000), ETH(100'000)), txflags(tfPassive));

            Path const p = [&] {
                Path result;
                result.push_back(allpe(gw, MPT(BTC)));
                result.push_back(cpe(ETH.issuanceID()));
                return result;
            }();

            PathSet paths(p);

            env(pay(alice, alice, ETH(1'000)),
                json(paths.json()),
                sendmax(XRP(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(temBAD_PATH));
        }

        {
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10'000));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 100'000,
                 .flags = MPTDEXFlags});

            AMM ammBob(env, bob, XRP(100), BTC(100));

            // payment path: XRP -> XRP/BTC -> BTC/XRP
            env(pay(alice, carol, XRP(100)),
                path(~MPT(BTC), ~XRP),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10'000));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 100'000,
                 .flags = MPTDEXFlags});

            AMM ammBob(env, bob, XRP(100), BTC(100));

            // payment path: XRP -> XRP/BTC -> BTC/XRP
            env(pay(alice, carol, XRP(100)),
                path(~MPT(BTC), ~XRP),
                sendmax(XRP(200)),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_MAX));
        }
    }

    void
    testLoop(FeatureBitset features)
    {
        testcase("test loop");
        using namespace jtx;

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .flags = MPTDEXFlags});

            env(pay(gw, bob, BTC(100'000'000)));
            env(pay(gw, alice, BTC(100'000'000)));
            env.close();

            AMM ammBob(env, bob, XRP(100), BTC(100'000'000));

            // payment path: BTC -> BTC/XRP -> XRP/BTC
            env(pay(alice, carol, BTC(100'000'000)),
                sendmax(BTC(100'000'000)),
                path(~XRP, ~MPT(BTC)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }

        {
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);
                env.close();

                auto const BTC = issue1(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});
                auto const ETH = issue2(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});
                auto const CNY = issue3(
                    {.env = env,
                     .token = "CNY",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});

                env(pay(gw, bob, BTC(200)));
                env(pay(gw, bob, ETH(200)));
                env(pay(gw, bob, CNY(100)));
                env.close();

                AMM ammBobXRP_BTC(env, bob, XRP(100), BTC(100));
                AMM ammBobBTC_ETH(env, bob, BTC(100), ETH(100));
                AMM ammBobETH_CNY(env, bob, ETH(100), CNY(100));

                // payment path: XRP->XRP/BTC->BTC/ETH->BTC/CNY
                env(pay(alice, carol, CNY(100)),
                    sendmax(XRP(100)),
                    path(~BTC, ~ETH, ~BTC, ~CNY),
                    txflags(tfNoRippleDirect),
                    ter(temBAD_PATH_LOOP));
            };
            testHelper3TokensMix(test);
        }
    }

    void
    testPaths()
    {
        path_find_consume_all();
        via_offers_via_gateway();
        receive_max();
        path_find_01();
        path_find_02();
        path_find_06();
    }

    void
    testFlow()
    {
        using namespace jtx;
        FeatureBitset const all{testable_amendments()};

        testFalseDry(all);
        testBookStep(all);
        testTransferRateNoOwnerFee(all);
        testLimitQuality();
        testXRPPathLoop();
    }

    void
    testCrossingLimits()
    {
        using namespace jtx;
        FeatureBitset const all{testable_amendments()};
        testStepLimit(all);
    }

    void
    testDeliverMin()
    {
        using namespace jtx;
        FeatureBitset const all{testable_amendments()};
        test_convert_all_of_an_asset(all);
    }

    void
    testDepositAuth()
    {
        auto const supported{jtx::testable_amendments()};
        testPayment(supported - featureDepositPreauth);
        testPayment(supported);
        testPayMPT();
    }

    void
    testLock()
    {
        using namespace test::jtx;
        auto const sa = testable_amendments();
        testIndividualLock(sa);
        testGlobalLock(sa);
        testOffersWhenLocked(sa);
    }

    void
    testMultisign()
    {
        using namespace jtx;
        auto const all = testable_amendments();

        testTxMultisign(
            all - featureMultiSignReserve - featureExpandedSignerList);
        testTxMultisign(all - featureExpandedSignerList);
        testTxMultisign(all);
    }

    void
    testPayStrand()
    {
        using namespace jtx;
        auto const all = testable_amendments();

        testToStrand(all);
        testRIPD1373(all);
        testLoop(all);
    }

    void
    run() override
    {
        testOffers();
        testPaths();
        testFlow();
        testCrossingLimits();
        testDeliverMin();
        testDepositAuth();
        testLock();
        testMultisign();
        testPayStrand();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMExtendedMPT, app, ripple, 1);

}  // namespace test
}  // namespace ripple
