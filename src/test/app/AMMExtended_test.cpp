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
#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/app/misc/AMMUtils.h>
#include <ripple/app/paths/AMMContext.h>
#include <ripple/app/paths/AMMOffer.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/StrandFlow.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/amount.h>
#include <test/jtx/sendmax.h>

#include <chrono>
#include <utility>
#include <vector>

namespace ripple {
namespace test {

/**
 * Tests of AMM that use offers too.
 */
struct AMMExtended_test : public jtx::AMMTest
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

        fund(
            env,
            gw,
            {alice, bob, carol},
            XRP(10'000),
            {USD(200'000), BTC(2'000)});

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env(offer(carol, BTC(49), XRP(49)));
        env(offer(carol, BTC(51), XRP(51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env(offer(carol, XRP(50), USD(50)));
        env(offer(carol, XRP(50), USD(50)));

        // Good quality path
        AMM ammCarol(env, carol, BTC(1'000), USD(100'100));

        PathSet paths(Path(XRP, USD), Path(USD));

        env(pay(alice, bob, USD(100)),
            json(paths.json()),
            sendmax(BTC(1'000)),
            txflags(tfPartialPayment));

        if (!features[fixAMMv1_1])
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{BTC, UINT64_C(1'001'000000374812), -12},
                USD(100'000),
                ammCarol.tokens()));
        }
        else
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{BTC, UINT64_C(1'001'000000374815), -12},
                USD(100'000),
                ammCarol.tokens()));
        }

        env.require(balance(bob, USD(200'100)));
        BEAST_EXPECT(isOffer(env, carol, BTC(49), XRP(49)));
    }

    void
    testEnforceNoRipple(FeatureBitset features)
    {
        testcase("Enforce No Ripple");
        using namespace jtx;

        {
            // No ripple with an implied account step after AMM
            Env env{*this, features};

            Account const dan("dan");
            Account const gw1("gw1");
            Account const gw2("gw2");
            auto const USD1 = gw1["USD"];
            auto const USD2 = gw2["USD"];

            env.fund(XRP(20'000), alice, noripple(bob), carol, dan, gw1, gw2);
            env.trust(USD1(20'000), alice, carol, dan);
            env(trust(bob, USD1(1'000), tfSetNoRipple));
            env.trust(USD2(1'000), alice, carol, dan);
            env(trust(bob, USD2(1'000), tfSetNoRipple));

            env(pay(gw1, dan, USD1(10'000)));
            env(pay(gw1, bob, USD1(50)));
            env(pay(gw2, bob, USD2(50)));

            AMM ammDan(env, dan, XRP(10'000), USD1(10'000));

            env(pay(alice, carol, USD2(50)),
                path(~USD1, bob),
                sendmax(XRP(50)),
                txflags(tfNoRippleDirect),
                ter(tecPATH_DRY));
        }

        {
            // Make sure payment works with default flags
            Env env{*this, features};

            Account const dan("dan");
            Account const gw1("gw1");
            Account const gw2("gw2");
            auto const USD1 = gw1["USD"];
            auto const USD2 = gw2["USD"];

            env.fund(XRP(20'000), alice, bob, carol, gw1, gw2);
            env.fund(XRP(20'000), dan);
            env.trust(USD1(20'000), alice, bob, carol, dan);
            env.trust(USD2(1'000), alice, bob, carol, dan);

            env(pay(gw1, dan, USD1(10'050)));
            env(pay(gw1, bob, USD1(50)));
            env(pay(gw2, bob, USD2(50)));

            AMM ammDan(env, dan, XRP(10'000), USD1(10'050));

            env(pay(alice, carol, USD2(50)),
                path(~USD1, bob),
                sendmax(XRP(50)),
                txflags(tfNoRippleDirect));
            BEAST_EXPECT(ammDan.expectBalances(
                XRP(10'050), USD1(10'000), ammDan.tokens()));

            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, XRP(20'000) - XRP(50) - txfee(env, 1)));
            BEAST_EXPECT(expectLine(env, bob, USD1(100)));
            BEAST_EXPECT(expectLine(env, bob, USD2(0)));
            BEAST_EXPECT(expectLine(env, carol, USD2(50)));
        }
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
                    // Order that can't be filled
                    TER const killedCode{
                        tweakedFeatures[fix1578] ? TER{tecKILLED}
                                                 : TER{tesSUCCESS}};
                    env(offer(carol, USD(100), XRP(100)),
                        txflags(tfFillOrKill),
                        ter(killedCode));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'100), USD(10'000), ammAlice.tokens()));
                    // fee = AMM
                    BEAST_EXPECT(expectLedgerEntryRoot(
                        env, carol, XRP(30'000) - (txfee(env, 1))));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                    BEAST_EXPECT(expectLine(env, carol, USD(30'000)));

                    // Order that can be filled
                    env(offer(carol, XRP(100), USD(100)),
                        txflags(tfFillOrKill),
                        ter(tesSUCCESS));
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'000), USD(10'100), ammAlice.tokens()));
                    BEAST_EXPECT(expectLedgerEntryRoot(
                        env, carol, XRP(30'000) + XRP(100) - txfee(env, 2)));
                    BEAST_EXPECT(expectLine(env, carol, USD(29'900)));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                },
                {{XRP(10'100), USD(10'000)}},
                0,
                std::nullopt,
                {tweakedFeatures});

            // Immediate or Cancel - cross as much as possible
            // and add nothing on the books.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    env(offer(carol, XRP(200), USD(200)),
                        txflags(tfImmediateOrCancel),
                        ter(tesSUCCESS));

                    // AMM generates a synthetic offer of 100USD/100XRP
                    // to match the CLOB offer quality.
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'000), USD(10'100), ammAlice.tokens()));
                    // +AMM - offer * fee
                    BEAST_EXPECT(expectLedgerEntryRoot(
                        env, carol, XRP(30'000) + XRP(100) - txfee(env, 1)));
                    // AMM
                    BEAST_EXPECT(expectLine(env, carol, USD(29'900)));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                },
                {{XRP(10'100), USD(10'000)}},
                0,
                std::nullopt,
                {tweakedFeatures});

            // tfPassive -- place the offer without crossing it.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    // Carol creates a passive offer that could cross AMM.
                    // Carol's offer should stay in the ledger.
                    env(offer(carol, XRP(100), USD(100), tfPassive));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'100), STAmount{USD, 10'000}, ammAlice.tokens()));
                    BEAST_EXPECT(expectOffers(
                        env, carol, 1, {{{XRP(100), STAmount{USD, 100}}}}));
                },
                {{XRP(10'100), USD(10'000)}},
                0,
                std::nullopt,
                {tweakedFeatures});

            // tfPassive -- cross only offers of better quality.
            testAMM(
                [&](AMM& ammAlice, Env& env) {
                    env(offer(alice, USD(110), XRP(100)));
                    env.close();

                    // Carol creates a passive offer.  That offer should cross
                    // AMM and leave Alice's offer untouched.
                    env(offer(carol, XRP(100), USD(100), tfPassive));
                    env.close();
                    BEAST_EXPECT(ammAlice.expectBalances(
                        XRP(10'900),
                        STAmount{USD, UINT64_C(9'082'56880733945), -11},
                        ammAlice.tokens()));
                    BEAST_EXPECT(expectOffers(env, carol, 0));
                    BEAST_EXPECT(expectOffers(env, alice, 1));
                },
                {{XRP(11'000), USD(9'000)}},
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

        fund(env, gw, {bob, alice}, XRP(300'000), {USD(100)}, Fund::All);

        AMM ammAlice(env, alice, XRP(150'000), USD(50));

        // Existing offer pays better than this wants.
        // Partially consume existing offer.
        // Pay 1 USD, get 3061224490 Drops.
        auto const xrpTransferred = XRPAmount{3'061'224'490};
        env(offer(bob, USD(1), XRP(4'000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(150'000) + xrpTransferred,
            USD(49),
            IOUAmount{273'861'278752583, -8}));

        BEAST_EXPECT(expectLine(env, bob, STAmount{USD, 101}));
        BEAST_EXPECT(expectLedgerEntryRoot(
            env, bob, XRP(300'000) - xrpTransferred - txfee(env, 1)));
        BEAST_EXPECT(expectOffers(env, bob, 0));
    }

    void
    testOfferCrossWithLimitOverride(FeatureBitset features)
    {
        testcase("Offer Crossing with Limit Override");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(200'000), gw, alice, bob);

        env(trust(alice, USD(1'000)));

        env(pay(gw, alice, alice["USD"](500)));

        AMM ammAlice(env, alice, XRP(150'000), USD(51));
        env(offer(bob, USD(1), XRP(3'000)));

        BEAST_EXPECT(
            ammAlice.expectBalances(XRP(153'000), USD(50), ammAlice.tokens()));

        auto jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-1");
        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string(
                (XRP(200'000) - XRP(3'000) - env.current()->fees().base * 1)
                    .xrp()));
    }

    void
    testCurrencyConversionEntire(FeatureBitset features)
    {
        testcase("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, XRP(10'000));
        env.require(owners(bob, 0));

        env(trust(alice, USD(100)));
        env(trust(bob, USD(1'000)));
        env(pay(gw, bob, USD(1'000)));

        env.require(owners(alice, 1), owners(bob, 1));

        env(pay(gw, alice, alice["USD"](100)));
        AMM ammBob(env, bob, USD(200), XRP(1'500));

        env(pay(alice, alice, XRP(500)), sendmax(USD(100)));

        BEAST_EXPECT(
            ammBob.expectBalances(USD(300), XRP(1'000), ammBob.tokens()));
        BEAST_EXPECT(expectLine(env, alice, USD(0)));

        auto jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10'000) + XRP(500) - env.current()->fees().base * 2)
                          .xrp()));
    }

    void
    testCurrencyConversionInParts(FeatureBitset features)
    {
        testcase("Currency Conversion: In Parts");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Alice converts USD to XRP which should fail
                // due to PartialPayment.
                env(pay(alice, alice, XRP(100)),
                    sendmax(USD(100)),
                    ter(tecPATH_PARTIAL));

                // Alice converts USD to XRP, should succeed because
                // we permit partial payment
                env(pay(alice, alice, XRP(100)),
                    sendmax(USD(100)),
                    txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{9'900'990'100}, USD(10'100), ammAlice.tokens()));
                // initial 30,000 - 10,000AMM - 100pay
                BEAST_EXPECT(expectLine(env, alice, USD(19'900)));
                // initial 30,000 - 10,0000AMM + 99.009900pay - fee*3
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice,
                    XRP(30'000) - XRP(10'000) + XRPAmount{99'009'900} -
                        ammCrtFee(env) - txfee(env, 2)));
            },
            {{XRP(10'000), USD(10'000)}},
            0,
            std::nullopt,
            {features});
    }

    void
    testCrossCurrencyStartXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Start with XRP");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(XRP(1'000), bob);
                env(trust(bob, USD(100)));
                env.close();
                env(pay(alice, bob, USD(100)), sendmax(XRP(100)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100), USD(10'000), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, bob, USD(100)));
            },
            {{XRP(10'000), USD(10'100)}},
            0,
            std::nullopt,
            {features});
    }

    void
    testCrossCurrencyEndXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: End with XRP");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(XRP(1'000), bob);
                env(trust(bob, USD(100)));
                env.close();
                env(pay(alice, bob, XRP(100)), sendmax(USD(100)));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000), USD(10'100), ammAlice.tokens()));
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(1'000) + XRP(100) - txfee(env, 1)));
            },
            {{XRP(10'100), USD(10'000)}},
            0,
            std::nullopt,
            {features});
    }

    void
    testCrossCurrencyBridged(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Bridged");

        using namespace jtx;

        Env env{*this, features};

        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const dan = Account{"dan"};
        auto const USD1 = gw1["USD"];
        auto const EUR1 = gw2["EUR"];

        fund(env, gw1, {gw2, alice, bob, carol, dan}, XRP(60'000));

        env(trust(alice, USD1(1'000)));
        env.close();
        env(trust(bob, EUR1(1'000)));
        env.close();
        env(trust(carol, USD1(10'000)));
        env.close();
        env(trust(dan, EUR1(1'000)));
        env.close();

        env(pay(gw1, alice, alice["USD"](500)));
        env.close();
        env(pay(gw1, carol, carol["USD"](6'000)));
        env(pay(gw2, dan, dan["EUR"](400)));
        env.close();

        AMM ammCarol(env, carol, USD1(5'000), XRP(50'000));

        env(offer(dan, XRP(500), EUR1(50)));
        env.close();

        Json::Value jtp{Json::arrayValue};
        jtp[0u][0u][jss::currency] = "XRP";
        env(pay(alice, bob, EUR1(30)),
            json(jss::Paths, jtp),
            sendmax(USD1(333)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(
            XRP(49'700),
            STAmount{USD1, UINT64_C(5'030'181086519115), -12},
            ammCarol.tokens()));
        BEAST_EXPECT(expectOffers(env, dan, 1, {{Amounts{XRP(200), EUR(20)}}}));
        BEAST_EXPECT(expectLine(env, bob, STAmount{EUR1, 30}));
    }

    void
    testOfferFeesConsumeFunds(FeatureBitset features)
    {
        testcase("Offer Fees Consume Funds");

        using namespace jtx;

        Env env{*this, features};

        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const gw3 = Account{"gateway_3"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];
        auto const USD3 = gw3["USD"];

        // Provide micro amounts to compensate for fees to make results round
        // nice.
        // reserve: Alice has 3 entries in the ledger, via trust lines
        // fees:
        //  1 for each trust limit == 3 (alice < mtgox/amazon/bitstamp) +
        //  1 for payment          == 4
        auto const starting_xrp = XRP(100) +
            env.current()->fees().accountReserve(3) +
            env.current()->fees().base * 4;

        env.fund(starting_xrp, gw1, gw2, gw3, alice);
        env.fund(XRP(2'000), bob);

        env(trust(alice, USD1(1'000)));
        env(trust(alice, USD2(1'000)));
        env(trust(alice, USD3(1'000)));
        env(trust(bob, USD1(1'200)));
        env(trust(bob, USD2(1'100)));

        env(pay(gw1, bob, bob["USD"](1'200)));

        AMM ammBob(env, bob, XRP(1'000), USD1(1'200));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(alice, USD1(200), XRP(200)));

        // The pool gets only 100XRP for ~109.09USD, even though
        // it can exchange more.
        BEAST_EXPECT(ammBob.expectBalances(
            XRP(1'100),
            STAmount{USD1, UINT64_C(1'090'909090909091), -12},
            ammBob.tokens()));

        auto jrr = ledgerEntryState(env, alice, gw1, "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] ==
            "109.090909090909");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] == XRP(350).value().getText());
    }

    void
    testOfferCreateThenCross(FeatureBitset features)
    {
        testcase("Offer Create, then Cross");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw, {alice, bob}, XRP(200'000));

        env(rate(gw, 1.005));

        env(trust(alice, USD(1'000)));
        env(trust(bob, USD(1'000)));

        env(pay(gw, bob, USD(1)));
        env(pay(gw, alice, USD(200)));

        AMM ammAlice(env, alice, USD(150), XRP(150'100));
        env(offer(bob, XRP(100), USD(0.1)));

        BEAST_EXPECT(ammAlice.expectBalances(
            USD(150.1), XRP(150'000), ammAlice.tokens()));

        auto const jrr = ledgerEntryState(env, bob, gw, "USD");
        // Bob pays 0.005 transfer fee. Note 10**-10 round-off.
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] == "-0.8995000001");
    }

    void
    testSellFlagBasic(FeatureBitset features)
    {
        testcase("Offer tfSell: Basic Sell");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(offer(carol, USD(100), XRP(100)), json(jss::Flags, tfSell));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000), USD(9'999), ammAlice.tokens()));
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectLine(env, carol, USD(30'101)));
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, carol, XRP(30'000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(9'900), USD(10'100)}},
            0,
            std::nullopt,
            {features});
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

        env(trust(alice, USD(150)));
        env(trust(bob, USD(4'000)));

        env(pay(gw, bob, bob["USD"](2'200)));

        AMM ammBob(env, bob, XRP(1'000), USD(2'200));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100 USD for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more USD than asked for.
        env(offer(alice, USD(100), XRP(200)), json(jss::Flags, tfSell));
        BEAST_EXPECT(
            ammBob.expectBalances(XRP(1'100), USD(2'000), ammBob.tokens()));
        BEAST_EXPECT(expectLine(env, alice, USD(200)));
        BEAST_EXPECT(expectLedgerEntryRoot(env, alice, XRP(250)));
        BEAST_EXPECT(expectOffers(env, alice, 0));
    }

    void
    testGatewayCrossCurrency(FeatureBitset features)
    {
        testcase("Client Issue: Gateway Cross Currency");

        using namespace jtx;

        Env env{*this, features};

        auto const XTS = gw["XTS"];
        auto const XXX = gw["XXX"];

        auto const starting_xrp =
            XRP(100.1) + reserve(env, 1) + env.current()->fees().base * 2;
        fund(
            env,
            gw,
            {alice, bob},
            starting_xrp,
            {XTS(100), XXX(100)},
            Fund::All);

        AMM ammAlice(env, alice, XTS(100), XXX(100));

        Json::Value payment;
        payment[jss::secret] = toBase58(generateSeed("bob"));
        payment[jss::id] = env.seq(bob);
        payment[jss::build_path] = true;
        payment[jss::tx_json] = pay(bob, bob, bob["XXX"](1));
        payment[jss::tx_json][jss::Sequence] =
            env.current()
                ->read(keylet::account(bob.id()))
                ->getFieldU32(sfSequence);
        payment[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
        payment[jss::tx_json][jss::SendMax] =
            bob["XTS"](1.5).value().getJson(JsonOptions::none);
        payment[jss::tx_json][jss::Flags] = tfPartialPayment;
        auto const jrr = env.rpc("json", "submit", to_string(payment));
        BEAST_EXPECT(jrr[jss::result][jss::status] == "success");
        BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");
        if (!features[fixAMMv1_1])
        {
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(XTS, UINT64_C(101'010101010101), -12),
                XXX(99),
                ammAlice.tokens()));
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{XTS, UINT64_C(98'989898989899), -12}));
        }
        else
        {
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(XTS, UINT64_C(101'0101010101011), -13),
                XXX(99),
                ammAlice.tokens()));
            BEAST_EXPECT(expectLine(
                env, bob, STAmount{XTS, UINT64_C(98'9898989898989), -13}));
        }
        BEAST_EXPECT(expectLine(env, bob, XXX(101)));
    }

    void
    testBridgedCross(FeatureBitset features)
    {
        testcase("Bridged Crossing");

        using namespace jtx;

        {
            Env env{*this, features};

            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15'000), EUR(15'000)},
                Fund::All);

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP AMM is created.
            //   o carol has EUR but wants USD.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), USD(10'100));
            AMM ammBob(env, bob, EUR(10'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(10'000), EUR(10'100), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15'100)));
            BEAST_EXPECT(expectLine(env, carol, EUR(14'900)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        {
            Env env{*this, features};

            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15'000), EUR(15'000)},
                Fund::All);

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP offer is created.
            //   o carol has EUR but wants USD.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and bob's offer are created, then autobridging
            // will not occur.
            AMM ammAlice(env, alice, XRP(10'000), USD(10'100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15'100)));
            BEAST_EXPECT(expectLine(env, carol, EUR(14'900)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            Env env{*this, features};

            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15'000), EUR(15'000)},
                Fund::All);

            // The scenario:
            //   o USD/XRP offer is created.
            //   o EUR/XRP AMM is created.
            //   o carol has EUR but wants USD.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and alice's offer are created, then
            // autobridging will not occur.
            env(offer(alice, XRP(100), USD(100)));
            env.close();
            AMM ammBob(env, bob, EUR(10'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(10'000), EUR(10'100), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15'100)));
            BEAST_EXPECT(expectLine(env, carol, EUR(14'900)));
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
            fund(env, gw, {alice, bob}, {USD(20'000)}, Fund::All);
            AMM ammBob(env, bob, XRP(20'000), USD(200));
            // alice submits a tfSell | tfFillOrKill offer that does not cross.
            env(offer(alice, USD(2.1), XRP(210), tfSell | tfFillOrKill),
                ter(killedCode));

            BEAST_EXPECT(
                ammBob.expectBalances(XRP(20'000), USD(200), ammBob.tokens()));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }
        {
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(1'000)}, Fund::All);
            AMM ammBob(env, bob, XRP(20'000), USD(200));
            // alice submits a tfSell | tfFillOrKill offer that crosses.
            // Even though tfSell is present it doesn't matter this time.
            env(offer(alice, USD(2), XRP(220), tfSell | tfFillOrKill));
            env.close();
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(20'220),
                STAmount{USD, UINT64_C(197'8239366963403), -13},
                ammBob.tokens()));
            BEAST_EXPECT(expectLine(
                env, alice, STAmount{USD, UINT64_C(1'002'17606330366), -11}));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses and
            // returns more than was asked for (because of the tfSell flag).
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(1'000)}, Fund::All);
            AMM ammBob(env, bob, XRP(20'000), USD(200));

            env(offer(alice, USD(10), XRP(1'500), tfSell | tfFillOrKill));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(21'500),
                STAmount{USD, UINT64_C(186'046511627907), -12},
                ammBob.tokens()));
            BEAST_EXPECT(expectLine(
                env, alice, STAmount{USD, UINT64_C(1'013'953488372093), -12}));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that doesn't cross.
            // This would have succeeded with a regular tfSell, but the
            // fillOrKill prevents the transaction from crossing since not
            // all of the offer is consumed because AMM generated offer,
            // which matches alice's offer quality is ~ 10XRP/0.01996USD.
            Env env{*this, features};
            fund(env, gw, {alice, bob}, {USD(10'000)}, Fund::All);
            AMM ammBob(env, bob, XRP(5000), USD(10));

            env(offer(alice, USD(1), XRP(501), tfSell | tfFillOrKill),
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

        // AMM XRP/USD. Alice places USD/XRP offer.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();

                env(offer(carol, USD(100), XRP(100)));
                env.close();

                // AMM doesn't pay the transfer fee
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100), USD(10'000), ammAlice.tokens()));
                BEAST_EXPECT(expectLine(env, carol, USD(30'100)));
                BEAST_EXPECT(expectOffers(env, carol, 0));
            },
            {{XRP(10'000), USD(10'100)}},
            0,
            std::nullopt,
            {features});

        // Reverse the order, so the offer in the books is to sell XRP
        // in return for USD.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw, 1.25));
                env.close();

                env(offer(carol, XRP(100), USD(100)));
                env.close();

                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000), USD(10'100), ammAlice.tokens()));
                // Carol pays 25% transfer fee
                BEAST_EXPECT(expectLine(env, carol, USD(29'875)));
                BEAST_EXPECT(expectOffers(env, carol, 0));
            },
            {{XRP(10'100), USD(10'000)}},
            0,
            std::nullopt,
            {features});

        {
            // Bridged crossing.
            Env env{*this, features};
            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15'000), EUR(15'000)},
                Fund::All);
            env(rate(gw, 1.25));

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), USD(10'100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15'100)));
            // Carol pays 25% transfer fee.
            BEAST_EXPECT(expectLine(env, carol, EUR(14'875)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(
                env,
                gw,
                {alice, bob, carol},
                {USD(15'000), EUR(15'000)},
                Fund::All);
            env(rate(gw, 1.25));

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), USD(10'050));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // partially consumes Bob's offer.
            env(offer(carol, USD(50), EUR(50)));
            env.close();
            // This test verifies that the amount removed from an offer
            // accounts for the transfer fee that is removed from the
            // account but not from the remaining offer.

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'050), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(15'050)));
            // Carol pays 25% transfer fee.
            BEAST_EXPECT(expectLine(env, carol, EUR(14'937.5)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(
                expectOffers(env, bob, 1, {{Amounts{EUR(50), XRP(50)}}}));
        }

        {
            // A trust line's QualityIn should not affect offer crossing.
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(env, gw, {alice, carol, bob}, XRP(30'000));
            env(rate(gw, 1.25));
            env(trust(alice, USD(15'000)));
            env(trust(bob, EUR(15'000)));
            env(trust(carol, EUR(15'000)), qualityInPercent(80));
            env(trust(bob, USD(15'000)));
            env(trust(carol, USD(15'000)));
            env.close();

            env(pay(gw, alice, USD(11'000)));
            env(pay(gw, carol, EUR(1'000)), sendmax(EUR(10'000)));
            env.close();
            // 1000 / 0.8
            BEAST_EXPECT(expectLine(env, carol, EUR(1'250)));
            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), USD(10'100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(100)));
            // Carol pays 25% transfer fee: 1250 - 100(offer) - 25(transfer fee)
            BEAST_EXPECT(expectLine(env, carol, EUR(1'125)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        {
            // A trust line's QualityOut should not affect offer crossing.
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(env, gw, {alice, carol, bob}, XRP(30'000));
            env(rate(gw, 1.25));
            env(trust(alice, USD(15'000)));
            env(trust(bob, EUR(15'000)));
            env(trust(carol, EUR(15'000)), qualityOutPercent(120));
            env(trust(bob, USD(15'000)));
            env(trust(carol, USD(15'000)));
            env.close();

            env(pay(gw, alice, USD(11'000)));
            env(pay(gw, carol, EUR(1'000)), sendmax(EUR(10'000)));
            env.close();
            BEAST_EXPECT(expectLine(env, carol, EUR(1'000)));
            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM ammAlice(env, alice, XRP(10'000), USD(10'100));
            env(offer(bob, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol, USD(100), EUR(100)));
            env.close();

            // AMM pay doesn't transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(100)));
            // Carol pays 25% transfer fee: 1000 - 100(offer) - 25(transfer fee)
            BEAST_EXPECT(expectLine(env, carol, EUR(875)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
            BEAST_EXPECT(expectOffers(env, bob, 0));
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

        auto const USD_bob = bob["USD"];
        auto const f = env.current()->fees().base;

        env.fund(XRP(30'000) + f, alice, bob);
        env.close();
        AMM ammBob(env, bob, XRP(10'000), USD_bob(10'100));

        env(offer(alice, USD_bob(100), XRP(100)));
        env.close();

        BEAST_EXPECT(ammBob.expectBalances(
            XRP(10'100), USD_bob(10'000), ammBob.tokens()));
        BEAST_EXPECT(expectOffers(env, alice, 0));
        BEAST_EXPECT(expectLine(env, alice, USD_bob(100)));
    }

    void
    testBadPathAssert(FeatureBitset features)
    {
        // At one point in the past this invalid path caused assert.  It
        // should not be possible for user-supplied data to cause assert.
        // Make sure assert is gone.
        testcase("Bad path assert");

        using namespace jtx;

        // The problem was identified when featureOwnerPaysFee was enabled,
        // so make sure that gets included.
        Env env{*this, features | featureOwnerPaysFee};

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;
        {
            // A trust line's QualityOut should not affect offer crossing.
            auto const ann = Account("ann");
            auto const A_BUX = ann["BUX"];
            auto const bob = Account("bob");
            auto const cam = Account("cam");
            auto const dan = Account("dan");
            auto const D_BUX = dan["BUX"];

            // Verify trust line QualityOut affects payments.
            env.fund(reserve(env, 4) + (fee * 4), ann, bob, cam, dan);
            env.close();

            env(trust(bob, A_BUX(400)));
            env(trust(bob, D_BUX(200)), qualityOutPercent(120));
            env(trust(cam, D_BUX(100)));
            env.close();
            env(pay(dan, bob, D_BUX(100)));
            env.close();
            BEAST_EXPECT(expectLine(env, bob, D_BUX(100)));

            env(pay(ann, cam, D_BUX(60)), path(bob, dan), sendmax(A_BUX(200)));
            env.close();

            BEAST_EXPECT(expectLine(env, ann, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, ann, D_BUX(none)));
            BEAST_EXPECT(expectLine(env, bob, A_BUX(72)));
            BEAST_EXPECT(expectLine(env, bob, D_BUX(40)));
            BEAST_EXPECT(expectLine(env, cam, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, cam, D_BUX(60)));
            BEAST_EXPECT(expectLine(env, dan, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, dan, D_BUX(none)));

            AMM ammBob(env, bob, A_BUX(30), D_BUX(30));

            env(trust(ann, D_BUX(100)));
            env.close();

            // This payment caused the assert.
            env(pay(ann, ann, D_BUX(30)),
                path(A_BUX, D_BUX),
                sendmax(A_BUX(30)),
                ter(temBAD_PATH));
            env.close();

            BEAST_EXPECT(
                ammBob.expectBalances(A_BUX(30), D_BUX(30), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, ann, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, ann, D_BUX(0)));
            BEAST_EXPECT(expectLine(env, cam, A_BUX(none)));
            BEAST_EXPECT(expectLine(env, cam, D_BUX(60)));
            BEAST_EXPECT(expectLine(env, dan, A_BUX(0)));
            BEAST_EXPECT(expectLine(env, dan, D_BUX(none)));
        }
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
        auto const A_BUX = ann["BUX"];
        auto const B_BUX = bob["BUX"];

        auto const fee = env.current()->fees().base;
        env.fund(XRP(1'000), carol);
        env.fund(reserve(env, 4) + (fee * 5), ann, bob, cam);
        env.close();

        env(trust(ann, B_BUX(40)));
        env(trust(cam, A_BUX(40)));
        env(trust(bob, A_BUX(30)));
        env(trust(cam, B_BUX(40)));
        env(trust(carol, B_BUX(400)));
        env(trust(carol, A_BUX(400)));
        env.close();

        env(pay(ann, cam, A_BUX(35)));
        env(pay(bob, cam, B_BUX(35)));
        env(pay(bob, carol, B_BUX(400)));
        env(pay(ann, carol, A_BUX(400)));

        AMM ammCarol(env, carol, A_BUX(300), B_BUX(330));

        // cam puts an offer on the books that her upcoming offer could cross.
        // But this offer should be deleted, not crossed, by her upcoming
        // offer.
        env(offer(cam, A_BUX(29), B_BUX(30), tfPassive));
        env.close();
        env.require(balance(cam, A_BUX(35)));
        env.require(balance(cam, B_BUX(35)));
        env.require(offers(cam, 1));

        // This offer caused the assert.
        env(offer(cam, B_BUX(30), A_BUX(30)));

        // AMM is consumed up to the first cam Offer quality
        if (!features[fixAMMv1_1])
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{A_BUX, UINT64_C(309'3541659651605), -13},
                STAmount{B_BUX, UINT64_C(320'0215509984417), -13},
                ammCarol.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                cam,
                1,
                {{Amounts{
                    STAmount{B_BUX, UINT64_C(20'0215509984417), -13},
                    STAmount{A_BUX, UINT64_C(20'0215509984417), -13}}}}));
        }
        else
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{A_BUX, UINT64_C(309'3541659651604), -13},
                STAmount{B_BUX, UINT64_C(320'0215509984419), -13},
                ammCarol.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                cam,
                1,
                {{Amounts{
                    STAmount{B_BUX, UINT64_C(20'0215509984419), -13},
                    STAmount{A_BUX, UINT64_C(20'0215509984419), -13}}}}));
        }
    }

    void
    testRequireAuth(FeatureBitset features)
    {
        testcase("lsfRequireAuth");

        using namespace jtx;

        Env env{*this, features};

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        env.fund(XRP(400'000), gw, alice, bob);
        env.close();

        // GW requires authorization for holders of its IOUs
        env(fset(gw, asfRequireAuth));
        env.close();

        // Properly set trust and have gw authorize bob and alice
        env(trust(gw, bobUSD(100)), txflags(tfSetfAuth));
        env(trust(bob, USD(100)));
        env(trust(gw, aliceUSD(100)), txflags(tfSetfAuth));
        env(trust(alice, USD(2'000)));
        env(pay(gw, alice, USD(1'000)));
        env.close();
        // Alice is able to create AMM since the GW has authorized her
        AMM ammAlice(env, alice, USD(1'000), XRP(1'050));

        // Set up authorized trust line for AMM.
        env(trust(gw, STAmount{Issue{USD.currency, ammAlice.ammAccount()}, 10}),
            txflags(tfSetfAuth));
        env.close();

        env(pay(gw, bob, USD(50)));
        env.close();

        BEAST_EXPECT(expectLine(env, bob, USD(50)));

        // Bob's offer should cross Alice's AMM
        env(offer(bob, XRP(50), USD(50)));
        env.close();

        BEAST_EXPECT(
            ammAlice.expectBalances(USD(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob, 0));
        BEAST_EXPECT(expectLine(env, bob, USD(0)));
    }

    void
    testMissingAuth(FeatureBitset features)
    {
        testcase("Missing Auth");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(400'000), gw, alice, bob);
        env.close();

        // Alice doesn't have the funds
        {
            AMM ammAlice(
                env, alice, USD(1'000), XRP(1'000), ter(tecUNFUNDED_AMM));
        }

        env(fset(gw, asfRequireAuth));
        env.close();

        env(trust(gw, bob["USD"](50)), txflags(tfSetfAuth));
        env.close();
        env(trust(bob, USD(50)));
        env.close();

        env(pay(gw, bob, USD(50)));
        env.close();
        BEAST_EXPECT(expectLine(env, bob, USD(50)));

        // Alice should not be able to create AMM without authorization.
        {
            AMM ammAlice(env, alice, USD(1'000), XRP(1'000), ter(tecNO_LINE));
        }

        // Set up a trust line for Alice, but don't authorize it. Alice
        // should still not be able to create AMM for USD/gw.
        env(trust(gw, alice["USD"](2'000)));
        env.close();

        {
            AMM ammAlice(env, alice, USD(1'000), XRP(1'000), ter(tecNO_AUTH));
        }

        // Finally, set up an authorized trust line for Alice. Now Alice's
        // AMM create should succeed.
        env(trust(gw, alice["USD"](100)), txflags(tfSetfAuth));
        env(trust(alice, USD(2'000)));
        env(pay(gw, alice, USD(1'000)));
        env.close();

        AMM ammAlice(env, alice, USD(1'000), XRP(1'050));

        // Set up authorized trust line for AMM.
        env(trust(gw, STAmount{Issue{USD.currency, ammAlice.ammAccount()}, 10}),
            txflags(tfSetfAuth));
        env.close();

        // Now bob creates his offer again, which crosses with  alice's AMM.
        env(offer(bob, XRP(50), USD(50)));
        env.close();

        BEAST_EXPECT(
            ammAlice.expectBalances(USD(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob, 0));
        BEAST_EXPECT(expectLine(env, bob, USD(0)));
    }

    void
    testOffers()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        testRmFundedOffer(all);
        testRmFundedOffer(all - fixAMMv1_1);
        testEnforceNoRipple(all);
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
        testGatewayCrossCurrency(all - fixAMMv1_1);
        testBridgedCross(all);
        testSellWithFillOrKill(all);
        testTransferRateOffer(all);
        testSelfIssueOffer(all);
        testBadPathAssert(all);
        testSellFlagBasic(all);
        testDirectToDirectPath(all);
        testDirectToDirectPath(all - fixAMMv1_1);
        testRequireAuth(all);
        testMissingAuth(all);
    }

    void
    path_find_consume_all()
    {
        testcase("path find consume all");
        using namespace jtx;

        Env env = pathTestEnv();
        env.fund(XRP(100'000'250), alice);
        fund(env, gw, {carol, bob}, {USD(100)}, Fund::All);
        fund(env, gw, {alice}, {USD(100)}, Fund::IOUOnly);
        AMM ammCarol(env, carol, XRP(100), USD(100));

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
            bob["USD"](-1),
            std::optional<STAmount>(XRP(100'000'000)));
        // Alice sends all requested 100,000,000XRP
        BEAST_EXPECT(sa == XRP(100'000'000));
        // Bob gets ~99.99USD. This is the amount Bob
        // can get out of AMM for 100,000,000XRP.
        BEAST_EXPECT(equal(
            da, STAmount{bob["USD"].issue(), UINT64_C(99'9999000001), -10}));
    }

    // carol holds gateway AUD, sells gateway AUD for XRP
    // bob will hold gateway AUD
    // alice pays bob gateway AUD using XRP
    void
    via_offers_via_gateway()
    {
        testcase("via gateway");
        using namespace jtx;

        Env env = pathTestEnv();
        auto const AUD = gw["AUD"];
        env.fund(XRP(10'000), alice, bob, carol, gw);
        env(rate(gw, 1.1));
        env.trust(AUD(2'000), bob, carol);
        env(pay(gw, carol, AUD(51)));
        env.close();
        AMM ammCarol(env, carol, XRP(40), AUD(51));
        env(pay(alice, bob, AUD(10)), sendmax(XRP(100)), paths(XRP));
        env.close();
        // AMM offer is 51.282052XRP/11AUD, 11AUD/1.1 = 10AUD to bob
        BEAST_EXPECT(
            ammCarol.expectBalances(XRP(51), AUD(40), ammCarol.tokens()));
        BEAST_EXPECT(expectLine(env, bob, AUD(10)));

        auto const result =
            find_paths(env, alice, bob, Account(bob)["USD"](25));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    receive_max()
    {
        testcase("Receive max");
        using namespace jtx;
        auto const charlie = Account("charlie");
        {
            // XRP -> IOU receive max
            Env env = pathTestEnv();
            fund(env, gw, {alice, bob, charlie}, {USD(11)}, Fund::All);
            AMM ammCharlie(env, charlie, XRP(10), USD(11));
            auto [st, sa, da] =
                find_paths(env, alice, bob, USD(-1), XRP(1).value());
            BEAST_EXPECT(sa == XRP(1));
            BEAST_EXPECT(equal(da, USD(1)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getCurrency() == USD.currency);
            }
        }
        {
            // IOU -> XRP receive max
            Env env = pathTestEnv();
            fund(env, gw, {alice, bob, charlie}, {USD(11)}, Fund::All);
            AMM ammCharlie(env, charlie, XRP(11), USD(10));
            env.close();
            auto [st, sa, da] =
                find_paths(env, alice, bob, drops(-1), USD(1).value());
            BEAST_EXPECT(sa == USD(1));
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
        testcase("Path Find: XRP -> XRP and XRP -> IOU");
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

        env.trust(G1["XYZ"](5'000), A1);
        env.trust(G3["ABC"](5'000), A1);
        env.trust(G2["XYZ"](5'000), A2);
        env.trust(G3["ABC"](5'000), A2);
        env.trust(A2["ABC"](1'000), A3);
        env.trust(G1["XYZ"](100'000), M1);
        env.trust(G2["XYZ"](100'000), M1);
        env.trust(G3["ABC"](100'000), M1);
        env.close();

        env(pay(G1, A1, G1["XYZ"](3'500)));
        env(pay(G3, A1, G3["ABC"](1'200)));
        env(pay(G1, M1, G1["XYZ"](25'000)));
        env(pay(G2, M1, G2["XYZ"](25'000)));
        env(pay(G3, M1, G3["ABC"](25'000)));
        env.close();

        AMM ammM1_G1_G2(env, M1, G1["XYZ"](1'000), G2["XYZ"](1'000));
        AMM ammM1_XRP_G3(env, M1, XRP(10'000), G3["ABC"](1'000));

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
            auto const& send_amt = G3["ABC"](10);
            std::tie(st, sa, da) =
                find_paths(env, A2, G3, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{101'010'102}));
            BEAST_EXPECT(same(st, stpath(IPE(G3["ABC"]))));
        }

        {
            auto const& send_amt = A2["ABC"](1);
            std::tie(st, sa, da) =
                find_paths(env, A1, A2, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{10'010'011}));
            BEAST_EXPECT(same(st, stpath(IPE(G3["ABC"]), G3)));
        }

        {
            auto const& send_amt = A3["ABC"](1);
            std::tie(st, sa, da) =
                find_paths(env, A1, A3, send_amt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, XRPAmount{10'010'011}));
            BEAST_EXPECT(same(st, stpath(IPE(G3["ABC"]), G3, A2)));
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

        env.trust(G3["ABC"](1'000), A1, A2);
        env.trust(G3["ABC"](100'000), M1);
        env.close();

        env(pay(G3, A1, G3["ABC"](1'000)));
        env(pay(G3, A2, G3["ABC"](1'000)));
        env(pay(G3, M1, G3["ABC"](1'200)));
        env.close();

        AMM ammM1(env, M1, G3["ABC"](1'000), XRP(10'010));

        STPathSet st;
        STAmount sa, da;

        auto const& send_amt = XRP(10);
        std::tie(st, sa, da) =
            find_paths(env, A1, A2, send_amt, std::nullopt, A2["ABC"].currency);
        BEAST_EXPECT(equal(da, send_amt));
        BEAST_EXPECT(equal(sa, A1["ABC"](1)));
        BEAST_EXPECT(same(st, stpath(G3, IPE(xrpIssue()))));
    }

    void
    path_find_05()
    {
        testcase("Path Find: non-XRP -> non-XRP, same currency");
        using namespace jtx;
        Env env = pathTestEnv();
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};
        Account G1{"G1"};
        Account G2{"G2"};
        Account G3{"G3"};
        Account G4{"G4"};
        Account M1{"M1"};
        Account M2{"M2"};

        env.fund(XRP(1'000), A1, A2, A3, G1, G2, G3, G4);
        env.fund(XRP(10'000), A4);
        env.fund(XRP(21'000), M1, M2);
        env.close();

        env.trust(G1["HKD"](2'000), A1);
        env.trust(G2["HKD"](2'000), A2);
        env.trust(G1["HKD"](2'000), A3);
        env.trust(G1["HKD"](100'000), M1);
        env.trust(G2["HKD"](100'000), M1);
        env.trust(G1["HKD"](100'000), M2);
        env.trust(G2["HKD"](100'000), M2);
        env.close();

        env(pay(G1, A1, G1["HKD"](1'000)));
        env(pay(G2, A2, G2["HKD"](1'000)));
        env(pay(G1, A3, G1["HKD"](1'000)));
        env(pay(G1, M1, G1["HKD"](1'200)));
        env(pay(G2, M1, G2["HKD"](5'000)));
        env(pay(G1, M2, G1["HKD"](1'200)));
        env(pay(G2, M2, G2["HKD"](5'000)));
        env.close();

        AMM ammM1(env, M1, G1["HKD"](1'010), G2["HKD"](1'000));
        AMM ammM2XRP_G2(env, M2, XRP(10'000), G2["HKD"](1'010));
        AMM ammM2G1_XRP(env, M2, G1["HKD"](1'010), XRP(10'000));

        STPathSet st;
        STAmount sa, da;

        {
            // A) Borrow or repay --
            //  Source -> Destination (repay source issuer)
            auto const& send_amt = G1["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, G1, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
        }

        {
            // A2) Borrow or repay --
            //  Source -> Destination (repay destination issuer)
            auto const& send_amt = A1["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, G1, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
        }

        {
            // B) Common gateway --
            //  Source -> AC -> Destination
            auto const& send_amt = A3["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, A3, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(G1)));
        }

        {
            // C) Gateway to gateway --
            //  Source -> OB -> Destination
            auto const& send_amt = G2["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, G1, G2, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, G1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(IPE(G2["HKD"])),
                stpath(M1),
                stpath(M2),
                stpath(IPE(xrpIssue()), IPE(G2["HKD"]))));
        }

        {
            // D) User to unlinked gateway via order book --
            //  Source -> AC -> OB -> Destination
            auto const& send_amt = G2["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, G2, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(G1, M1),
                stpath(G1, M2),
                stpath(G1, IPE(G2["HKD"])),
                stpath(G1, IPE(xrpIssue()), IPE(G2["HKD"]))));
        }

        {
            // I4) XRP bridge" --
            //  Source -> AC -> OB to XRP -> OB from XRP -> AC ->
            //  Destination
            auto const& send_amt = A2["HKD"](10);
            std::tie(st, sa, da) = find_paths(
                env, A1, A2, send_amt, std::nullopt, G1["HKD"].currency);
            BEAST_EXPECT(equal(da, send_amt));
            BEAST_EXPECT(equal(sa, A1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(G1, M1, G2),
                stpath(G1, M2, G2),
                stpath(G1, IPE(G2["HKD"]), G2),
                stpath(G1, IPE(xrpIssue()), IPE(G2["HKD"]), G2)));
        }
    }

    void
    path_find_06()
    {
        testcase("Path Find: non-XRP -> non-XRP, same currency");
        using namespace jtx;
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

        env.trust(G1["HKD"](2'000), A1);
        env.trust(G2["HKD"](2'000), A2);
        env.trust(A2["HKD"](2'000), A3);
        env.trust(G1["HKD"](100'000), M1);
        env.trust(G2["HKD"](100'000), M1);
        env.close();

        env(pay(G1, A1, G1["HKD"](1'000)));
        env(pay(G2, A2, G2["HKD"](1'000)));
        env(pay(G1, M1, G1["HKD"](5'000)));
        env(pay(G2, M1, G2["HKD"](5'000)));
        env.close();

        AMM ammM1(env, M1, G1["HKD"](1'010), G2["HKD"](1'000));

        // E) Gateway to user
        //  Source -> OB -> AC -> Destination
        auto const& send_amt = A2["HKD"](10);
        STPathSet st;
        STAmount sa, da;
        std::tie(st, sa, da) =
            find_paths(env, G1, A2, send_amt, std::nullopt, G1["HKD"].currency);
        BEAST_EXPECT(equal(da, send_amt));
        BEAST_EXPECT(equal(sa, G1["HKD"](10)));
        BEAST_EXPECT(same(st, stpath(M1, G2), stpath(IPE(G2["HKD"]), G2)));
    }

    void
    testFalseDry(FeatureBitset features)
    {
        testcase("falseDryChanges");

        using namespace jtx;

        Env env(*this, features);

        env.fund(XRP(10'000), alice, gw);
        // This removes no ripple for carol,
        // different from the original test
        fund(env, gw, {carol}, XRP(10'000), {}, Fund::Acct);
        auto const AMMXRPPool = env.current()->fees().increment * 2;
        env.fund(reserve(env, 5) + ammCrtFee(env) + AMMXRPPool, bob);
        env.trust(USD(1'000), alice, bob, carol);
        env.trust(EUR(1'000), alice, bob, carol);

        env(pay(gw, alice, EUR(50)));
        env(pay(gw, bob, USD(150)));

        // Bob has _just_ slightly less than 50 xrp available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incoming xrp to the XRP/USD offer will require two
        // recursive calls to the EUR/XRP offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry.
        // This is the second error case to test (when flowV1 is used).
        env(offer(bob, EUR(50), XRP(50)));
        AMM ammBob(env, bob, AMMXRPPool, USD(150));

        env(pay(alice, carol, USD(1'000'000)),
            path(~XRP, ~USD),
            sendmax(EUR(500)),
            txflags(tfNoRippleDirect | tfPartialPayment));

        auto const carolUSD = env.balance(carol, USD).value();
        BEAST_EXPECT(carolUSD > USD(0) && carolUSD < USD(50));
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("Book Step");

        using namespace jtx;

        {
            // simple IOU/IOU offer
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(10'000),
                {BTC(100), USD(150)},
                Fund::All);

            AMM ammBob(env, bob, BTC(100), USD(150));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(BTC(50)));

            BEAST_EXPECT(expectLine(env, alice, BTC(50)));
            BEAST_EXPECT(expectLine(env, bob, BTC(0)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, USD(200)));
            BEAST_EXPECT(
                ammBob.expectBalances(BTC(150), USD(100), ammBob.tokens()));
        }
        {
            // simple IOU/XRP XRP/IOU offer
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(10'000),
                {BTC(100), USD(150)},
                Fund::All);

            AMM ammBobBTC_XRP(env, bob, BTC(100), XRP(150));
            AMM ammBobXRP_USD(env, bob, XRP(100), USD(150));

            env(pay(alice, carol, USD(50)), path(~XRP, ~USD), sendmax(BTC(50)));

            BEAST_EXPECT(expectLine(env, alice, BTC(50)));
            BEAST_EXPECT(expectLine(env, bob, BTC(0)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, USD(200)));
            BEAST_EXPECT(ammBobBTC_XRP.expectBalances(
                BTC(150), XRP(100), ammBobBTC_XRP.tokens()));
            BEAST_EXPECT(ammBobXRP_USD.expectBalances(
                XRP(150), USD(100), ammBobXRP_USD.tokens()));
        }
        {
            // simple XRP -> USD through offer and sendmax
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(10'000),
                {USD(150)},
                Fund::All);

            AMM ammBob(env, bob, XRP(100), USD(150));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(XRP(50)));

            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10'000 - 50)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(10'000) - XRP(100) - ammCrtFee(env)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLine(env, carol, USD(200)));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(150), USD(100), ammBob.tokens()));
        }
        {
            // simple USD -> XRP through offer and sendmax
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, carol, bob},
                XRP(10'000),
                {USD(100)},
                Fund::All);

            AMM ammBob(env, bob, USD(100), XRP(150));

            env(pay(alice, carol, XRP(50)), path(~XRP), sendmax(USD(50)));

            BEAST_EXPECT(expectLine(env, alice, USD(50)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(10'000) - XRP(150) - ammCrtFee(env)));
            BEAST_EXPECT(expectLine(env, bob, USD(0)));
            BEAST_EXPECT(expectLedgerEntryRoot(env, carol, XRP(10'000 + 50)));
            BEAST_EXPECT(
                ammBob.expectBalances(USD(150), XRP(100), ammBob.tokens()));
        }
        {
            // test unfunded offers are removed when payment succeeds
            Env env(*this, features);

            env.fund(XRP(10'000), alice, carol, gw);
            env.fund(XRP(10'000), bob);
            env.trust(USD(1'000), alice, bob, carol);
            env.trust(BTC(1'000), alice, bob, carol);
            env.trust(EUR(1'000), alice, bob, carol);

            env(pay(gw, alice, BTC(60)));
            env(pay(gw, bob, USD(200)));
            env(pay(gw, bob, EUR(150)));

            env(offer(bob, BTC(50), USD(50)));
            env(offer(bob, BTC(40), EUR(50)));
            AMM ammBob(env, bob, EUR(100), USD(150));

            // unfund offer
            env(pay(bob, gw, EUR(50)));
            BEAST_EXPECT(isOffer(env, bob, BTC(50), USD(50)));
            BEAST_EXPECT(isOffer(env, bob, BTC(40), EUR(50)));

            env(pay(alice, carol, USD(50)),
                path(~USD),
                path(~EUR, ~USD),
                sendmax(BTC(60)));

            env.require(balance(alice, BTC(10)));
            env.require(balance(bob, BTC(50)));
            env.require(balance(bob, USD(0)));
            env.require(balance(bob, EUR(0)));
            env.require(balance(carol, USD(50)));
            // used in the payment
            BEAST_EXPECT(!isOffer(env, bob, BTC(50), USD(50)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, BTC(40), EUR(50)));
            // unchanged
            BEAST_EXPECT(
                ammBob.expectBalances(EUR(100), USD(150), ammBob.tokens()));
        }
        {
            // test unfunded offers are removed when the payment fails.
            // bob makes two offers: a funded 50 USD for 50 BTC and an
            // unfunded 50 EUR for 60 BTC. alice pays carol 61 USD with 61
            // BTC. alice only has 60 BTC, so the payment will fail. The
            // payment uses two paths: one through bob's funded offer and
            // one through his unfunded offer. When the payment fails `flow`
            // should return the unfunded offer. This test is intentionally
            // similar to the one that removes unfunded offers when the
            // payment succeeds.
            Env env(*this, features);

            env.fund(XRP(10'000), bob, carol, gw);
            // Sets rippling on, this is different from
            // the original test
            fund(env, gw, {alice}, XRP(10'000), {}, Fund::Acct);
            env.trust(USD(1'000), alice, bob, carol);
            env.trust(BTC(1'000), alice, bob, carol);
            env.trust(EUR(1'000), alice, bob, carol);

            env(pay(gw, alice, BTC(60)));
            env(pay(gw, bob, BTC(100)));
            env(pay(gw, bob, USD(100)));
            env(pay(gw, bob, EUR(50)));
            env(pay(gw, carol, EUR(1)));

            // This is multiplath, which generates limited # of offers
            AMM ammBobBTC_USD(env, bob, BTC(50), USD(50));
            env(offer(bob, BTC(60), EUR(50)));
            env(offer(carol, BTC(1'000), EUR(1)));
            env(offer(bob, EUR(50), USD(50)));

            // unfund offer
            env(pay(bob, gw, EUR(50)));
            BEAST_EXPECT(ammBobBTC_USD.expectBalances(
                BTC(50), USD(50), ammBobBTC_USD.tokens()));
            BEAST_EXPECT(isOffer(env, bob, BTC(60), EUR(50)));
            BEAST_EXPECT(isOffer(env, carol, BTC(1'000), EUR(1)));
            BEAST_EXPECT(isOffer(env, bob, EUR(50), USD(50)));

            auto flowJournal = env.app().logs().journal("Flow");
            auto const flowResult = [&] {
                STAmount deliver(USD(51));
                STAmount smax(BTC(61));
                PaymentSandbox sb(env.current().get(), tapNONE);
                STPathSet paths;
                auto IPE = [](Issue const& iss) {
                    return STPathElement(
                        STPathElement::typeCurrency | STPathElement::typeIssuer,
                        xrpAccount(),
                        iss.currency,
                        iss.account);
                };
                {
                    // BTC -> USD
                    STPath p1({IPE(USD.issue())});
                    paths.push_back(p1);
                    // BTC -> EUR -> USD
                    STPath p2({IPE(EUR.issue()), IPE(USD.issue())});
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
            BEAST_EXPECT(ammBobBTC_USD.expectBalances(
                BTC(50), USD(50), ammBobBTC_USD.tokens()));
            BEAST_EXPECT(isOffer(env, carol, BTC(1'000), EUR(1)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, BTC(60), EUR(50)));
        }
        {
            // Do not produce more in the forward pass than the reverse pass
            // This test uses a path that whose reverse pass will compute a
            // 0.5 USD input required for a 1 EUR output. It sets a sendmax
            // of 0.4 USD, so the payment engine will need to do a forward
            // pass. Without limits, the 0.4 USD would produce 1000 EUR in
            // the forward pass. This test checks that the payment produces
            // 1 EUR, as expected.

            Env env(*this, features);
            env.fund(XRP(10'000), bob, carol, gw);
            fund(env, gw, {alice}, XRP(10'000), {}, Fund::Acct);
            env.trust(USD(1'000), alice, bob, carol);
            env.trust(EUR(1'000), alice, bob, carol);

            env(pay(gw, alice, USD(1'000)));
            env(pay(gw, bob, EUR(1'000)));
            env(pay(gw, bob, USD(1'000)));

            // env(offer(bob, USD(1), drops(2)), txflags(tfPassive));
            AMM ammBob(env, bob, USD(8), XRPAmount{21});
            env(offer(bob, drops(1), EUR(1'000)), txflags(tfPassive));

            env(pay(alice, carol, EUR(1)),
                path(~XRP, ~EUR),
                sendmax(USD(0.4)),
                txflags(tfNoRippleDirect | tfPartialPayment));

            BEAST_EXPECT(expectLine(env, carol, EUR(1)));
            BEAST_EXPECT(ammBob.expectBalances(
                USD(8.4), XRPAmount{20}, ammBob.tokens()));
        }
    }

    void
    testTransferRate(FeatureBitset features)
    {
        testcase("Transfer Rate");

        using namespace jtx;

        {
            // transfer fee on AMM
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10'000), {USD(1'000)});
            env(rate(gw, 1.25));
            env.close();

            AMM ammBob(env, bob, XRP(100), USD(150));
            // no transfer fee on create
            BEAST_EXPECT(expectLine(env, bob, USD(1000 - 150)));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(XRP(50)));
            env.close();

            BEAST_EXPECT(expectLine(env, bob, USD(1'000 - 150)));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(150), USD(100), ammBob.tokens()));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10'000 - 50)));
            BEAST_EXPECT(expectLine(env, carol, USD(1'050)));
        }

        {
            // Transfer fee AMM and offer
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(10'000),
                {USD(1'000), EUR(1'000)});
            env(rate(gw, 1.25));
            env.close();

            AMM ammBob(env, bob, XRP(100), USD(140));
            BEAST_EXPECT(expectLine(env, bob, USD(1'000 - 140)));

            env(offer(bob, USD(50), EUR(50)));

            // alice buys 40EUR with 40XRP
            env(pay(alice, carol, EUR(40)), path(~USD, ~EUR), sendmax(XRP(40)));

            // 40XRP is swapped in for 40USD
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(140), USD(100), ammBob.tokens()));
            // 40USD buys 40EUR via bob's offer. 40EUR delivered to carol
            // and bob pays 25% on 40EUR, 40EUR*0.25=10EUR
            BEAST_EXPECT(expectLine(env, bob, EUR(1'000 - 40 - 40 * 0.25)));
            // bob gets 40USD back from the offer
            BEAST_EXPECT(expectLine(env, bob, USD(1'000 - 140 + 40)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10'000 - 40)));
            BEAST_EXPECT(expectLine(env, carol, EUR(1'040)));
            BEAST_EXPECT(expectOffers(env, bob, 1, {{USD(10), EUR(10)}}));
        }

        {
            // Transfer fee two consecutive AMM
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(10'000),
                {USD(1'000), EUR(1'000)});
            env(rate(gw, 1.25));
            env.close();

            AMM ammBobXRP_USD(env, bob, XRP(100), USD(140));
            BEAST_EXPECT(expectLine(env, bob, USD(1'000 - 140)));

            AMM ammBobUSD_EUR(env, bob, USD(100), EUR(140));
            BEAST_EXPECT(expectLine(env, bob, EUR(1'000 - 140)));
            BEAST_EXPECT(expectLine(env, bob, USD(1'000 - 140 - 100)));

            // alice buys 40EUR with 40XRP
            env(pay(alice, carol, EUR(40)), path(~USD, ~EUR), sendmax(XRP(40)));

            // 40XRP is swapped in for 40USD
            BEAST_EXPECT(ammBobXRP_USD.expectBalances(
                XRP(140), USD(100), ammBobXRP_USD.tokens()));
            // 40USD is swapped in for 40EUR
            BEAST_EXPECT(ammBobUSD_EUR.expectBalances(
                USD(140), EUR(100), ammBobUSD_EUR.tokens()));
            // no other charges on bob
            BEAST_EXPECT(expectLine(env, bob, USD(1'000 - 140 - 100)));
            BEAST_EXPECT(expectLine(env, bob, EUR(1'000 - 140)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, alice, xrpMinusFee(env, 10'000 - 40)));
            BEAST_EXPECT(expectLine(env, carol, EUR(1'040)));
        }

        {
            // Payment via AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(1'000),
                {USD(1'200), GBP(1'200)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm(env, bob, GBP(1'000), USD(1'100));

            // requested quality limit is 90USD/110GBP = 0.8181
            // trade quality is 77.2727USD/94.4444GBP = 0.8181
            env(pay(alice, carol, USD(90)),
                path(~USD),
                sendmax(GBP(110)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice buys 77.2727USD with 75.5555GBP and pays 25% tr fee
                // on 75.5555GBP
                // 1,200 - 75.55555*1.25 = 1200 - 94.4444 = 1105.55555GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'105'555555555555), -12}));
                // 75.5555GBP is swapped in for 77.7272USD
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{GBP, UINT64_C(1'075'555555555556), -12},
                    STAmount{USD, UINT64_C(1'022'727272727272), -12},
                    amm.tokens()));
            }
            else
            {
                // alice buys 77.2727USD with 75.5555GBP and pays 25% tr fee
                // on 75.5555GBP
                // 1,200 - 75.55555*1.25 = 1200 - 94.4444 = 1105.55555GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'105'555555555554), -12}));
                // 75.5555GBP is swapped in for 77.7272USD
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{GBP, UINT64_C(1'075'555555555557), -12},
                    STAmount{USD, UINT64_C(1'022'727272727272), -12},
                    amm.tokens()));
            }
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(1'277'272727272728), -12}));
        }

        {
            // AMM offer crossing
            Env env(*this, features);

            fund(env, gw, {alice, bob}, XRP(1'000), {USD(1'200), EUR(1'200)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm(env, bob, USD(1'000), EUR(1'150));

            env(offer(alice, EUR(100), USD(100)));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // 95.2380USD is swapped in for 100EUR
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{USD, UINT64_C(1'095'238095238095), -12},
                    EUR(1'050),
                    amm.tokens()));
                // alice pays 25% tr fee on 95.2380USD
                // 1200-95.2380*1.25 = 1200 - 119.0477 = 1080.9523USD
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{USD, UINT64_C(1'080'952380952381), -12},
                    EUR(1'300)));
            }
            else
            {
                // 95.2380USD is swapped in for 100EUR
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{USD, UINT64_C(1'095'238095238096), -12},
                    EUR(1'050),
                    amm.tokens()));
                // alice pays 25% tr fee on 95.2380USD
                // 1200-95.2380*1.25 = 1200 - 119.0477 = 1080.9523USD
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{USD, UINT64_C(1'080'95238095238), -11},
                    EUR(1'300)));
            }
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }

        {
            // First pass through a strand redeems, second pass issues,
            // through an offer limiting step is not an endpoint
            Env env(*this, features);
            auto const USDA = alice["USD"];
            auto const USDB = bob["USD"];
            Account const dan("dan");

            env.fund(XRP(10'000), bob, carol, dan, gw);
            fund(env, {alice}, XRP(10'000));
            env(rate(gw, 1.25));
            env.trust(USD(2'000), alice, bob, carol, dan);
            env.trust(EUR(2'000), carol, dan);
            env.trust(USDA(1'000), bob);
            env.trust(USDB(1'000), gw);
            env(pay(gw, bob, USD(50)));
            env(pay(gw, dan, EUR(1'050)));
            env(pay(gw, dan, USD(1'000)));
            AMM ammDan(env, dan, USD(1'000), EUR(1'050));

            if (!features[fixAMMv1_1])
            {
                // alice -> bob -> gw -> carol. $50 should have transfer fee;
                // $10, no fee
                env(pay(alice, carol, EUR(50)),
                    path(bob, gw, ~EUR),
                    sendmax(USDA(60)),
                    txflags(tfNoRippleDirect));
                BEAST_EXPECT(ammDan.expectBalances(
                    USD(1'050), EUR(1'000), ammDan.tokens()));
                BEAST_EXPECT(expectLine(env, dan, USD(0)));
                BEAST_EXPECT(expectLine(env, dan, EUR(0)));
                BEAST_EXPECT(expectLine(env, bob, USD(-10)));
                BEAST_EXPECT(expectLine(env, bob, USDA(60)));
                BEAST_EXPECT(expectLine(env, carol, EUR(50)));
            }
            else
            {
                // alice -> bob -> gw -> carol. $50 should have transfer fee;
                // $10, no fee
                env(pay(alice, carol, EUR(50)),
                    path(bob, gw, ~EUR),
                    sendmax(USDA(60.1)),
                    txflags(tfNoRippleDirect));
                BEAST_EXPECT(ammDan.expectBalances(
                    STAmount{USD, UINT64_C(1'050'000000000001), -12},
                    EUR(1'000),
                    ammDan.tokens()));
                BEAST_EXPECT(expectLine(env, dan, USD(0)));
                BEAST_EXPECT(expectLine(env, dan, EUR(0)));
                BEAST_EXPECT(expectLine(
                    env, bob, STAmount{USD, INT64_C(-10'000000000001), -12}));
                BEAST_EXPECT(expectLine(
                    env, bob, STAmount{USDA, UINT64_C(60'000000000001), -12}));
                BEAST_EXPECT(expectLine(env, carol, EUR(50)));
            }
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

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(1'000),
                {USD(1'000), GBP(1'000)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm(env, bob, GBP(1'000), USD(1'000));

            env(pay(alice, carol, USD(100)),
                path(~USD),
                sendmax(GBP(150)),
                txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice buys 107.1428USD with 120GBP and pays 25% tr fee on 120GBP
            // 1,000 - 120*1.25 = 850GBP
            BEAST_EXPECT(expectLine(env, alice, GBP(850)));
            if (!features[fixAMMv1_1])
            {
                // 120GBP is swapped in for 107.1428USD
                BEAST_EXPECT(amm.expectBalances(
                    GBP(1'120),
                    STAmount{USD, UINT64_C(892'8571428571428), -13},
                    amm.tokens()));
            }
            else
            {
                BEAST_EXPECT(amm.expectBalances(
                    GBP(1'120),
                    STAmount{USD, UINT64_C(892'8571428571429), -13},
                    amm.tokens()));
            }
            // 25% of 85.7142USD is paid in tr fee
            // 85.7142*1.25 = 107.1428USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount(USD, UINT64_C(1'085'714285714286), -12)));
        }

        {
            // Payment via offer and AMM
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(1'000), EUR(1'000), GBP(1'000)});
            env(rate(gw, 1.25));
            env.close();

            env(offer(ed, GBP(1'000), EUR(1'000)), txflags(tfPassive));
            env.close();

            AMM amm(env, bob, EUR(1'000), USD(1'000));

            env(pay(alice, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(GBP(150)),
                txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice buys 120EUR with 120GBP via the offer
            // and pays 25% tr fee on 120GBP
            // 1,000 - 120*1.25 = 850GBP
            BEAST_EXPECT(expectLine(env, alice, GBP(850)));
            // consumed offer is 120GBP/120EUR
            // ed doesn't pay tr fee
            BEAST_EXPECT(expectLine(env, ed, EUR(880), GBP(1'120)));
            BEAST_EXPECT(
                expectOffers(env, ed, 1, {Amounts{GBP(880), EUR(880)}}));
            // 25% on 96EUR is paid in tr fee 96*1.25 = 120EUR
            // 96EUR is swapped in for 87.5912USD
            BEAST_EXPECT(amm.expectBalances(
                EUR(1'096),
                STAmount{USD, UINT64_C(912'4087591240876), -13},
                amm.tokens()));
            // 25% on 70.0729USD is paid in tr fee 70.0729*1.25 = 87.5912USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount(USD, UINT64_C(1'070'07299270073), -11)));
        }
        {
            // Payment via AMM, AMM
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(1'000), EUR(1'000), GBP(1'000)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm1(env, bob, GBP(1'000), EUR(1'000));
            AMM amm2(env, ed, EUR(1'000), USD(1'000));

            env(pay(alice, carol, USD(100)),
                path(~EUR, ~USD),
                sendmax(GBP(150)),
                txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectLine(env, alice, GBP(850)));
            if (!features[fixAMMv1_1])
            {
                // alice buys 107.1428EUR with 120GBP and pays 25% tr fee on
                // 120GBP 1,000 - 120*1.25 = 850GBP 120GBP is swapped in for
                // 107.1428EUR
                BEAST_EXPECT(amm1.expectBalances(
                    GBP(1'120),
                    STAmount{EUR, UINT64_C(892'8571428571428), -13},
                    amm1.tokens()));
                // 25% on 85.7142EUR is paid in tr fee 85.7142*1.25 =
                // 107.1428EUR 85.7142EUR is swapped in for 78.9473USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount(EUR, UINT64_C(1'085'714285714286), -12),
                    STAmount{USD, UINT64_C(921'0526315789471), -13},
                    amm2.tokens()));
            }
            else
            {
                // alice buys 107.1428EUR with 120GBP and pays 25% tr fee on
                // 120GBP 1,000 - 120*1.25 = 850GBP 120GBP is swapped in for
                // 107.1428EUR
                BEAST_EXPECT(amm1.expectBalances(
                    GBP(1'120),
                    STAmount{EUR, UINT64_C(892'8571428571429), -13},
                    amm1.tokens()));
                // 25% on 85.7142EUR is paid in tr fee 85.7142*1.25 =
                // 107.1428EUR 85.7142EUR is swapped in for 78.9473USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount(EUR, UINT64_C(1'085'714285714286), -12),
                    STAmount{USD, UINT64_C(921'052631578948), -12},
                    amm2.tokens()));
            }
            // 25% on 63.1578USD is paid in tr fee 63.1578*1.25 = 78.9473USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount(USD, UINT64_C(1'063'157894736842), -12)));
        }
        {
            // AMM offer crossing
            Env env(*this, features);

            fund(env, gw, {alice, bob}, XRP(1'000), {USD(1'100), EUR(1'100)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm(env, bob, USD(1'000), EUR(1'100));
            env(offer(alice, EUR(100), USD(100)));
            env.close();

            // 100USD is swapped in for 100EUR
            BEAST_EXPECT(
                amm.expectBalances(USD(1'100), EUR(1'000), amm.tokens()));
            // alice pays 25% tr fee on 100USD 1100-100*1.25 = 975USD
            BEAST_EXPECT(expectLine(env, alice, USD(975), EUR(1'200)));
            BEAST_EXPECT(expectOffers(env, alice, 0));
        }

        {
            // Payment via AMM with limit quality
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(1'000),
                {USD(1'000), GBP(1'000)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm(env, bob, GBP(1'000), USD(1'000));

            // requested quality limit is 100USD/178.58GBP = 0.55997
            // trade quality is 100USD/178.5714 = 0.55999
            env(pay(alice, carol, USD(100)),
                path(~USD),
                sendmax(GBP(178.58)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 125USD with 142.8571GBP and pays 25% tr fee
            // on 142.8571GBP
            // 1,000 - 142.8571*1.25 = 821.4285GBP
            BEAST_EXPECT(expectLine(
                env, alice, STAmount(GBP, UINT64_C(821'4285714285712), -13)));
            // 142.8571GBP is swapped in for 125USD
            BEAST_EXPECT(amm.expectBalances(
                STAmount{GBP, UINT64_C(1'142'857142857143), -12},
                USD(875),
                amm.tokens()));
            // 25% on 100USD is paid in tr fee
            // 100*1.25 = 125USD
            BEAST_EXPECT(expectLine(env, carol, USD(1'100)));
        }
        {
            // Payment via AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(1'000),
                {USD(1'200), GBP(1'200)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm(env, bob, GBP(1'000), USD(1'200));

            // requested quality limit is 90USD/120GBP = 0.75
            // trade quality is 22.5USD/30GBP = 0.75
            env(pay(alice, carol, USD(90)),
                path(~USD),
                sendmax(GBP(120)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice buys 28.125USD with 24GBP and pays 25% tr fee
                // on 24GBP
                // 1,200 - 24*1.25 = 1,170GBP
                BEAST_EXPECT(expectLine(env, alice, GBP(1'170)));
                // 24GBP is swapped in for 28.125USD
                BEAST_EXPECT(amm.expectBalances(
                    GBP(1'024), USD(1'171.875), amm.tokens()));
            }
            else
            {
                // alice buys 28.125USD with 24GBP and pays 25% tr fee
                // on 24GBP
                // 1,200 - 24*1.25 =~ 1,170GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'169'999999999999), -12}));
                // 24GBP is swapped in for 28.125USD
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{GBP, UINT64_C(1'024'000000000001), -12},
                    USD(1'171.875),
                    amm.tokens()));
            }
            // 25% on 22.5USD is paid in tr fee
            // 22.5*1.25 = 28.125USD
            BEAST_EXPECT(expectLine(env, carol, USD(1'222.5)));
        }
        {
            // Payment via offer and AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw, 1.25));
            env.close();

            env(offer(ed, GBP(1'000), EUR(1'000)), txflags(tfPassive));
            env.close();

            AMM amm(env, bob, EUR(1'000), USD(1'400));

            // requested quality limit is 95USD/140GBP = 0.6785
            // trade quality is 59.7321USD/88.0262GBP = 0.6785
            env(pay(alice, carol, USD(95)),
                path(~EUR, ~USD),
                sendmax(GBP(140)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice buys 70.4210EUR with 70.4210GBP via the offer
                // and pays 25% tr fee on 70.4210GBP
                // 1,400 - 70.4210*1.25 = 1400 - 88.0262 = 1311.9736GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'311'973684210527), -12}));
                // ed doesn't pay tr fee, the balances reflect consumed offer
                // 70.4210GBP/70.4210EUR
                BEAST_EXPECT(expectLine(
                    env,
                    ed,
                    STAmount{EUR, UINT64_C(1'329'578947368421), -12},
                    STAmount{GBP, UINT64_C(1'470'421052631579), -12}));
                BEAST_EXPECT(expectOffers(
                    env,
                    ed,
                    1,
                    {Amounts{
                        STAmount{GBP, UINT64_C(929'5789473684212), -13},
                        STAmount{EUR, UINT64_C(929'5789473684212), -13}}}));
                // 25% on 56.3368EUR is paid in tr fee 56.3368*1.25 = 70.4210EUR
                // 56.3368EUR is swapped in for 74.6651USD
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{EUR, UINT64_C(1'056'336842105263), -12},
                    STAmount{USD, UINT64_C(1'325'334821428571), -12},
                    amm.tokens()));
            }
            else
            {
                // alice buys 70.4210EUR with 70.4210GBP via the offer
                // and pays 25% tr fee on 70.4210GBP
                // 1,400 - 70.4210*1.25 = 1400 - 88.0262 = 1311.9736GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'311'973684210525), -12}));
                // ed doesn't pay tr fee, the balances reflect consumed offer
                // 70.4210GBP/70.4210EUR
                BEAST_EXPECT(expectLine(
                    env,
                    ed,
                    STAmount{EUR, UINT64_C(1'329'57894736842), -11},
                    STAmount{GBP, UINT64_C(1'470'42105263158), -11}));
                BEAST_EXPECT(expectOffers(
                    env,
                    ed,
                    1,
                    {Amounts{
                        STAmount{GBP, UINT64_C(929'57894736842), -11},
                        STAmount{EUR, UINT64_C(929'57894736842), -11}}}));
                // 25% on 56.3368EUR is paid in tr fee 56.3368*1.25 = 70.4210EUR
                // 56.3368EUR is swapped in for 74.6651USD
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{EUR, UINT64_C(1'056'336842105264), -12},
                    STAmount{USD, UINT64_C(1'325'334821428571), -12},
                    amm.tokens()));
            }
            // 25% on 59.7321USD is paid in tr fee 59.7321*1.25 = 74.6651USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount(USD, UINT64_C(1'459'732142857143), -12)));
        }
        {
            // Payment via AMM and offer with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm(env, bob, GBP(1'000), EUR(1'000));

            env(offer(ed, EUR(1'000), USD(1'400)), txflags(tfPassive));
            env.close();

            // requested quality limit is 95USD/140GBP = 0.6785
            // trade quality is 47.7857USD/70.4210GBP = 0.6785
            env(pay(alice, carol, USD(95)),
                path(~EUR, ~USD),
                sendmax(GBP(140)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice buys 53.3322EUR with 56.3368GBP via the amm
                // and pays 25% tr fee on 56.3368GBP
                // 1,400 - 56.3368*1.25 = 1400 - 70.4210 = 1329.5789GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'329'578947368421), -12}));
                //// 25% on 56.3368EUR is paid in tr fee 56.3368*1.25
                ///= 70.4210EUR
                // 56.3368GBP is swapped in for 53.3322EUR
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{GBP, UINT64_C(1'056'336842105263), -12},
                    STAmount{EUR, UINT64_C(946'6677295918366), -13},
                    amm.tokens()));
            }
            else
            {
                // alice buys 53.3322EUR with 56.3368GBP via the amm
                // and pays 25% tr fee on 56.3368GBP
                // 1,400 - 56.3368*1.25 = 1400 - 70.4210 = 1329.5789GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'329'57894736842), -11}));
                //// 25% on 56.3368EUR is paid in tr fee 56.3368*1.25
                ///= 70.4210EUR
                // 56.3368GBP is swapped in for 53.3322EUR
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{GBP, UINT64_C(1'056'336842105264), -12},
                    STAmount{EUR, UINT64_C(946'6677295918366), -13},
                    amm.tokens()));
            }
            // 25% on 42.6658EUR is paid in tr fee 42.6658*1.25 = 53.3322EUR
            // 42.6658EUR/59.7321USD
            BEAST_EXPECT(expectLine(
                env,
                ed,
                STAmount{USD, UINT64_C(1'340'267857142857), -12},
                STAmount{EUR, UINT64_C(1'442'665816326531), -12}));
            BEAST_EXPECT(expectOffers(
                env,
                ed,
                1,
                {Amounts{
                    STAmount{EUR, UINT64_C(957'3341836734693), -13},
                    STAmount{USD, UINT64_C(1'340'267857142857), -12}}}));
            // 25% on 47.7857USD is paid in tr fee 47.7857*1.25 = 59.7321USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount(USD, UINT64_C(1'447'785714285714), -12)));
        }
        {
            // Payment via AMM, AMM  with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw,
                {alice, bob, carol, ed},
                XRP(1'000),
                {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm1(env, bob, GBP(1'000), EUR(1'000));
            AMM amm2(env, ed, EUR(1'000), USD(1'400));

            // requested quality limit is 90USD/145GBP = 0.6206
            // trade quality is 66.7432USD/107.5308GBP = 0.6206
            env(pay(alice, carol, USD(90)),
                path(~EUR, ~USD),
                sendmax(GBP(145)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice buys 53.3322EUR with 107.5308GBP
                // 25% on 86.0246GBP is paid in tr fee
                // 1,400 - 86.0246*1.25 = 1400 - 107.5308 = 1229.4691GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'292'469135802469), -12}));
                // 86.0246GBP is swapped in for 79.2106EUR
                BEAST_EXPECT(amm1.expectBalances(
                    STAmount{GBP, UINT64_C(1'086'024691358025), -12},
                    STAmount{EUR, UINT64_C(920'78937795562), -11},
                    amm1.tokens()));
                // 25% on 63.3684EUR is paid in tr fee 63.3684*1.25 = 79.2106EUR
                // 63.3684EUR is swapped in for 83.4291USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount{EUR, UINT64_C(1'063'368497635504), -12},
                    STAmount{USD, UINT64_C(1'316'570881226053), -12},
                    amm2.tokens()));
            }
            else
            {
                // alice buys 53.3322EUR with 107.5308GBP
                // 25% on 86.0246GBP is paid in tr fee
                // 1,400 - 86.0246*1.25 = 1400 - 107.5308 = 1229.4691GBP
                BEAST_EXPECT(expectLine(
                    env,
                    alice,
                    STAmount{GBP, UINT64_C(1'292'469135802466), -12}));
                // 86.0246GBP is swapped in for 79.2106EUR
                BEAST_EXPECT(amm1.expectBalances(
                    STAmount{GBP, UINT64_C(1'086'024691358027), -12},
                    STAmount{EUR, UINT64_C(920'7893779556188), -13},
                    amm1.tokens()));
                // 25% on 63.3684EUR is paid in tr fee 63.3684*1.25 = 79.2106EUR
                // 63.3684EUR is swapped in for 83.4291USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount{EUR, UINT64_C(1'063'368497635505), -12},
                    STAmount{USD, UINT64_C(1'316'570881226053), -12},
                    amm2.tokens()));
            }
            // 25% on 66.7432USD is paid in tr fee 66.7432*1.25 = 83.4291USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount(USD, UINT64_C(1'466'743295019157), -12)));
        }
        {
            // Payment by the issuer via AMM, AMM  with limit quality,
            // deliver less than requested
            Env env(*this, features);

            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(1'000),
                {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw, 1.25));
            env.close();

            AMM amm1(env, alice, GBP(1'000), EUR(1'000));
            AMM amm2(env, bob, EUR(1'000), USD(1'400));

            // requested quality limit is 90USD/120GBP = 0.75
            // trade quality is 81.1111USD/108.1481GBP = 0.75
            env(pay(gw, carol, USD(90)),
                path(~EUR, ~USD),
                sendmax(GBP(120)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // 108.1481GBP is swapped in for 97.5935EUR
                BEAST_EXPECT(amm1.expectBalances(
                    STAmount{GBP, UINT64_C(1'108'148148148149), -12},
                    STAmount{EUR, UINT64_C(902'4064171122988), -13},
                    amm1.tokens()));
                // 25% on 78.0748EUR is paid in tr fee 78.0748*1.25 = 97.5935EUR
                // 78.0748EUR is swapped in for 101.3888USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount{EUR, UINT64_C(1'078'074866310161), -12},
                    STAmount{USD, UINT64_C(1'298'611111111111), -12},
                    amm2.tokens()));
            }
            else
            {
                // 108.1481GBP is swapped in for 97.5935EUR
                BEAST_EXPECT(amm1.expectBalances(
                    STAmount{GBP, UINT64_C(1'108'148148148151), -12},
                    STAmount{EUR, UINT64_C(902'4064171122975), -13},
                    amm1.tokens()));
                // 25% on 78.0748EUR is paid in tr fee 78.0748*1.25 = 97.5935EUR
                // 78.0748EUR is swapped in for 101.3888USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount{EUR, UINT64_C(1'078'074866310162), -12},
                    STAmount{USD, UINT64_C(1'298'611111111111), -12},
                    amm2.tokens()));
            }
            // 25% on 81.1111USD is paid in tr fee 81.1111*1.25 = 101.3888USD
            BEAST_EXPECT(expectLine(
                env, carol, STAmount{USD, UINT64_C(1'481'111111111111), -12}));
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

            fund(env, gw, {alice, bob, carol}, XRP(10'000), {USD(2'000)});

            AMM ammBob(env, bob, XRP(1'000), USD(1'050));
            env(offer(bob, XRP(100), USD(50)));

            env(pay(alice, carol, USD(100)),
                path(~USD),
                sendmax(XRP(100)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));

            BEAST_EXPECT(
                ammBob.expectBalances(XRP(1'050), USD(1'000), ammBob.tokens()));
            BEAST_EXPECT(expectLine(env, carol, USD(2'050)));
            BEAST_EXPECT(expectOffers(env, bob, 1, {{{XRP(100), USD(50)}}}));
        }
    }

    void
    testXRPPathLoop()
    {
        testcase("Circular XRP");

        using namespace jtx;

        for (auto const withFix : {true, false})
        {
            auto const feats = withFix
                ? supported_amendments()
                : supported_amendments() - FeatureBitset{fix1781};

            // Payment path starting with XRP
            Env env(*this, feats);
            // Note, if alice doesn't have default ripple, then pay
            // fails with tecPATH_DRY.
            fund(
                env,
                gw,
                {alice, bob},
                XRP(10'000),
                {USD(200), EUR(200)},
                Fund::All);

            AMM ammAliceXRP_USD(env, alice, XRP(100), USD(101));
            AMM ammAliceXRP_EUR(env, alice, XRP(100), EUR(101));
            env.close();

            TER const expectedTer =
                withFix ? TER{temBAD_PATH_LOOP} : TER{tesSUCCESS};
            env(pay(alice, bob, EUR(1)),
                path(~USD, ~XRP, ~EUR),
                sendmax(XRP(1)),
                txflags(tfNoRippleDirect),
                ter(expectedTer));
        }
        {
            // Payment path ending with XRP
            Env env(*this);
            // Note, if alice doesn't have default ripple, then pay fails
            // with tecPATH_DRY.
            fund(
                env,
                gw,
                {alice, bob},
                XRP(10'000),
                {USD(200), EUR(200)},
                Fund::All);

            AMM ammAliceXRP_USD(env, alice, XRP(100), USD(100));
            AMM ammAliceXRP_EUR(env, alice, XRP(100), EUR(100));
            // EUR -> //XRP -> //USD ->XRP
            env(pay(alice, bob, XRP(1)),
                path(~XRP, ~USD, ~XRP),
                sendmax(EUR(1)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
        {
            // Payment where loop is formed in the middle of the path, not
            // on an endpoint
            auto const JPY = gw["JPY"];
            Env env(*this);
            // Note, if alice doesn't have default ripple, then pay fails
            // with tecPATH_DRY.
            fund(
                env,
                gw,
                {alice, bob},
                XRP(10'000),
                {USD(200), EUR(200), JPY(200)},
                Fund::All);

            AMM ammAliceXRP_USD(env, alice, XRP(100), USD(100));
            AMM ammAliceXRP_EUR(env, alice, XRP(100), EUR(100));
            AMM ammAliceXRP_JPY(env, alice, XRP(100), JPY(100));

            env(pay(alice, bob, JPY(1)),
                path(~XRP, ~EUR, ~XRP, ~JPY),
                sendmax(USD(1)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
    }

    void
    testStepLimit(FeatureBitset features)
    {
        testcase("Step Limit");

        using namespace jtx;
        Env env(*this, features);
        auto const dan = Account("dan");
        auto const ed = Account("ed");

        fund(env, gw, {ed}, XRP(100'000'000), {USD(11)});
        env.fund(XRP(100'000'000), alice, bob, carol, dan);
        env.trust(USD(1), bob);
        env(pay(gw, bob, USD(1)));
        env.trust(USD(1), dan);
        env(pay(gw, dan, USD(1)));
        n_offers(env, 2'000, bob, XRP(1), USD(1));
        n_offers(env, 1, dan, XRP(1), USD(1));
        AMM ammEd(env, ed, XRP(9), USD(11));

        // Alice offers to buy 1000 XRP for 1000 USD. She takes Bob's first
        // offer, removes 999 more as unfunded, then hits the step limit.
        env(offer(alice, USD(1'000), XRP(1'000)));
        if (!features[fixAMMv1_1])
            env.require(balance(
                alice, STAmount{USD, UINT64_C(2'050126257867561), -15}));
        else
            env.require(balance(
                alice, STAmount{USD, UINT64_C(2'050125257867587), -15}));
        env.require(owners(alice, 2));
        env.require(balance(bob, USD(0)));
        env.require(owners(bob, 1'001));
        env.require(balance(dan, USD(1)));
        env.require(owners(dan, 2));

        // Carol offers to buy 1000 XRP for 1000 USD. She removes Bob's next
        // 1000 offers as unfunded and hits the step limit.
        env(offer(carol, USD(1'000), XRP(1'000)));
        env.require(balance(carol, USD(none)));
        env.require(owners(carol, 1));
        env.require(balance(bob, USD(0)));
        env.require(owners(bob, 1));
        env.require(balance(dan, USD(1)));
        env.require(owners(dan, 2));
    }

    void
    test_convert_all_of_an_asset(FeatureBitset features)
    {
        testcase("Convert all of an asset using DeliverMin");

        using namespace jtx;

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol}, XRP(10'000));
            env.trust(USD(100), alice, bob, carol);
            env(pay(alice, bob, USD(10)),
                delivermin(USD(10)),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(USD(-5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(XRP(5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(Account(carol)["USD"](5)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(alice, bob, USD(10)),
                delivermin(USD(15)),
                txflags(tfPartialPayment),
                ter(temBAD_AMOUNT));
            env(pay(gw, carol, USD(50)));
            AMM ammCarol(env, carol, XRP(10), USD(15));
            env(pay(alice, bob, USD(10)),
                paths(XRP),
                delivermin(USD(7)),
                txflags(tfPartialPayment),
                sendmax(XRP(5)),
                ter(tecPATH_PARTIAL));
            env.require(balance(alice, XRP(9'999.99999)));
            env.require(balance(bob, XRP(10'000)));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob}, XRP(10'000));
            env.trust(USD(1'100), alice, bob);
            env(pay(gw, bob, USD(1'100)));
            AMM ammBob(env, bob, XRP(1'000), USD(1'100));
            env(pay(alice, alice, USD(10'000)),
                paths(XRP),
                delivermin(USD(100)),
                txflags(tfPartialPayment),
                sendmax(XRP(100)));
            env.require(balance(alice, USD(100)));
        }

        {
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol}, XRP(10'000));
            env.trust(USD(1'200), bob, carol);
            env(pay(gw, bob, USD(1'200)));
            AMM ammBob(env, bob, XRP(5'500), USD(1'200));
            env(pay(alice, carol, USD(10'000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1'000)),
                ter(tecPATH_PARTIAL));
            env(pay(alice, carol, USD(10'000)),
                paths(XRP),
                delivermin(USD(200)),
                txflags(tfPartialPayment),
                sendmax(XRP(1'100)));
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(6'600), USD(1'000), ammBob.tokens()));
            env.require(balance(carol, USD(200)));
        }

        {
            auto const dan = Account("dan");
            Env env(*this, features);
            fund(env, gw, {alice, bob, carol, dan}, XRP(10'000));
            env.trust(USD(1'100), bob, carol, dan);
            env(pay(gw, bob, USD(100)));
            env(pay(gw, dan, USD(1'100)));
            env(offer(bob, XRP(100), USD(100)));
            env(offer(bob, XRP(1'000), USD(100)));
            AMM ammDan(env, dan, XRP(1'000), USD(1'100));
            if (!features[fixAMMv1_1])
            {
                env(pay(alice, carol, USD(10'000)),
                    paths(XRP),
                    delivermin(USD(200)),
                    txflags(tfPartialPayment),
                    sendmax(XRP(200)));
                env.require(balance(bob, USD(0)));
                env.require(balance(carol, USD(200)));
                BEAST_EXPECT(ammDan.expectBalances(
                    XRP(1'100), USD(1'000), ammDan.tokens()));
            }
            else
            {
                env(pay(alice, carol, USD(10'000)),
                    paths(XRP),
                    delivermin(USD(200)),
                    txflags(tfPartialPayment),
                    sendmax(XRPAmount(200'000'001)));
                env.require(balance(bob, USD(0)));
                env.require(balance(
                    carol, STAmount{USD, UINT64_C(200'00000090909), -11}));
                BEAST_EXPECT(ammDan.expectBalances(
                    XRPAmount{1'100'000'001},
                    STAmount{USD, UINT64_C(999'99999909091), -11},
                    ammDan.tokens()));
            }
        }
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace jtx;
        Account const becky{"becky"};

        bool const supportsPreauth = {features[featureDepositPreauth]};

        // The initial implementation of DepositAuth had a bug where an
        // account with the DepositAuth flag set could not make a payment
        // to itself.  That bug was fixed in the DepositPreauth amendment.
        Env env(*this, features);
        fund(env, gw, {alice, becky}, XRP(5'000));
        env.close();

        env.trust(USD(1'000), alice);
        env.trust(USD(1'000), becky);
        env.close();

        env(pay(gw, alice, USD(500)));
        env.close();

        AMM ammAlice(env, alice, XRP(100), USD(140));

        // becky pays herself USD (10) by consuming part of alice's offer.
        // Make sure the payment works if PaymentAuth is not involved.
        env(pay(becky, becky, USD(10)), path(~USD), sendmax(XRP(10)));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount(107'692'308), USD(130), ammAlice.tokens()));

        // becky decides to require authorization for deposits.
        env(fset(becky, asfDepositAuth));
        env.close();

        // becky pays herself again.  Whether it succeeds depends on
        // whether featureDepositPreauth is enabled.
        TER const expect{
            supportsPreauth ? TER{tesSUCCESS} : TER{tecNO_PERMISSION}};

        env(pay(becky, becky, USD(10)),
            path(~USD),
            sendmax(XRP(10)),
            ter(expect));

        env.close();
    }

    void
    testPayIOU()
    {
        // Exercise IOU payments and non-direct XRP payments to an account
        // that has the lsfDepositAuth flag set.
        testcase("Pay IOU");

        using namespace jtx;

        Env env(*this);

        fund(env, gw, {alice, bob, carol}, XRP(10'000));
        env.trust(USD(1'000), alice, bob, carol);
        env.close();

        env(pay(gw, alice, USD(150)));
        env(pay(gw, carol, USD(150)));
        AMM ammCarol(env, carol, USD(100), XRPAmount(101));

        // Make sure bob's trust line is all set up so he can receive USD.
        env(pay(alice, bob, USD(50)));
        env.close();

        // bob sets the lsfDepositAuth flag.
        env(fset(bob, asfDepositAuth), require(flags(bob, asfDepositAuth)));
        env.close();

        // None of the following payments should succeed.
        auto failedIouPayments = [this, &env]() {
            env.require(flags(bob, asfDepositAuth));

            // Capture bob's balances before hand to confirm they don't
            // change.
            PrettyAmount const bobXrpBalance{env.balance(bob, XRP)};
            PrettyAmount const bobUsdBalance{env.balance(bob, USD)};

            env(pay(alice, bob, USD(50)), ter(tecNO_PERMISSION));
            env.close();

            // Note that even though alice is paying bob in XRP, the payment
            // is still not allowed since the payment passes through an
            // offer.
            env(pay(alice, bob, drops(1)),
                sendmax(USD(1)),
                ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(bobXrpBalance == env.balance(bob, XRP));
            BEAST_EXPECT(bobUsdBalance == env.balance(bob, USD));
        };

        //  Test when bob has an XRP balance > base reserve.
        failedIouPayments();

        // Set bob's XRP balance == base reserve.  Also demonstrate that
        // bob can make payments while his lsfDepositAuth flag is set.
        env(pay(bob, alice, USD(25)));
        env.close();

        {
            STAmount const bobPaysXRP{env.balance(bob, XRP) - reserve(env, 1)};
            XRPAmount const bobPaysFee{reserve(env, 1) - reserve(env, 0)};
            env(pay(bob, alice, bobPaysXRP), fee(bobPaysFee));
            env.close();
        }

        // Test when bob's XRP balance == base reserve.
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0));
        BEAST_EXPECT(env.balance(bob, USD) == USD(25));
        failedIouPayments();

        // Test when bob has an XRP balance == 0.
        env(noop(bob), fee(reserve(env, 0)));
        env.close();

        BEAST_EXPECT(env.balance(bob, XRP) == XRP(0));
        failedIouPayments();

        // Give bob enough XRP for the fee to clear the lsfDepositAuth flag.
        env(pay(alice, bob, drops(env.current()->fees().base)));

        // bob clears the lsfDepositAuth and the next payment succeeds.
        env(fclear(bob, asfDepositAuth));
        env.close();

        env(pay(alice, bob, USD(50)));
        env.close();

        env(pay(alice, bob, drops(1)), sendmax(USD(1)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(
            USD(101), XRPAmount(100), ammCarol.tokens()));
    }

    void
    testRippleState(FeatureBitset features)
    {
        testcase("RippleState Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account const G1{"G1"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1'000), G1, alice, bob);
        env.close();

        env.trust(G1["USD"](100), bob);
        env.trust(G1["USD"](205), alice);
        env.close();

        env(pay(G1, bob, G1["USD"](10)));
        env(pay(G1, alice, G1["USD"](205)));
        env.close();

        AMM ammAlice(env, alice, XRP(500), G1["USD"](105));

        {
            auto lines = getAccountLines(env, bob);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == G1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "100");
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "10");
        }

        {
            auto lines = getAccountLines(env, alice, G1["USD"]);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == G1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "205");
            // 105 transferred to AMM
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "100");
        }

        {
            // Account with line unfrozen (proving operations normally work)
            //   test: can make Payment on that line
            env(pay(alice, bob, G1["USD"](1)));

            //   test: can receive Payment on that line
            env(pay(bob, alice, G1["USD"](1)));
            env.close();
        }

        {
            // Is created via a TrustSet with SetFreeze flag
            //   test: sets LowFreeze | HighFreeze flags
            env(trust(G1, bob["USD"](0), tfSetFreeze));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfLowLimit.fieldName] ==
                G1["USD"](0).value().getJson(JsonOptions::none));
            BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfLowFreeze);
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
            env.close();
        }

        {
            // Account with line frozen by issuer
            //    test: can buy more assets on that line
            env(offer(bob, G1["USD"](5), XRP(25)));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 4u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfHighLimit.fieldName] ==
                bob["USD"](100).value().getJson(JsonOptions::none));
            auto amt = STAmount{Issue{to_currency("USD"), noAccount()}, -15}
                           .value()
                           .getJson(JsonOptions::none);
            BEAST_EXPECT(ff[sfBalance.fieldName] == amt);
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(525), G1["USD"](100), ammAlice.tokens()));
        }

        {
            //    test: can not sell assets from that line
            env(offer(bob, XRP(1), G1["USD"](5)), ter(tecUNFUNDED_OFFER));

            //    test: can receive Payment on that line
            env(pay(alice, bob, G1["USD"](1)));

            //    test: can not make Payment from that line
            env(pay(bob, alice, G1["USD"](1)), ter(tecPATH_DRY));
        }

        {
            // check G1 account lines
            //    test: shows freeze
            auto lines = getAccountLines(env, G1);
            Json::Value bobLine;
            for (auto const& it : lines[jss::lines])
            {
                if (it[jss::account] == bob.human())
                {
                    bobLine = it;
                    break;
                }
            }
            if (!BEAST_EXPECT(bobLine))
                return;
            BEAST_EXPECT(bobLine[jss::freeze] == true);
            BEAST_EXPECT(bobLine[jss::balance] == "-16");
        }

        {
            //    test: shows freeze peer
            auto lines = getAccountLines(env, bob);
            Json::Value g1Line;
            for (auto const& it : lines[jss::lines])
            {
                if (it[jss::account] == G1.human())
                {
                    g1Line = it;
                    break;
                }
            }
            if (!BEAST_EXPECT(g1Line))
                return;
            BEAST_EXPECT(g1Line[jss::freeze_peer] == true);
            BEAST_EXPECT(g1Line[jss::balance] == "16");
        }

        {
            // Is cleared via a TrustSet with ClearFreeze flag
            //    test: sets LowFreeze | HighFreeze flags
            env(trust(G1, bob["USD"](0), tfClearFreeze));
            auto affected = env.meta()->getJson(
                JsonOptions::none)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
                return;
            auto ff =
                affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfLowLimit.fieldName] ==
                G1["USD"](0).value().getJson(JsonOptions::none));
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfLowFreeze));
            BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
            env.close();
        }
    }

    void
    testGlobalFreeze(FeatureBitset features)
    {
        testcase("Global Freeze");

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
        env.close();

        env.trust(G1["USD"](1'200), A1);
        env.trust(G1["USD"](200), A2);
        env.trust(G1["BTC"](100), A3);
        env.trust(G1["BTC"](100), A4);
        env.close();

        env(pay(G1, A1, G1["USD"](1'000)));
        env(pay(G1, A2, G1["USD"](100)));
        env(pay(G1, A3, G1["BTC"](100)));
        env(pay(G1, A4, G1["BTC"](100)));
        env.close();

        AMM ammG1(env, G1, XRP(10'000), G1["USD"](100));
        env(offer(A1, XRP(10'000), G1["USD"](100)), txflags(tfPassive));
        env(offer(A2, G1["USD"](100), XRP(10'000)), txflags(tfPassive));
        env.close();

        {
            // Account without GlobalFreeze (proving operations normally
            // work)
            //    test: visible offers where taker_pays is unfrozen issuer
            auto offers = env.rpc(
                "book_offers",
                std::string("USD/") + G1.human(),
                "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
            std::set<std::string> accounts;
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(A2.human()) != std::end(accounts));

            //    test: visible offers where taker_gets is unfrozen issuer
            offers = env.rpc(
                "book_offers",
                "XRP",
                std::string("USD/") + G1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
            accounts.clear();
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(A1.human()) != std::end(accounts));
        }

        {
            // Offers/Payments
            //    test: assets can be bought on the market
            // env(offer(A3, G1["BTC"](1), XRP(1)));
            AMM ammA3(env, A3, G1["BTC"](1), XRP(1));

            //    test: assets can be sold on the market
            // AMM is bidirectional

            //    test: direct issues can be sent
            env(pay(G1, A2, G1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(A2, G1, G1["USD"](1)));

            //    test: via rippling can be sent
            env(pay(A2, A1, G1["USD"](1)));

            //    test: via rippling can be sent back
            env(pay(A1, A2, G1["USD"](1)));
            ammA3.withdrawAll(std::nullopt);
        }

        {
            // Account with GlobalFreeze
            //  set GlobalFreeze first
            //    test: SetFlag GlobalFreeze will toggle back to freeze
            env.require(nflags(G1, asfGlobalFreeze));
            env(fset(G1, asfGlobalFreeze));
            env.require(flags(G1, asfGlobalFreeze));
            env.require(nflags(G1, asfNoFreeze));

            //    test: assets can't be bought on the market
            AMM ammA3(env, A3, G1["BTC"](1), XRP(1), ter(tecFROZEN));

            //    test: assets can't be sold on the market
            // AMM is bidirectional
        }

        {
            //    test: book_offers shows offers
            //    (should these actually be filtered?)
            auto offers = env.rpc(
                "book_offers",
                "XRP",
                std::string("USD/") + G1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;

            offers = env.rpc(
                "book_offers",
                std::string("USD/") + G1.human(),
                "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
        }

        {
            // Payments
            //    test: direct issues can be sent
            env(pay(G1, A2, G1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(A2, G1, G1["USD"](1)));

            //    test: via rippling cant be sent
            env(pay(A2, A1, G1["USD"](1)), ter(tecPATH_DRY));
        }
    }

    void
    testOffersWhenFrozen(FeatureBitset features)
    {
        testcase("Offers for Frozen Trust Lines");

        using namespace test::jtx;
        Env env(*this, features);

        Account G1{"G1"};
        Account A2{"A2"};
        Account A3{"A3"};
        Account A4{"A4"};

        env.fund(XRP(2'000), G1, A3, A4);
        env.fund(XRP(2'000), A2);
        env.close();

        env.trust(G1["USD"](1'000), A2);
        env.trust(G1["USD"](2'000), A3);
        env.trust(G1["USD"](2'001), A4);
        env.close();

        env(pay(G1, A3, G1["USD"](2'000)));
        env(pay(G1, A4, G1["USD"](2'001)));
        env.close();

        AMM ammA3(env, A3, XRP(1'000), G1["USD"](1'001));

        // removal after successful payment
        //    test: make a payment with partially consuming offer
        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
        env.close();

        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1'001), G1["USD"](1'000), ammA3.tokens()));

        //    test: someone else creates an offer providing liquidity
        env(offer(A4, XRP(999), G1["USD"](999)));
        env.close();
        // The offer consumes AMM offer
        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1'000), G1["USD"](1'001), ammA3.tokens()));

        //    test: AMM line is frozen
        auto const a3am =
            STAmount{Issue{to_currency("USD"), ammA3.ammAccount()}, 0};
        env(trust(G1, a3am, tfSetFreeze));
        auto const info = ammA3.ammRpcInfo();
        BEAST_EXPECT(info[jss::amm][jss::asset2_frozen].asBool());
        auto affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
            return;
        auto ff =
            affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
        BEAST_EXPECT(
            ff[sfHighLimit.fieldName] ==
            G1["USD"](0).value().getJson(JsonOptions::none));
        BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfLowFreeze));
        BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfHighFreeze);
        env.close();

        //    test: Can make a payment via the new offer
        env(pay(A2, G1, G1["USD"](1)), paths(G1["USD"]), sendmax(XRP(1)));
        env.close();
        // AMM is not consumed
        BEAST_EXPECT(
            ammA3.expectBalances(XRP(1'000), G1["USD"](1'001), ammA3.tokens()));

        // removal buy successful OfferCreate
        //    test: freeze the new offer
        env(trust(G1, A4["USD"](0), tfSetFreeze));
        affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
            return;
        ff = affected[0u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
        BEAST_EXPECT(
            ff[sfLowLimit.fieldName] ==
            G1["USD"](0).value().getJson(JsonOptions::none));
        BEAST_EXPECT(ff[jss::Flags].asUInt() & lsfLowFreeze);
        BEAST_EXPECT(!(ff[jss::Flags].asUInt() & lsfHighFreeze));
        env.close();

        //    test: can no longer create a crossing offer
        env(offer(A2, G1["USD"](999), XRP(999)));
        affected =
            env.meta()->getJson(JsonOptions::none)[sfAffectedNodes.fieldName];
        if (!BEAST_EXPECT(checkArraySize(affected, 8u)))
            return;
        auto created = affected[0u][sfCreatedNode.fieldName];
        BEAST_EXPECT(
            created[sfNewFields.fieldName][jss::Account] == A2.human());
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
        fund(env, gw, {alice, becky, zelda}, XRP(20'000), {USD(20'000)});

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), sig(alie));
        env.close();
        int const signerListOwners{features[featureMultiSignReserve] ? 2 : 5};
        env.require(owners(alice, signerListOwners + 0));

        const msig ms{becky, bogie};

        // Multisign all AMM transactions
        AMM ammAlice(
            env,
            alice,
            XRP(10'000),
            USD(10'000),
            false,
            0,
            ammCrtFee(env).drops(),
            std::nullopt,
            std::nullopt,
            ms,
            ter(tesSUCCESS));
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), USD(10'000), ammAlice.tokens()));

        ammAlice.deposit(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));

        ammAlice.withdraw(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), USD(10'000), ammAlice.tokens()));

        ammAlice.vote({}, 1'000);
        BEAST_EXPECT(ammAlice.expectTradingFee(1'000));

        env(ammAlice.bid({.account = alice, .bidMin = 100}), ms).close();
        BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, IOUAmount{4'000}));
        // 4000 tokens burnt
        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(10'000), USD(10'000), IOUAmount{9'996'000, 0}));
    }

    void
    testToStrand(FeatureBitset features)
    {
        testcase("To Strand");

        using namespace jtx;

        // cannot have more than one offer with the same output issue

        Env env(*this, features);

        fund(
            env,
            gw,
            {alice, bob, carol},
            XRP(10'000),
            {USD(2'000), EUR(1'000)});

        AMM bobXRP_USD(env, bob, XRP(1'000), USD(1'000));
        AMM bobUSD_EUR(env, bob, USD(1'000), EUR(1'000));

        // payment path: XRP -> XRP/USD -> USD/EUR -> EUR/USD
        env(pay(alice, carol, USD(100)),
            path(~USD, ~EUR, ~USD),
            sendmax(XRP(200)),
            txflags(tfNoRippleDirect),
            ter(temBAD_PATH_LOOP));
    }

    void
    testRIPD1373(FeatureBitset features)
    {
        using namespace jtx;
        testcase("RIPD1373");

        {
            Env env(*this, features);
            auto const BobUSD = bob["USD"];
            auto const BobEUR = bob["EUR"];
            fund(env, gw, {alice, bob}, XRP(10'000));
            env.trust(USD(1'000), alice, bob);
            env.trust(EUR(1'000), alice, bob);
            fund(
                env,
                bob,
                {alice, gw},
                {BobUSD(100), BobEUR(100)},
                Fund::IOUOnly);

            AMM ammBobXRP_USD(env, bob, XRP(100), BobUSD(100));
            env(offer(gw, XRP(100), USD(100)), txflags(tfPassive));

            AMM ammBobUSD_EUR(env, bob, BobUSD(100), BobEUR(100));
            env(offer(gw, USD(100), EUR(100)), txflags(tfPassive));

            Path const p = [&] {
                Path result;
                result.push_back(allpe(gw, BobUSD));
                result.push_back(cpe(EUR.currency));
                return result;
            }();

            PathSet paths(p);

            env(pay(alice, alice, EUR(1)),
                json(paths.json()),
                sendmax(XRP(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(temBAD_PATH));
        }

        {
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10'000), {USD(100)});

            AMM ammBob(env, bob, XRP(100), USD(100));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                path(~USD, ~XRP),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            fund(env, gw, {alice, bob, carol}, XRP(10'000), {USD(100)});

            AMM ammBob(env, bob, XRP(100), USD(100));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                path(~USD, ~XRP),
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

        auto const CNY = gw["CNY"];

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);
            env.trust(USD(10'000), alice, bob, carol);

            env(pay(gw, bob, USD(100)));
            env(pay(gw, alice, USD(100)));

            AMM ammBob(env, bob, XRP(100), USD(100));

            // payment path: USD -> USD/XRP -> XRP/USD
            env(pay(alice, carol, USD(100)),
                sendmax(USD(100)),
                path(~XRP, ~USD),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);
            env.trust(USD(10'000), alice, bob, carol);
            env.trust(EUR(10'000), alice, bob, carol);
            env.trust(CNY(10'000), alice, bob, carol);

            env(pay(gw, bob, USD(200)));
            env(pay(gw, bob, EUR(200)));
            env(pay(gw, bob, CNY(100)));

            AMM ammBobXRP_USD(env, bob, XRP(100), USD(100));
            AMM ammBobUSD_EUR(env, bob, USD(100), EUR(100));
            AMM ammBobEUR_CNY(env, bob, EUR(100), CNY(100));

            // payment path: XRP->XRP/USD->USD/EUR->USD/CNY
            env(pay(alice, carol, CNY(100)),
                sendmax(XRP(100)),
                path(~USD, ~EUR, ~USD, ~CNY),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
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
        path_find_05();
        path_find_06();
    }

    void
    testFlow()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        FeatureBitset const ownerPaysFee{featureOwnerPaysFee};

        testFalseDry(all);
        testBookStep(all);
        testBookStep(all | ownerPaysFee);
        testTransferRate(all | ownerPaysFee);
        testTransferRate((all - fixAMMv1_1) | ownerPaysFee);
        testTransferRateNoOwnerFee(all);
        testTransferRateNoOwnerFee(all - fixAMMv1_1);
        testLimitQuality();
        testXRPPathLoop();
    }

    void
    testCrossingLimits()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        testStepLimit(all);
        testStepLimit(all - fixAMMv1_1);
    }

    void
    testDeliverMin()
    {
        using namespace jtx;
        FeatureBitset const all{supported_amendments()};
        test_convert_all_of_an_asset(all);
        test_convert_all_of_an_asset(all - fixAMMv1_1);
    }

    void
    testDepositAuth()
    {
        auto const supported{jtx::supported_amendments()};
        testPayment(supported - featureDepositPreauth);
        testPayment(supported);
        testPayIOU();
    }

    void
    testFreeze()
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testRippleState(sa);
        testGlobalFreeze(sa);
        testOffersWhenFrozen(sa);
    }

    void
    testMultisign()
    {
        using namespace jtx;
        auto const all = supported_amendments();

        testTxMultisign(
            all - featureMultiSignReserve - featureExpandedSignerList);
        testTxMultisign(all - featureExpandedSignerList);
        testTxMultisign(all);
    }

    void
    testPayStrand()
    {
        using namespace jtx;
        auto const all = supported_amendments();

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
        testFreeze();
        testMultisign();
        testPayStrand();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMExtended, app, ripple, 1);

}  // namespace test
}  // namespace ripple
