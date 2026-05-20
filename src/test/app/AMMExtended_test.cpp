#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/delivermin.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/quality.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/require.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/paths/Flow.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <cstdint>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * Tests of AMM that use offers too.
 */
struct AMMExtended_test : public jtx::AMMTest
{
    // Use small Number mantissas for the life of this test.
    NumberMantissaScaleGuard const sg{xrpl::MantissaRange::MantissaScale::Small};

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

        fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000), {USD(200'000), BTC(2'000)});

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env(offer(carol_, BTC(49), XRP(49)));
        env(offer(carol_, BTC(51), XRP(51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env(offer(carol_, XRP(50), USD(50)));
        env(offer(carol_, XRP(50), USD(50)));

        // Good quality path
        AMM const ammCarol(env, carol_, BTC(1'000), USD(100'100));

        PathSet const paths(TestPath(XRP, USD), TestPath(USD));

        env(pay(alice_, bob_, USD(100)),
            Json(paths.json()),
            Sendmax(BTC(1'000)),
            Txflags(tfPartialPayment));

        if (!features[fixAMMv1_1])
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{BTC, UINT64_C(1'001'000000374812), -12}, USD(100'000), ammCarol.tokens()));
        }
        else
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{BTC, UINT64_C(1'001'000000374815), -12}, USD(100'000), ammCarol.tokens()));
        }

        env.require(Balance(bob_, USD(200'100)));
        BEAST_EXPECT(isOffer(env, carol_, BTC(49), XRP(49)));
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
            auto const usD1 = gw1["USD"];
            auto const usD2 = gw2["USD"];

            env.fund(XRP(20'000), alice_, noripple(bob_), carol_, dan, gw1, gw2);
            env.close();
            env.trust(usD1(20'000), alice_, carol_, dan);
            env(trust(bob_, usD1(1'000), tfSetNoRipple));
            env.trust(usD2(1'000), alice_, carol_, dan);
            env(trust(bob_, usD2(1'000), tfSetNoRipple));
            env.close();

            env(pay(gw1, dan, usD1(10'000)));
            env(pay(gw1, bob_, usD1(50)));
            env(pay(gw2, bob_, usD2(50)));
            env.close();

            AMM const ammDan(env, dan, XRP(10'000), usD1(10'000));

            env(pay(alice_, carol_, usD2(50)),
                Path(~usD1, bob_),
                Sendmax(XRP(50)),
                Txflags(tfNoRippleDirect),
                Ter(tecPATH_DRY));
        }

        {
            // Make sure payment works with default flags
            Env env{*this, features};

            Account const dan("dan");
            Account const gw1("gw1");
            Account const gw2("gw2");
            auto const usD1 = gw1["USD"];
            auto const usD2 = gw2["USD"];

            env.fund(XRP(20'000), alice_, bob_, carol_, gw1, gw2);
            env.fund(XRP(20'000), dan);
            env.close();
            env.trust(usD1(20'000), alice_, bob_, carol_, dan);
            env.trust(usD2(1'000), alice_, bob_, carol_, dan);
            env.close();

            env(pay(gw1, dan, usD1(10'050)));
            env(pay(gw1, bob_, usD1(50)));
            env(pay(gw2, bob_, usD2(50)));
            env.close();

            AMM const ammDan(env, dan, XRP(10'000), usD1(10'050));

            env(pay(alice_, carol_, usD2(50)),
                Path(~usD1, bob_),
                Sendmax(XRP(50)),
                Txflags(tfNoRippleDirect));
            BEAST_EXPECT(ammDan.expectBalances(XRP(10'050), usD1(10'000), ammDan.tokens()));

            BEAST_EXPECT(expectLedgerEntryRoot(env, alice_, XRP(20'000) - XRP(50) - txFee(env, 1)));
            BEAST_EXPECT(expectHolding(env, bob_, usD1(100)));
            BEAST_EXPECT(expectHolding(env, bob_, usD2(0)));
            BEAST_EXPECT(expectHolding(env, carol_, usD2(50)));
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
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Order that can't be filled
                TER const killedCode{TER{tecKILLED}};
                env(offer(carol_, USD(100), XRP(100)), Txflags(tfFillOrKill), Ter(killedCode));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
                // fee = AMM
                BEAST_EXPECT(expectLedgerEntryRoot(env, carol_, XRP(30'000) - (txFee(env, 1))));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
                BEAST_EXPECT(expectHolding(env, carol_, USD(30'000)));

                // Order that can be filled
                env(offer(carol_, XRP(100), USD(100)), Txflags(tfFillOrKill), Ter(tesSUCCESS));
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(10'100), ammAlice.tokens()));
                BEAST_EXPECT(
                    expectLedgerEntryRoot(env, carol_, XRP(30'000) + XRP(100) - txFee(env, 2)));
                BEAST_EXPECT(expectHolding(env, carol_, USD(29'900)));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
            },
            {{XRP(10'100), USD(10'000)}},
            0,
            std::nullopt,
            {features});

        // Immediate or Cancel - cross as much as possible
        // and add nothing on the books.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(offer(carol_, XRP(200), USD(200)),
                    Txflags(tfImmediateOrCancel),
                    Ter(tesSUCCESS));

                // AMM generates a synthetic offer of 100USD/100XRP
                // to match the CLOB offer quality.
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(10'100), ammAlice.tokens()));
                // +AMM - offer * fee
                BEAST_EXPECT(
                    expectLedgerEntryRoot(env, carol_, XRP(30'000) + XRP(100) - txFee(env, 1)));
                // AMM
                BEAST_EXPECT(expectHolding(env, carol_, USD(29'900)));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
            },
            {{XRP(10'100), USD(10'000)}},
            0,
            std::nullopt,
            {features});

        // tfPassive -- place the offer without crossing it.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Carol creates a passive offer that could cross AMM.
                // Carol's offer should stay in the ledger.
                env(offer(carol_, XRP(100), USD(100), tfPassive));
                env.close();
                BEAST_EXPECT(
                    ammAlice.expectBalances(XRP(10'100), STAmount{USD, 10'000}, ammAlice.tokens()));
                BEAST_EXPECT(expectOffers(env, carol_, 1, {{{XRP(100), STAmount{USD, 100}}}}));
            },
            {{XRP(10'100), USD(10'000)}},
            0,
            std::nullopt,
            {features});

        // tfPassive -- cross only offers of better quality.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(offer(alice_, USD(110), XRP(100)));
                env.close();

                // Carol creates a passive offer.  That offer should cross
                // AMM and leave Alice's offer untouched.
                env(offer(carol_, XRP(100), USD(100), tfPassive));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'900),
                    STAmount{USD, UINT64_C(9'082'56880733945), -11},
                    ammAlice.tokens()));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
                BEAST_EXPECT(expectOffers(env, alice_, 1));
            },
            {{XRP(11'000), USD(9'000)}},
            0,
            std::nullopt,
            {features});
    }

    void
    testOfferCrossWithXRP(FeatureBitset features)
    {
        testcase("Offer Crossing with XRP, Normal order");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw_, {bob_, alice_}, XRP(300'000), {USD(100)}, Fund::All);

        AMM const ammAlice(env, alice_, XRP(150'000), USD(50));

        // Existing offer pays better than this wants.
        // Partially consume existing offer.
        // Pay 1 USD, get 3061224490 Drops.
        auto const xrpTransferred = XRPAmount{3'061'224'490};
        env(offer(bob_, USD(1), XRP(4'000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(150'000) + xrpTransferred, USD(49), IOUAmount{273'861'278752583, -8}));

        BEAST_EXPECT(expectHolding(env, bob_, STAmount{USD, 101}));
        BEAST_EXPECT(
            expectLedgerEntryRoot(env, bob_, XRP(300'000) - xrpTransferred - txFee(env, 1)));
        BEAST_EXPECT(expectOffers(env, bob_, 0));
    }

    void
    testOfferCrossWithLimitOverride(FeatureBitset features)
    {
        testcase("Offer Crossing with Limit Override");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(200'000), gw_, alice_, bob_);
        env.close();

        env(trust(alice_, USD(1'000)));

        env(pay(gw_, alice_, alice_["USD"](500)));

        AMM const ammAlice(env, alice_, XRP(150'000), USD(51));
        env(offer(bob_, USD(1), XRP(3'000)));

        BEAST_EXPECT(ammAlice.expectBalances(XRP(153'000), USD(50), ammAlice.tokens()));

        auto jrr = ledgerEntryState(env, bob_, gw_, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-1");
        jrr = ledgerEntryRoot(env, bob_);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(200'000) - XRP(3'000) - env.current()->fees().base * 1).xrp()));
    }

    void
    testCurrencyConversionEntire(FeatureBitset features)
    {
        testcase("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw_, {alice_, bob_}, XRP(10'000));
        env.require(Owners(bob_, 0));

        env(trust(alice_, USD(100)));
        env(trust(bob_, USD(1'000)));
        env(pay(gw_, bob_, USD(1'000)));

        env.require(Owners(alice_, 1), Owners(bob_, 1));

        env(pay(gw_, alice_, alice_["USD"](100)));
        AMM const ammBob(env, bob_, USD(200), XRP(1'500));

        env(pay(alice_, alice_, XRP(500)), Sendmax(USD(100)));

        BEAST_EXPECT(ammBob.expectBalances(USD(300), XRP(1'000), ammBob.tokens()));
        BEAST_EXPECT(expectHolding(env, alice_, USD(0)));

        auto jrr = ledgerEntryRoot(env, alice_);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10'000) + XRP(500) - env.current()->fees().base * 2).xrp()));
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
                env(pay(alice_, alice_, XRP(100)), Sendmax(USD(100)), Ter(tecPATH_PARTIAL));

                // Alice converts USD to XRP, should succeed because
                // we permit partial payment
                env(pay(alice_, alice_, XRP(100)), Sendmax(USD(100)), Txflags(tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{9'900'990'100}, USD(10'100), ammAlice.tokens()));
                // initial 30,000 - 10,000AMM - 100pay
                BEAST_EXPECT(expectHolding(env, alice_, USD(19'900)));
                // initial 30,000 - 10,0000AMM + 99.009900pay - fee*3
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env,
                    alice_,
                    XRP(30'000) - XRP(10'000) + XRPAmount{99'009'900} - ammCrtFee(env) -
                        txFee(env, 2)));
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
                env.fund(XRP(1'000), bob_);
                env.close();
                env(trust(bob_, USD(100)));
                env.close();
                env(pay(alice_, bob_, USD(100)), Sendmax(XRP(100)));
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
                BEAST_EXPECT(expectHolding(env, bob_, USD(100)));
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
                env.fund(XRP(1'000), bob_);
                env.close();
                env(trust(bob_, USD(100)));
                env.close();
                env(pay(alice_, bob_, XRP(100)), Sendmax(USD(100)));
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(10'100), ammAlice.tokens()));
                BEAST_EXPECT(
                    expectLedgerEntryRoot(env, bob_, XRP(1'000) + XRP(100) - txFee(env, 1)));
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
        auto const usD1 = gw1["USD"];
        auto const euR1 = gw2["EUR"];

        fund(env, gw1, {gw2, alice_, bob_, carol_, dan}, XRP(60'000));
        env(trust(alice_, usD1(1'000)));
        env.close();
        env(trust(bob_, euR1(1'000)));
        env.close();
        env(trust(carol_, usD1(10'000)));
        env.close();
        env(trust(dan, euR1(1'000)));
        env.close();

        env(pay(gw1, alice_, alice_["USD"](500)));
        env.close();
        env(pay(gw1, carol_, carol_["USD"](6'000)));
        env(pay(gw2, dan, dan["EUR"](400)));
        env.close();

        AMM const ammCarol(env, carol_, usD1(5'000), XRP(50'000));

        env(offer(dan, XRP(500), euR1(50)));
        env.close();

        json::Value jtp{json::ValueType::Array};
        jtp[0u][0u][jss::currency] = "XRP";
        env(pay(alice_, bob_, euR1(30)), Json(jss::Paths, jtp), Sendmax(usD1(333)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(
            XRP(49'700), STAmount{usD1, UINT64_C(5'030'181086519115), -12}, ammCarol.tokens()));
        BEAST_EXPECT(expectOffers(env, dan, 1, {{Amounts{XRP(200), EUR(20)}}}));
        BEAST_EXPECT(expectHolding(env, bob_, STAmount{euR1, 30}));
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
        auto const localAlice = Account{"alice"};
        auto const localBob = Account{"bob"};
        auto const usD1 = gw1["USD"];
        auto const usD2 = gw2["USD"];
        auto const usD3 = gw3["USD"];

        // Provide micro amounts to compensate for fees to make results round
        // nice.
        // reserve: Alice has 3 entries in the ledger, via trust lines
        // fees:
        //  1 for each trust limit == 3 (alice_ < mtgox/amazon/bitstamp) +
        //  1 for payment          == 4
        auto const startingXrp =
            XRP(100) + env.current()->fees().accountReserve(3) + env.current()->fees().base * 4;

        env.fund(startingXrp, gw1, gw2, gw3, localAlice);
        env.fund(XRP(2'000), localBob);
        env.close();

        env(trust(localAlice, usD1(1'000)));
        env(trust(localAlice, usD2(1'000)));
        env(trust(localAlice, usD3(1'000)));
        env(trust(localBob, usD1(1'200)));
        env(trust(localBob, usD2(1'100)));

        env(pay(gw1, localBob, localBob["USD"](1'200)));

        AMM const ammBob(env, localBob, XRP(1'000), usD1(1'200));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(localAlice, usD1(200), XRP(200)));

        // The pool gets only 100XRP for ~109.09USD, even though
        // it can exchange more.
        BEAST_EXPECT(ammBob.expectBalances(
            XRP(1'100), STAmount{usD1, UINT64_C(1'090'909090909091), -12}, ammBob.tokens()));

        auto jrr = ledgerEntryState(env, localAlice, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "109.090909090909");
        jrr = ledgerEntryRoot(env, localAlice);
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName] == XRP(350).value().getText());
    }

    void
    testOfferCreateThenCross(FeatureBitset features)
    {
        testcase("Offer Create, then Cross");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw_, {alice_, bob_}, XRP(200'000));

        env(rate(gw_, 1.005));

        env(trust(alice_, USD(1'000)));
        env(trust(bob_, USD(1'000)));

        env(pay(gw_, bob_, USD(1)));
        env(pay(gw_, alice_, USD(200)));

        AMM const ammAlice(env, alice_, USD(150), XRP(150'100));
        env(offer(bob_, XRP(100), USD(0.1)));

        BEAST_EXPECT(ammAlice.expectBalances(USD(150.1), XRP(150'000), ammAlice.tokens()));

        auto const jrr = ledgerEntryState(env, bob_, gw_, "USD");
        // Bob pays 0.005 transfer fee. Note 10**-10 round-off.
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-0.8995000001");
    }

    void
    testSellFlagBasic(FeatureBitset features)
    {
        testcase("Offer tfSell: Basic Sell");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(offer(carol_, USD(100), XRP(100)), Json(jss::Flags, tfSell));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(9'999), ammAlice.tokens()));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
                BEAST_EXPECT(expectHolding(env, carol_, USD(30'101)));
                BEAST_EXPECT(
                    expectLedgerEntryRoot(env, carol_, XRP(30'000) - XRP(100) - txFee(env, 1)));
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

        auto const startingXrp = XRP(100) + reserve(env, 1) + env.current()->fees().base * 2;

        env.fund(startingXrp, gw_, alice_);
        env.fund(XRP(2'000), bob_);
        env.close();

        env(trust(alice_, USD(150)));
        env(trust(bob_, USD(4'000)));

        env(pay(gw_, bob_, bob_["USD"](2'200)));

        AMM const ammBob(env, bob_, XRP(1'000), USD(2'200));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100 USD for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more USD than asked for.
        env(offer(alice_, USD(100), XRP(200)), Json(jss::Flags, tfSell));
        BEAST_EXPECT(ammBob.expectBalances(XRP(1'100), USD(2'000), ammBob.tokens()));
        BEAST_EXPECT(expectHolding(env, alice_, USD(200)));
        BEAST_EXPECT(expectLedgerEntryRoot(env, alice_, XRP(250)));
        BEAST_EXPECT(expectOffers(env, alice_, 0));
    }

    void
    testGatewayCrossCurrency(FeatureBitset features)
    {
        testcase("Client Issue: Gateway Cross Currency");

        using namespace jtx;

        Env env{*this, features};

        auto const xts = gw_["XTS"];
        auto const xxx = gw_["XXX"];

        auto const startingXrp = XRP(100.1) + reserve(env, 1) + env.current()->fees().base * 2;
        fund(env, gw_, {alice_, bob_}, startingXrp, {xts(100), xxx(100)}, Fund::All);

        AMM const ammAlice(env, alice_, xts(100), xxx(100));

        json::Value payment;
        payment[jss::secret] = toBase58(generateSeed("bob"));
        payment[jss::id] = env.seq(bob_);
        payment[jss::build_path] = true;
        payment[jss::tx_json] = pay(bob_, bob_, bob_["XXX"](1));
        payment[jss::tx_json][jss::Sequence] =
            env.current()->read(keylet::account(bob_.id()))->getFieldU32(sfSequence);
        payment[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
        payment[jss::tx_json][jss::SendMax] =
            bob_["XTS"](1.5).value().getJson(JsonOptions::Values::None);
        payment[jss::tx_json][jss::Flags] = tfPartialPayment;
        auto const jrr = env.rpc("json", "submit", to_string(payment));
        BEAST_EXPECT(jrr[jss::result][jss::status] == "success");
        BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");
        if (!features[fixAMMv1_1])
        {
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(xts, UINT64_C(101'010101010101), -12), xxx(99), ammAlice.tokens()));
            BEAST_EXPECT(expectHolding(env, bob_, STAmount{xts, UINT64_C(98'989898989899), -12}));
        }
        else
        {
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(xts, UINT64_C(101'0101010101011), -13), xxx(99), ammAlice.tokens()));
            BEAST_EXPECT(expectHolding(env, bob_, STAmount{xts, UINT64_C(98'9898989898989), -13}));
        }
        BEAST_EXPECT(expectHolding(env, bob_, xxx(101)));
    }

    void
    testBridgedCross(FeatureBitset features)
    {
        testcase("Bridged Crossing");

        using namespace jtx;

        {
            Env env{*this, features};

            fund(env, gw_, {alice_, bob_, carol_}, {USD(15'000), EUR(15'000)}, Fund::All);

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP AMM is created.
            //   o carol_ has EUR but wants USD.
            // Note that carol_'s offer must come last.  If carol_'s offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), USD(10'100));
            AMM const ammBob(env, bob_, EUR(10'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol_, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(ammBob.expectBalances(XRP(10'000), EUR(10'100), ammBob.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(15'100)));
            BEAST_EXPECT(expectHolding(env, carol_, EUR(14'900)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
        }

        {
            Env env{*this, features};

            fund(env, gw_, {alice_, bob_, carol_}, {USD(15'000), EUR(15'000)}, Fund::All);

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP offer is created.
            //   o carol_ has EUR but wants USD.
            // Note that carol_'s offer must come last.  If carol_'s offer is
            // placed before AMM and bob_'s offer are created, then autobridging
            // will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), USD(10'100));
            env(offer(bob_, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol_, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(15'100)));
            BEAST_EXPECT(expectHolding(env, carol_, EUR(14'900)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }

        {
            Env env{*this, features};

            fund(env, gw_, {alice_, bob_, carol_}, {USD(15'000), EUR(15'000)}, Fund::All);

            // The scenario:
            //   o USD/XRP offer is created.
            //   o EUR/XRP AMM is created.
            //   o carol_ has EUR but wants USD.
            // Note that carol_'s offer must come last.  If carol_'s offer is
            // placed before AMM and alice_'s offer are created, then
            // autobridging will not occur.
            env(offer(alice_, XRP(100), USD(100)));
            env.close();
            AMM const ammBob(env, bob_, EUR(10'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol_, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(XRP(10'000), EUR(10'100), ammBob.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(15'100)));
            BEAST_EXPECT(expectHolding(env, carol_, EUR(14'900)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, alice_, 0));
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
        TER const killedCode{TER{tecKILLED}};

        {
            Env env{*this, features};
            fund(env, gw_, {alice_, bob_}, {USD(20'000)}, Fund::All);
            AMM const ammBob(env, bob_, XRP(20'000), USD(200));
            // alice_ submits a tfSell | tfFillOrKill offer that does not cross.
            env(offer(alice_, USD(2.1), XRP(210), tfSell | tfFillOrKill), Ter(killedCode));

            BEAST_EXPECT(ammBob.expectBalances(XRP(20'000), USD(200), ammBob.tokens()));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }
        {
            Env env{*this, features};
            fund(env, gw_, {alice_, bob_}, {USD(1'000)}, Fund::All);
            AMM const ammBob(env, bob_, XRP(20'000), USD(200));
            // alice_ submits a tfSell | tfFillOrKill offer that crosses.
            // Even though tfSell is present it doesn't matter this time.
            env(offer(alice_, USD(2), XRP(220), tfSell | tfFillOrKill));
            env.close();
            BEAST_EXPECT(ammBob.expectBalances(
                XRP(20'220), STAmount{USD, UINT64_C(197'8239366963403), -13}, ammBob.tokens()));
            BEAST_EXPECT(
                expectHolding(env, alice_, STAmount{USD, UINT64_C(1'002'17606330366), -11}));
            BEAST_EXPECT(expectOffers(env, alice_, 0));
        }
        {
            // alice_ submits a tfSell | tfFillOrKill offer that crosses and
            // returns more than was asked for (because of the tfSell flag).
            Env env{*this, features};
            fund(env, gw_, {alice_, bob_}, {USD(1'000)}, Fund::All);
            AMM const ammBob(env, bob_, XRP(20'000), USD(200));

            env(offer(alice_, USD(10), XRP(1'500), tfSell | tfFillOrKill));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(
                XRP(21'500), STAmount{USD, UINT64_C(186'046511627907), -12}, ammBob.tokens()));
            BEAST_EXPECT(
                expectHolding(env, alice_, STAmount{USD, UINT64_C(1'013'953488372093), -12}));
            BEAST_EXPECT(expectOffers(env, alice_, 0));
        }
        {
            // alice_ submits a tfSell | tfFillOrKill offer that doesn't cross.
            // This would have succeeded with a regular tfSell, but the
            // fillOrKill prevents the transaction from crossing since not
            // all of the offer is consumed because AMM generated offer,
            // which matches alice_'s offer quality is ~ 10XRP/0.01996USD.
            Env env{*this, features};
            fund(env, gw_, {alice_, bob_}, {USD(10'000)}, Fund::All);
            AMM const ammBob(env, bob_, XRP(5000), USD(10));

            env(offer(alice_, USD(1), XRP(501), tfSell | tfFillOrKill), Ter(tecKILLED));
            env.close();
            BEAST_EXPECT(expectOffers(env, alice_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
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
                env(rate(gw_, 1.25));
                env.close();

                env(offer(carol_, USD(100), XRP(100)));
                env.close();

                // AMM doesn't pay the transfer fee
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
                BEAST_EXPECT(expectHolding(env, carol_, USD(30'100)));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
            },
            {{XRP(10'000), USD(10'100)}},
            0,
            std::nullopt,
            {features});

        // Reverse the order, so the offer in the books is to sell XRP
        // in return for USD.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(rate(gw_, 1.25));
                env.close();

                env(offer(carol_, XRP(100), USD(100)));
                env.close();

                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(10'100), ammAlice.tokens()));
                // Carol pays 25% transfer fee
                BEAST_EXPECT(expectHolding(env, carol_, USD(29'875)));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
            },
            {{XRP(10'100), USD(10'000)}},
            0,
            std::nullopt,
            {features});

        {
            // Bridged crossing.
            Env env{*this, features};
            fund(env, gw_, {alice_, bob_, carol_}, {USD(15'000), EUR(15'000)}, Fund::All);
            env(rate(gw_, 1.25));

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol_ has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), USD(10'100));
            env(offer(bob_, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol_, USD(100), EUR(100)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(15'100)));
            // Carol pays 25% transfer fee.
            BEAST_EXPECT(expectHolding(env, carol_, EUR(14'875)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }

        {
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(env, gw_, {alice_, bob_, carol_}, {USD(15'000), EUR(15'000)}, Fund::All);
            env(rate(gw_, 1.25));

            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol_ has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), USD(10'050));
            env(offer(bob_, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // partially consumes Bob's offer.
            env(offer(carol_, USD(50), EUR(50)));
            env.close();
            // This test verifies that the amount removed from an offer
            // accounts for the transfer fee that is removed from the
            // account but not from the remaining offer.

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'050), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(15'050)));
            // Carol pays 25% transfer fee.
            BEAST_EXPECT(expectHolding(env, carol_, EUR(14'937.5)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 1, {{Amounts{EUR(50), XRP(50)}}}));
        }

        {
            // A trust line's QualityIn should not affect offer crossing.
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(env, gw_, {alice_, carol_, bob_}, XRP(30'000));
            env(rate(gw_, 1.25));
            env(trust(alice_, USD(15'000)));
            env(trust(bob_, EUR(15'000)));
            env(trust(carol_, EUR(15'000)), QualityInPercent(80));
            env(trust(bob_, USD(15'000)));
            env(trust(carol_, USD(15'000)));
            env.close();

            env(pay(gw_, alice_, USD(11'000)));
            env(pay(gw_, carol_, EUR(1'000)), Sendmax(EUR(10'000)));
            env.close();
            // 1000 / 0.8
            BEAST_EXPECT(expectHolding(env, carol_, EUR(1'250)));
            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol_ has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), USD(10'100));
            env(offer(bob_, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol_, USD(100), EUR(100)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(100)));
            // Carol pays 25% transfer fee: 1250 - 100(offer) - 25(transfer fee)
            BEAST_EXPECT(expectHolding(env, carol_, EUR(1'125)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }

        {
            // A trust line's QualityOut should not affect offer crossing.
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            fund(env, gw_, {alice_, carol_, bob_}, XRP(30'000));
            env(rate(gw_, 1.25));
            env(trust(alice_, USD(15'000)));
            env(trust(bob_, EUR(15'000)));
            env(trust(carol_, EUR(15'000)), QualityOutPercent(120));
            env(trust(bob_, USD(15'000)));
            env(trust(carol_, USD(15'000)));
            env.close();

            env(pay(gw_, alice_, USD(11'000)));
            env(pay(gw_, carol_, EUR(1'000)), Sendmax(EUR(10'000)));
            env.close();
            BEAST_EXPECT(expectHolding(env, carol_, EUR(1'000)));
            // The scenario:
            //   o USD/XRP AMM is created.
            //   o EUR/XRP Offer is created.
            //   o carol_ has EUR but wants USD.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), USD(10'100));
            env(offer(bob_, EUR(100), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol_, USD(100), EUR(100)));
            env.close();

            // AMM pay doesn't transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), USD(10'000), ammAlice.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(100)));
            // Carol pays 25% transfer fee: 1000 - 100(offer) - 25(transfer fee)
            BEAST_EXPECT(expectHolding(env, carol_, EUR(875)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
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

        auto const usdBob = bob_["USD"];
        auto const f = env.current()->fees().base;

        env.fund(XRP(30'000) + f, alice_, bob_);
        env.close();
        AMM const ammBob(env, bob_, XRP(10'000), usdBob(10'100));

        env(offer(alice_, usdBob(100), XRP(100)));
        env.close();

        BEAST_EXPECT(ammBob.expectBalances(XRP(10'100), usdBob(10'000), ammBob.tokens()));
        BEAST_EXPECT(expectOffers(env, alice_, 0));
        BEAST_EXPECT(expectHolding(env, alice_, usdBob(100)));
    }

    void
    testBadPathAssert(FeatureBitset features)
    {
        // At one point in the past this invalid path caused assert.  It
        // should not be possible for user-supplied data to cause assert.
        // Make sure assert is gone.
        testcase("Bad path assert");

        using namespace jtx;

        Env env{*this, features};

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;
        {
            // A trust line's QualityOut should not affect offer crossing.
            auto const ann = Account("ann");
            auto const aBux = ann["BUX"];
            auto const localBob = Account("bob");
            auto const cam = Account("cam");
            auto const dan = Account("dan");
            auto const dBux = dan["BUX"];

            // Verify trust line QualityOut affects payments.
            env.fund(reserve(env, 4) + (fee * 4), ann, localBob, cam, dan);
            env.close();

            env(trust(localBob, aBux(400)));
            env(trust(localBob, dBux(200)), QualityOutPercent(120));
            env(trust(cam, dBux(100)));
            env.close();
            env(pay(dan, localBob, dBux(100)));
            env.close();
            BEAST_EXPECT(expectHolding(env, localBob, dBux(100)));

            env(pay(ann, cam, dBux(60)), Path(localBob, dan), Sendmax(aBux(200)));
            env.close();

            BEAST_EXPECT(expectHolding(env, ann, aBux(kNone)));
            BEAST_EXPECT(expectHolding(env, ann, dBux(kNone)));
            BEAST_EXPECT(expectHolding(env, localBob, aBux(72)));
            BEAST_EXPECT(expectHolding(env, localBob, dBux(40)));
            BEAST_EXPECT(expectHolding(env, cam, aBux(kNone)));
            BEAST_EXPECT(expectHolding(env, cam, dBux(60)));
            BEAST_EXPECT(expectHolding(env, dan, aBux(kNone)));
            BEAST_EXPECT(expectHolding(env, dan, dBux(kNone)));

            AMM const ammBob(env, localBob, aBux(30), dBux(30));

            env(trust(ann, dBux(100)));
            env.close();

            // This payment caused the assert.
            env(pay(ann, ann, dBux(30)), Path(aBux, dBux), Sendmax(aBux(30)), Ter(temBAD_PATH));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(aBux(30), dBux(30), ammBob.tokens()));
            BEAST_EXPECT(expectHolding(env, ann, aBux(kNone)));
            BEAST_EXPECT(expectHolding(env, ann, dBux(0)));
            BEAST_EXPECT(expectHolding(env, cam, aBux(kNone)));
            BEAST_EXPECT(expectHolding(env, cam, dBux(60)));
            BEAST_EXPECT(expectHolding(env, dan, aBux(0)));
            BEAST_EXPECT(expectHolding(env, dan, dBux(kNone)));
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
        auto const localBob = Account("bob");
        auto const cam = Account("cam");
        auto const carol = Account("carol");
        auto const aBux = ann["BUX"];
        auto const bBux = localBob["BUX"];

        auto const fee = env.current()->fees().base;
        env.fund(XRP(1'000), carol);
        env.fund(reserve(env, 4) + (fee * 5), ann, localBob, cam);
        env.close();

        env(trust(ann, bBux(40)));
        env(trust(cam, aBux(40)));
        env(trust(localBob, aBux(30)));
        env(trust(cam, bBux(40)));
        env(trust(carol, bBux(400)));
        env(trust(carol, aBux(400)));
        env.close();

        env(pay(ann, cam, aBux(35)));
        env(pay(localBob, cam, bBux(35)));
        env(pay(localBob, carol, bBux(400)));
        env(pay(ann, carol, aBux(400)));

        AMM const ammCarol(env, carol, aBux(300), bBux(330));

        // cam puts an offer on the books that her upcoming offer could cross.
        // But this offer should be deleted, not crossed, by her upcoming
        // offer.
        env(offer(cam, aBux(29), bBux(30), tfPassive));
        env.close();
        env.require(Balance(cam, aBux(35)));
        env.require(Balance(cam, bBux(35)));
        env.require(offers(cam, 1));

        // This offer caused the assert.
        env(offer(cam, bBux(30), aBux(30)));

        // AMM is consumed up to the first cam Offer quality
        if (!features[fixAMMv1_1])
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{aBux, UINT64_C(309'3541659651605), -13},
                STAmount{bBux, UINT64_C(320'0215509984417), -13},
                ammCarol.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                cam,
                1,
                {{Amounts{
                    STAmount{bBux, UINT64_C(20'0215509984417), -13},
                    STAmount{aBux, UINT64_C(20'0215509984417), -13}}}}));
        }
        else
        {
            BEAST_EXPECT(ammCarol.expectBalances(
                STAmount{aBux, UINT64_C(309'3541659651604), -13},
                STAmount{bBux, UINT64_C(320'0215509984419), -13},
                ammCarol.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                cam,
                1,
                {{Amounts{
                    STAmount{bBux, UINT64_C(20'0215509984419), -13},
                    STAmount{aBux, UINT64_C(20'0215509984419), -13}}}}));
        }
    }

    void
    testRequireAuth(FeatureBitset features)
    {
        testcase("lsfRequireAuth");

        using namespace jtx;

        Env env{*this, features};

        auto const aliceUSD = alice_["USD"];
        auto const bobUSD = bob_["USD"];

        env.fund(XRP(400'000), gw_, alice_, bob_);
        env.close();

        // GW requires authorization for holders of its IOUs
        env(fset(gw_, asfRequireAuth));
        env.close();

        // Properly set trust and have gw_ authorize bob_ and alice_
        env(trust(gw_, bobUSD(100)), Txflags(tfSetfAuth));
        env(trust(bob_, USD(100)));
        env(trust(gw_, aliceUSD(100)), Txflags(tfSetfAuth));
        env(trust(alice_, USD(2'000)));
        env(pay(gw_, alice_, USD(1'000)));
        env.close();
        // Alice is able to create AMM since the GW has authorized her
        AMM const ammAlice(env, alice_, USD(1'000), XRP(1'050));

        // Set up authorized trust line for AMM.
        env(trust(gw_, STAmount{Issue{USD.currency, ammAlice.ammAccount()}, 10}),
            Txflags(tfSetfAuth));
        env.close();

        env(pay(gw_, bob_, USD(50)));
        env.close();

        BEAST_EXPECT(expectHolding(env, bob_, USD(50)));

        // Bob's offer should cross Alice's AMM
        env(offer(bob_, XRP(50), USD(50)));
        env.close();

        BEAST_EXPECT(ammAlice.expectBalances(USD(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob_, 0));
        BEAST_EXPECT(expectHolding(env, bob_, USD(0)));
    }

    void
    testMissingAuth(FeatureBitset features)
    {
        testcase("Missing Auth");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(400'000), gw_, alice_, bob_);
        env.close();

        // Alice doesn't have the funds
        {
            AMM const ammAlice(env, alice_, USD(1'000), XRP(1'000), Ter(tecUNFUNDED_AMM));
        }

        env(fset(gw_, asfRequireAuth));
        env.close();

        env(trust(gw_, bob_["USD"](50)), Txflags(tfSetfAuth));
        env.close();
        env(trust(bob_, USD(50)));
        env.close();

        env(pay(gw_, bob_, USD(50)));
        env.close();
        BEAST_EXPECT(expectHolding(env, bob_, USD(50)));

        // Alice should not be able to create AMM without authorization.
        {
            AMM const ammAlice(env, alice_, USD(1'000), XRP(1'000), Ter(tecNO_LINE));
        }

        // Set up a trust line for Alice, but don't authorize it. Alice
        // should still not be able to create AMM for USD/gw_.
        env(trust(gw_, alice_["USD"](2'000)));
        env.close();

        {
            AMM const ammAlice(env, alice_, USD(1'000), XRP(1'000), Ter(tecNO_AUTH));
        }

        // Finally, set up an authorized trust line for Alice. Now Alice's
        // AMM create should succeed.
        env(trust(gw_, alice_["USD"](100)), Txflags(tfSetfAuth));
        env(trust(alice_, USD(2'000)));
        env(pay(gw_, alice_, USD(1'000)));
        env.close();

        AMM const ammAlice(env, alice_, USD(1'000), XRP(1'050));

        // Set up authorized trust line for AMM.
        env(trust(gw_, STAmount{Issue{USD.currency, ammAlice.ammAccount()}, 10}),
            Txflags(tfSetfAuth));
        env.close();

        // Now bob_ creates his offer again, which crosses with  alice_'s AMM.
        env(offer(bob_, XRP(50), USD(50)));
        env.close();

        BEAST_EXPECT(ammAlice.expectBalances(USD(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob_, 0));
        BEAST_EXPECT(expectHolding(env, bob_, USD(0)));
    }

    void
    testOffers()
    {
        using namespace jtx;
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas
        FeatureBitset const all{
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol};

        testRmFundedOffer(all);
        testRmFundedOffer(all - fixAMMv1_1 - fixAMMv1_3);
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
        testGatewayCrossCurrency(all - fixAMMv1_1 - fixAMMv1_3);
        testBridgedCross(all);
        testSellWithFillOrKill(all);
        testTransferRateOffer(all);
        testSelfIssueOffer(all);
        testBadPathAssert(all);
        testSellFlagBasic(all);
        testDirectToDirectPath(all);
        testDirectToDirectPath(all - fixAMMv1_1 - fixAMMv1_3);
        testRequireAuth(all);
        testMissingAuth(all);
    }

    void
    pathFindConsumeAll()
    {
        testcase("path find consume all");
        using namespace jtx;

        Env env = pathTestEnv();
        env.fund(XRP(100'000'250), alice_);
        fund(env, gw_, {carol_, bob_}, {USD(100)}, Fund::All);
        fund(env, gw_, {alice_}, {USD(100)}, Fund::TokenOnly);
        AMM const ammCarol(env, carol_, XRP(100), USD(100));

        STPathSet st;
        STAmount sa;
        STAmount da;
        std::tie(st, sa, da) = findPaths(
            env, alice_, bob_, bob_["AUD"](-1), std::optional<STAmount>(XRP(100'000'000)));
        BEAST_EXPECT(st.empty());
        std::tie(st, sa, da) = findPaths(
            env, alice_, bob_, bob_["USD"](-1), std::optional<STAmount>(XRP(100'000'000)));
        // Alice sends all requested 100,000,000XRP
        BEAST_EXPECT(sa == XRP(100'000'000));
        // Bob gets ~99.99USD. This is the amount Bob
        // can get out of AMM for 100,000,000XRP.
        BEAST_EXPECT(equal(da, STAmount{bob_["USD"], UINT64_C(99'9999000001), -10}));
    }

    // carol_ holds gateway AUD, sells gateway AUD for XRP
    // bob_ will hold gateway AUD
    // alice_ pays bob_ gateway AUD using XRP
    void
    viaOffersViaGateway()
    {
        testcase("via gateway");
        using namespace jtx;

        Env env = pathTestEnv();
        auto const aud = gw_["AUD"];
        env.fund(XRP(10'000), alice_, bob_, carol_, gw_);
        env.close();
        env(rate(gw_, 1.1));
        env.trust(aud(2'000), bob_, carol_);
        env(pay(gw_, carol_, aud(51)));
        env.close();
        AMM const ammCarol(env, carol_, XRP(40), aud(51));
        env(pay(alice_, bob_, aud(10)), Sendmax(XRP(100)), Paths(XRP));
        env.close();
        // AMM offer is 51.282052XRP/11AUD, 11AUD/1.1 = 10AUD to bob_
        BEAST_EXPECT(ammCarol.expectBalances(XRP(51), aud(40), ammCarol.tokens()));
        BEAST_EXPECT(expectHolding(env, bob_, aud(10)));

        auto const result = findPaths(env, alice_, bob_, Account(bob_)["USD"](25));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    receiveMax()
    {
        testcase("Receive max");
        using namespace jtx;
        auto const charlie = Account("charlie");
        {
            // XRP -> IOU receive max
            Env env = pathTestEnv();
            fund(env, gw_, {alice_, bob_, charlie}, {USD(11)}, Fund::All);
            AMM const ammCharlie(env, charlie, XRP(10), USD(11));
            auto [st, sa, da] = findPaths(env, alice_, bob_, USD(-1), XRP(1).value());
            BEAST_EXPECT(sa == XRP(1));
            BEAST_EXPECT(equal(da, USD(1)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw_.id() &&
                    pathElem.getCurrency() == USD.currency);
            }
        }
        {
            // IOU -> XRP receive max
            Env env = pathTestEnv();
            fund(env, gw_, {alice_, bob_, charlie}, {USD(11)}, Fund::All);
            AMM const ammCharlie(env, charlie, XRP(11), USD(10));
            env.close();
            auto [st, sa, da] = findPaths(env, alice_, bob_, drops(-1), USD(1).value());
            BEAST_EXPECT(sa == USD(1));
            BEAST_EXPECT(equal(da, XRP(1)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == xrpAccount() &&
                    pathElem.getCurrency() == xrpCurrency());
            }
        }
    }

    void
    pathFind01()
    {
        testcase("Path Find: XRP -> XRP and XRP -> IOU");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const g1{"G1"};
        Account const g2{"G2"};
        Account const g3{"G3"};
        Account const m1{"M1"};

        env.fund(XRP(100'000), a1);
        env.fund(XRP(10'000), a2);
        env.fund(XRP(1'000), a3, g1, g2, g3);
        env.fund(XRP(20'000), m1);
        env.close();

        env.trust(g1["XYZ"](5'000), a1);
        env.trust(g3["ABC"](5'000), a1);
        env.trust(g2["XYZ"](5'000), a2);
        env.trust(g3["ABC"](5'000), a2);
        env.trust(a2["ABC"](1'000), a3);
        env.trust(g1["XYZ"](100'000), m1);
        env.trust(g2["XYZ"](100'000), m1);
        env.trust(g3["ABC"](100'000), m1);
        env.close();

        env(pay(g1, a1, g1["XYZ"](3'500)));
        env(pay(g3, a1, g3["ABC"](1'200)));
        env(pay(g1, m1, g1["XYZ"](25'000)));
        env(pay(g2, m1, g2["XYZ"](25'000)));
        env(pay(g3, m1, g3["ABC"](25'000)));
        env.close();

        AMM const ammM1G1G2(env, m1, g1["XYZ"](1'000), g2["XYZ"](1'000));
        AMM const ammM1XrpG3(env, m1, XRP(10'000), g3["ABC"](1'000));

        STPathSet st;
        STAmount sa, da;

        {
            auto const& sendAmt = XRP(10);
            std::tie(st, sa, da) = findPaths(env, a1, a2, sendAmt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(st.empty());
        }

        {
            // no path should exist for this since dest account
            // does not exist.
            auto const& sendAmt = XRP(200);
            std::tie(st, sa, da) =
                findPaths(env, a1, Account{"A0"}, sendAmt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(st.empty());
        }

        {
            auto const& sendAmt = g3["ABC"](10);
            std::tie(st, sa, da) = findPaths(env, a2, g3, sendAmt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRPAmount{101'010'102}));
            BEAST_EXPECT(same(st, stpath(ipe(g3["ABC"]))));
        }

        {
            auto const& sendAmt = a2["ABC"](1);
            std::tie(st, sa, da) = findPaths(env, a1, a2, sendAmt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRPAmount{10'010'011}));
            BEAST_EXPECT(same(st, stpath(ipe(g3["ABC"]), g3)));
        }

        {
            auto const& sendAmt = a3["ABC"](1);
            std::tie(st, sa, da) = findPaths(env, a1, a3, sendAmt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRPAmount{10'010'011}));
            BEAST_EXPECT(same(st, stpath(ipe(g3["ABC"]), g3, a2)));
        }
    }

    void
    pathFind02()
    {
        testcase("Path Find: non-XRP -> XRP");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const g3{"G3"};
        Account const m1{"M1"};

        env.fund(XRP(1'000), a1, a2, g3);
        env.fund(XRP(11'000), m1);
        env.close();

        env.trust(g3["ABC"](1'000), a1, a2);
        env.trust(g3["ABC"](100'000), m1);
        env.close();

        env(pay(g3, a1, g3["ABC"](1'000)));
        env(pay(g3, a2, g3["ABC"](1'000)));
        env(pay(g3, m1, g3["ABC"](1'200)));
        env.close();

        AMM const ammM1(env, m1, g3["ABC"](1'000), XRP(10'010));

        STPathSet st;
        STAmount sa, da;

        auto const& sendAmt = XRP(10);
        std::tie(st, sa, da) = findPaths(env, a1, a2, sendAmt, std::nullopt, a2["ABC"].currency);
        BEAST_EXPECT(equal(da, sendAmt));
        BEAST_EXPECT(equal(sa, a1["ABC"](1)));
        BEAST_EXPECT(same(st, stpath(g3, ipe(xrpIssue()))));
    }

    void
    pathFind05()
    {
        testcase("Path Find: non-XRP -> non-XRP, same currency");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const a4{"A4"};
        Account const g1{"G1"};
        Account const g2{"G2"};
        Account const g3{"G3"};
        Account const g4{"G4"};
        Account const m1{"M1"};
        Account const m2{"M2"};

        env.fund(XRP(1'000), a1, a2, a3, g1, g2, g3, g4);
        env.fund(XRP(10'000), a4);
        env.fund(XRP(21'000), m1, m2);
        env.close();

        env.trust(g1["HKD"](2'000), a1);
        env.trust(g2["HKD"](2'000), a2);
        env.trust(g1["HKD"](2'000), a3);
        env.trust(g1["HKD"](100'000), m1);
        env.trust(g2["HKD"](100'000), m1);
        env.trust(g1["HKD"](100'000), m2);
        env.trust(g2["HKD"](100'000), m2);
        env.close();

        env(pay(g1, a1, g1["HKD"](1'000)));
        env(pay(g2, a2, g2["HKD"](1'000)));
        env(pay(g1, a3, g1["HKD"](1'000)));
        env(pay(g1, m1, g1["HKD"](1'200)));
        env(pay(g2, m1, g2["HKD"](5'000)));
        env(pay(g1, m2, g1["HKD"](1'200)));
        env(pay(g2, m2, g2["HKD"](5'000)));
        env.close();

        AMM const ammM1(env, m1, g1["HKD"](1'010), g2["HKD"](1'000));
        AMM const ammM2XrpG2(env, m2, XRP(10'000), g2["HKD"](1'010));
        AMM const ammM2G1Xrp(env, m2, g1["HKD"](1'010), XRP(10'000));

        STPathSet st;
        STAmount sa, da;

        {
            // A) Borrow or repay --
            //  Source -> Destination (repay source issuer)
            auto const& sendAmt = g1["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, g1, sendAmt, std::nullopt, g1["HKD"].currency);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
        }

        {
            // A2) Borrow or repay --
            //  Source -> Destination (repay destination issuer)
            auto const& sendAmt = a1["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, g1, sendAmt, std::nullopt, g1["HKD"].currency);
            BEAST_EXPECT(st.empty());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
        }

        {
            // B) Common gateway --
            //  Source -> AC -> Destination
            auto const& sendAmt = a3["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, a3, sendAmt, std::nullopt, g1["HKD"].currency);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            BEAST_EXPECT(same(st, stpath(g1)));
        }

        {
            // C) Gateway to gateway --
            //  Source -> OB -> Destination
            auto const& sendAmt = g2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, g1, g2, sendAmt, std::nullopt, g1["HKD"].currency);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, g1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(ipe(g2["HKD"])),
                stpath(m1),
                stpath(m2),
                stpath(ipe(xrpIssue()), ipe(g2["HKD"]))));
        }

        {
            // D) User to unlinked gateway via order book --
            //  Source -> AC -> OB -> Destination
            auto const& sendAmt = g2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, g2, sendAmt, std::nullopt, g1["HKD"].currency);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(g1, m1),
                stpath(g1, m2),
                stpath(g1, ipe(g2["HKD"])),
                stpath(g1, ipe(xrpIssue()), ipe(g2["HKD"]))));
        }

        {
            // I4) XRP bridge" --
            //  Source -> AC -> OB to XRP -> OB from XRP -> AC ->
            //  Destination
            auto const& sendAmt = a2["HKD"](10);
            std::tie(st, sa, da) =
                findPaths(env, a1, a2, sendAmt, std::nullopt, g1["HKD"].currency);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, a1["HKD"](10)));
            BEAST_EXPECT(same(
                st,
                stpath(g1, m1, g2),
                stpath(g1, m2, g2),
                stpath(g1, ipe(g2["HKD"]), g2),
                stpath(g1, ipe(xrpIssue()), ipe(g2["HKD"]), g2)));
        }
    }

    void
    pathFind06()
    {
        testcase("Path Find: non-XRP -> non-XRP, same currency");
        using namespace jtx;
        Env env = pathTestEnv();
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const g1{"G1"};
        Account const g2{"G2"};
        Account const m1{"M1"};

        env.fund(XRP(11'000), m1);
        env.fund(XRP(1'000), a1, a2, a3, g1, g2);
        env.close();

        env.trust(g1["HKD"](2'000), a1);
        env.trust(g2["HKD"](2'000), a2);
        env.trust(a2["HKD"](2'000), a3);
        env.trust(g1["HKD"](100'000), m1);
        env.trust(g2["HKD"](100'000), m1);
        env.close();

        env(pay(g1, a1, g1["HKD"](1'000)));
        env(pay(g2, a2, g2["HKD"](1'000)));
        env(pay(g1, m1, g1["HKD"](5'000)));
        env(pay(g2, m1, g2["HKD"](5'000)));
        env.close();

        AMM const ammM1(env, m1, g1["HKD"](1'010), g2["HKD"](1'000));

        // E) Gateway to user
        //  Source -> OB -> AC -> Destination
        auto const& sendAmt = a2["HKD"](10);
        STPathSet st;
        STAmount sa, da;
        std::tie(st, sa, da) = findPaths(env, g1, a2, sendAmt, std::nullopt, g1["HKD"].currency);
        BEAST_EXPECT(equal(da, sendAmt));
        BEAST_EXPECT(equal(sa, g1["HKD"](10)));
        BEAST_EXPECT(same(st, stpath(m1, g2), stpath(ipe(g2["HKD"]), g2)));
    }

    void
    testFalseDry(FeatureBitset features)
    {
        testcase("falseDryChanges");

        using namespace jtx;

        Env env(*this, features);

        env.fund(XRP(10'000), alice_, gw_);
        // This removes no ripple for carol_,
        // different from the original test
        fund(env, gw_, {carol_}, XRP(10'000), {}, Fund::Acct);
        auto const ammXrpPool = env.current()->fees().increment * 2;
        env.fund(reserve(env, 5) + ammCrtFee(env) + ammXrpPool, bob_);
        env.close();
        env.trust(USD(1'000), alice_, bob_, carol_);
        env.trust(EUR(1'000), alice_, bob_, carol_);

        env(pay(gw_, alice_, EUR(50)));
        env(pay(gw_, bob_, USD(150)));

        // Bob has _just_ slightly less than 50 xrp available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incoming xrp to the XRP/USD offer will require two
        // recursive calls to the EUR/XRP offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry.
        // This is the second error case to test (when flowV1 is used).
        env(offer(bob_, EUR(50), XRP(50)));
        AMM const ammBob(env, bob_, ammXrpPool, USD(150));

        env(pay(alice_, carol_, USD(1'000'000)),
            Path(~XRP, ~USD),
            Sendmax(EUR(500)),
            Txflags(tfNoRippleDirect | tfPartialPayment));

        auto const carolUSD = env.balance(carol_, USD).value();
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

            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000), {BTC(100), USD(150)}, Fund::All);

            AMM const ammBob(env, bob_, BTC(100), USD(150));

            env(pay(alice_, carol_, USD(50)), Path(~USD), Sendmax(BTC(50)));

            BEAST_EXPECT(expectHolding(env, alice_, BTC(50)));
            BEAST_EXPECT(expectHolding(env, bob_, BTC(0)));
            BEAST_EXPECT(expectHolding(env, bob_, USD(0)));
            BEAST_EXPECT(expectHolding(env, carol_, USD(200)));
            BEAST_EXPECT(ammBob.expectBalances(BTC(150), USD(100), ammBob.tokens()));
        }
        {
            // simple IOU/XRP XRP/IOU offer
            Env env(*this, features);

            fund(env, gw_, {alice_, carol_, bob_}, XRP(10'000), {BTC(100), USD(150)}, Fund::All);

            AMM const ammBobBtcXrp(env, bob_, BTC(100), XRP(150));
            AMM const ammBobXrpUsd(env, bob_, XRP(100), USD(150));

            env(pay(alice_, carol_, USD(50)), Path(~XRP, ~USD), Sendmax(BTC(50)));

            BEAST_EXPECT(expectHolding(env, alice_, BTC(50)));
            BEAST_EXPECT(expectHolding(env, bob_, BTC(0)));
            BEAST_EXPECT(expectHolding(env, bob_, USD(0)));
            BEAST_EXPECT(expectHolding(env, carol_, USD(200)));
            BEAST_EXPECT(ammBobBtcXrp.expectBalances(BTC(150), XRP(100), ammBobBtcXrp.tokens()));
            BEAST_EXPECT(ammBobXrpUsd.expectBalances(XRP(150), USD(100), ammBobXrpUsd.tokens()));
        }
        {
            // simple XRP -> USD through offer and sendmax
            Env env(*this, features);

            fund(env, gw_, {alice_, carol_, bob_}, XRP(10'000), {USD(150)}, Fund::All);

            AMM const ammBob(env, bob_, XRP(100), USD(150));

            env(pay(alice_, carol_, USD(50)), Path(~USD), Sendmax(XRP(50)));

            BEAST_EXPECT(expectLedgerEntryRoot(env, alice_, xrpMinusFee(env, 10'000 - 50)));
            BEAST_EXPECT(expectLedgerEntryRoot(env, bob_, XRP(10'000) - XRP(100) - ammCrtFee(env)));
            BEAST_EXPECT(expectHolding(env, bob_, USD(0)));
            BEAST_EXPECT(expectHolding(env, carol_, USD(200)));
            BEAST_EXPECT(ammBob.expectBalances(XRP(150), USD(100), ammBob.tokens()));
        }
        {
            // simple USD -> XRP through offer and sendmax
            Env env(*this, features);

            fund(env, gw_, {alice_, carol_, bob_}, XRP(10'000), {USD(100)}, Fund::All);

            AMM const ammBob(env, bob_, USD(100), XRP(150));

            env(pay(alice_, carol_, XRP(50)), Path(~XRP), Sendmax(USD(50)));

            BEAST_EXPECT(expectHolding(env, alice_, USD(50)));
            BEAST_EXPECT(expectLedgerEntryRoot(env, bob_, XRP(10'000) - XRP(150) - ammCrtFee(env)));
            BEAST_EXPECT(expectHolding(env, bob_, USD(0)));
            BEAST_EXPECT(expectLedgerEntryRoot(env, carol_, XRP(10'000 + 50)));
            BEAST_EXPECT(ammBob.expectBalances(USD(150), XRP(100), ammBob.tokens()));
        }
        {
            // test unfunded offers are removed when payment succeeds
            Env env(*this, features);

            env.fund(XRP(10'000), alice_, carol_, gw_);
            env.fund(XRP(10'000), bob_);
            env.close();
            env.trust(USD(1'000), alice_, bob_, carol_);
            env.trust(BTC(1'000), alice_, bob_, carol_);
            env.trust(EUR(1'000), alice_, bob_, carol_);
            env.close();

            env(pay(gw_, alice_, BTC(60)));
            env(pay(gw_, bob_, USD(200)));
            env(pay(gw_, bob_, EUR(150)));
            env.close();

            env(offer(bob_, BTC(50), USD(50)));
            env(offer(bob_, BTC(40), EUR(50)));
            env.close();
            AMM const ammBob(env, bob_, EUR(100), USD(150));

            // unfund offer
            env(pay(bob_, gw_, EUR(50)));
            BEAST_EXPECT(isOffer(env, bob_, BTC(50), USD(50)));
            BEAST_EXPECT(isOffer(env, bob_, BTC(40), EUR(50)));

            env(pay(alice_, carol_, USD(50)), Path(~USD), Path(~EUR, ~USD), Sendmax(BTC(60)));

            env.require(Balance(alice_, BTC(10)));
            env.require(Balance(bob_, BTC(50)));
            env.require(Balance(bob_, USD(0)));
            env.require(Balance(bob_, EUR(0)));
            env.require(Balance(carol_, USD(50)));
            // used in the payment
            BEAST_EXPECT(!isOffer(env, bob_, BTC(50), USD(50)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob_, BTC(40), EUR(50)));
            // unchanged
            BEAST_EXPECT(ammBob.expectBalances(EUR(100), USD(150), ammBob.tokens()));
        }
        {
            // test unfunded offers are removed when the payment fails.
            // bob_ makes two offers: a funded 50 USD for 50 BTC and an
            // unfunded 50 EUR for 60 BTC. alice_ pays carol_ 61 USD with 61
            // BTC. alice_ only has 60 BTC, so the payment will fail. The
            // payment uses two paths: one through bob_'s funded offer and
            // one through his unfunded offer. When the payment fails `flow`
            // should return the unfunded offer. This test is intentionally
            // similar to the one that removes unfunded offers when the
            // payment succeeds.
            Env env(*this, features);

            env.fund(XRP(10'000), bob_, carol_, gw_);
            env.close();
            // Sets rippling on, this is different from
            // the original test
            fund(env, gw_, {alice_}, XRP(10'000), {}, Fund::Acct);
            env.trust(USD(1'000), alice_, bob_, carol_);
            env.trust(BTC(1'000), alice_, bob_, carol_);
            env.trust(EUR(1'000), alice_, bob_, carol_);
            env.close();

            env(pay(gw_, alice_, BTC(60)));
            env(pay(gw_, bob_, BTC(100)));
            env(pay(gw_, bob_, USD(100)));
            env(pay(gw_, bob_, EUR(50)));
            env(pay(gw_, carol_, EUR(1)));
            env.close();

            // This is multiplath, which generates limited # of offers
            AMM const ammBobBtcUsd(env, bob_, BTC(50), USD(50));
            env(offer(bob_, BTC(60), EUR(50)));
            env(offer(carol_, BTC(1'000), EUR(1)));
            env(offer(bob_, EUR(50), USD(50)));

            // unfund offer
            env(pay(bob_, gw_, EUR(50)));
            BEAST_EXPECT(ammBobBtcUsd.expectBalances(BTC(50), USD(50), ammBobBtcUsd.tokens()));
            BEAST_EXPECT(isOffer(env, bob_, BTC(60), EUR(50)));
            BEAST_EXPECT(isOffer(env, carol_, BTC(1'000), EUR(1)));
            BEAST_EXPECT(isOffer(env, bob_, EUR(50), USD(50)));

            auto flowJournal = env.app().getJournal("Flow");
            auto const flowResult = [&] {
                STAmount const deliver(USD(51));
                STAmount smax(BTC(61));
                PaymentSandbox sb(env.current().get(), TapNone);
                STPathSet paths;
                auto ipe = [](Issue const& iss) {
                    return STPathElement(
                        STPathElement::TypeCurrency | STPathElement::TypeIssuer,
                        xrpAccount(),
                        iss.currency,
                        iss.account);
                };
                {
                    // BTC -> USD
                    STPath const p1({ipe(USD)});
                    paths.pushBack(p1);
                    // BTC -> EUR -> USD
                    STPath const p2({ipe(EUR), ipe(USD)});
                    paths.pushBack(p2);
                }

                return flow(
                    sb,
                    deliver,
                    alice_,
                    carol_,
                    paths,
                    false,
                    false,
                    true,
                    OfferCrossing::No,
                    std::nullopt,
                    smax,
                    std::nullopt,
                    flowJournal);
            }();

            BEAST_EXPECT(flowResult.removableOffers.size() == 1);
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                if (flowResult.removableOffers.empty())
                    return false;
                Sandbox sb(&view, TapNone);
                for (auto const& o : flowResult.removableOffers)
                {
                    if (auto ok = sb.peek(keylet::offer(o)))
                        offerDelete(sb, ok, flowJournal);
                }
                sb.apply(view);
                return true;
            });

            // used in payment, but since payment failed should be untouched
            BEAST_EXPECT(ammBobBtcUsd.expectBalances(BTC(50), USD(50), ammBobBtcUsd.tokens()));
            BEAST_EXPECT(isOffer(env, carol_, BTC(1'000), EUR(1)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob_, BTC(60), EUR(50)));
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
            env.fund(XRP(10'000), bob_, carol_, gw_);
            env.close();
            fund(env, gw_, {alice_}, XRP(10'000), {}, Fund::Acct);
            env.trust(USD(1'000), alice_, bob_, carol_);
            env.trust(EUR(1'000), alice_, bob_, carol_);
            env.close();

            env(pay(gw_, alice_, USD(1'000)));
            env(pay(gw_, bob_, EUR(1'000)));
            env(pay(gw_, bob_, USD(1'000)));
            env.close();

            // env(offer(bob_, USD(1), drops(2)), txflags(tfPassive));
            AMM const ammBob(env, bob_, USD(8), XRPAmount{21});
            env(offer(bob_, drops(1), EUR(1'000)), Txflags(tfPassive));

            env(pay(alice_, carol_, EUR(1)),
                Path(~XRP, ~EUR),
                Sendmax(USD(0.4)),
                Txflags(tfNoRippleDirect | tfPartialPayment));

            BEAST_EXPECT(expectHolding(env, carol_, EUR(1)));
            BEAST_EXPECT(ammBob.expectBalances(USD(8.4), XRPAmount{20}, ammBob.tokens()));
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

            fund(env, gw_, {alice_, bob_, carol_}, XRP(1'000), {USD(1'000), GBP(1'000)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm(env, bob_, GBP(1'000), USD(1'000));

            env(pay(alice_, carol_, USD(100)),
                Path(~USD),
                Sendmax(GBP(150)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice_ buys 107.1428USD with 120GBP and pays 25% tr fee on 120GBP
            // 1,000 - 120*1.25 = 850GBP
            BEAST_EXPECT(expectHolding(env, alice_, GBP(850)));
            if (!features[fixAMMv1_1])
            {
                // 120GBP is swapped in for 107.1428USD
                BEAST_EXPECT(amm.expectBalances(
                    GBP(1'120), STAmount{USD, UINT64_C(892'8571428571428), -13}, amm.tokens()));
            }
            else
            {
                BEAST_EXPECT(amm.expectBalances(
                    GBP(1'120), STAmount{USD, UINT64_C(892'8571428571429), -13}, amm.tokens()));
            }
            // 25% of 85.7142USD is paid in tr fee
            // 85.7142*1.25 = 107.1428USD
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount(USD, UINT64_C(1'085'714285714286), -12)));
        }

        {
            // Payment via offer and AMM
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw_,
                {alice_, bob_, carol_, ed},
                XRP(1'000),
                {USD(1'000), EUR(1'000), GBP(1'000)});
            env(rate(gw_, 1.25));
            env.close();

            env(offer(ed, GBP(1'000), EUR(1'000)), Txflags(tfPassive));
            env.close();

            AMM const amm(env, bob_, EUR(1'000), USD(1'000));

            env(pay(alice_, carol_, USD(100)),
                Path(~EUR, ~USD),
                Sendmax(GBP(150)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice_ buys 120EUR with 120GBP via the offer
            // and pays 25% tr fee on 120GBP
            // 1,000 - 120*1.25 = 850GBP
            BEAST_EXPECT(expectHolding(env, alice_, GBP(850)));
            // consumed offer is 120GBP/120EUR
            // ed doesn't pay tr fee
            BEAST_EXPECT(expectHolding(env, ed, EUR(880), GBP(1'120)));
            BEAST_EXPECT(expectOffers(env, ed, 1, {Amounts{GBP(880), EUR(880)}}));
            // 25% on 96EUR is paid in tr fee 96*1.25 = 120EUR
            // 96EUR is swapped in for 87.5912USD
            BEAST_EXPECT(amm.expectBalances(
                EUR(1'096), STAmount{USD, UINT64_C(912'4087591240876), -13}, amm.tokens()));
            // 25% on 70.0729USD is paid in tr fee 70.0729*1.25 = 87.5912USD
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount(USD, UINT64_C(1'070'07299270073), -11)));
        }
        {
            // Payment via AMM, AMM
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw_,
                {alice_, bob_, carol_, ed},
                XRP(1'000),
                {USD(1'000), EUR(1'000), GBP(1'000)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm1(env, bob_, GBP(1'000), EUR(1'000));
            AMM const amm2(env, ed, EUR(1'000), USD(1'000));

            env(pay(alice_, carol_, USD(100)),
                Path(~EUR, ~USD),
                Sendmax(GBP(150)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            BEAST_EXPECT(expectHolding(env, alice_, GBP(850)));
            if (!features[fixAMMv1_1])
            {
                // alice_ buys 107.1428EUR with 120GBP and pays 25% tr fee on
                // 120GBP 1,000 - 120*1.25 = 850GBP 120GBP is swapped in for
                // 107.1428EUR
                BEAST_EXPECT(amm1.expectBalances(
                    GBP(1'120), STAmount{EUR, UINT64_C(892'8571428571428), -13}, amm1.tokens()));
                // 25% on 85.7142EUR is paid in tr fee 85.7142*1.25 =
                // 107.1428EUR 85.7142EUR is swapped in for 78.9473USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount(EUR, UINT64_C(1'085'714285714286), -12),
                    STAmount{USD, UINT64_C(921'0526315789471), -13},
                    amm2.tokens()));
            }
            else
            {
                // alice_ buys 107.1428EUR with 120GBP and pays 25% tr fee on
                // 120GBP 1,000 - 120*1.25 = 850GBP 120GBP is swapped in for
                // 107.1428EUR
                BEAST_EXPECT(amm1.expectBalances(
                    GBP(1'120), STAmount{EUR, UINT64_C(892'8571428571429), -13}, amm1.tokens()));
                // 25% on 85.7142EUR is paid in tr fee 85.7142*1.25 =
                // 107.1428EUR 85.7142EUR is swapped in for 78.9473USD
                BEAST_EXPECT(amm2.expectBalances(
                    STAmount(EUR, UINT64_C(1'085'714285714286), -12),
                    STAmount{USD, UINT64_C(921'052631578948), -12},
                    amm2.tokens()));
            }
            // 25% on 63.1578USD is paid in tr fee 63.1578*1.25 = 78.9473USD
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount(USD, UINT64_C(1'063'157894736842), -12)));
        }
        {
            // AMM offer crossing
            Env env(*this, features);

            fund(env, gw_, {alice_, bob_}, XRP(1'000), {USD(1'100), EUR(1'100)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm(env, bob_, USD(1'000), EUR(1'100));
            env(offer(alice_, EUR(100), USD(100)));
            env.close();

            // 100USD is swapped in for 100EUR
            BEAST_EXPECT(amm.expectBalances(USD(1'100), EUR(1'000), amm.tokens()));
            // alice_ pays 25% tr fee on 100USD 1100-100*1.25 = 975USD
            BEAST_EXPECT(expectHolding(env, alice_, USD(975), EUR(1'200)));
            BEAST_EXPECT(expectOffers(env, alice_, 0));
        }

        {
            // Payment via AMM with limit quality
            Env env(*this, features);

            fund(env, gw_, {alice_, bob_, carol_}, XRP(1'000), {USD(1'000), GBP(1'000)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm(env, bob_, GBP(1'000), USD(1'000));

            // requested quality limit is 100USD/178.58GBP = 0.55997
            // trade quality is 100USD/178.5714 = 0.55999
            env(pay(alice_, carol_, USD(100)),
                Path(~USD),
                Sendmax(GBP(178.58)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice_ buys 125USD with 142.8571GBP and pays 25% tr fee
            // on 142.8571GBP
            // 1,000 - 142.8571*1.25 = 821.4285GBP
            BEAST_EXPECT(
                expectHolding(env, alice_, STAmount(GBP, UINT64_C(821'4285714285712), -13)));
            // 142.8571GBP is swapped in for 125USD
            BEAST_EXPECT(amm.expectBalances(
                STAmount{GBP, UINT64_C(1'142'857142857143), -12}, USD(875), amm.tokens()));
            // 25% on 100USD is paid in tr fee
            // 100*1.25 = 125USD
            BEAST_EXPECT(expectHolding(env, carol_, USD(1'100)));
        }
        {
            // Payment via AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);

            fund(env, gw_, {alice_, bob_, carol_}, XRP(1'000), {USD(1'200), GBP(1'200)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm(env, bob_, GBP(1'000), USD(1'200));

            // requested quality limit is 90USD/120GBP = 0.75
            // trade quality is 22.5USD/30GBP = 0.75
            env(pay(alice_, carol_, USD(90)),
                Path(~USD),
                Sendmax(GBP(120)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice_ buys 28.125USD with 24GBP and pays 25% tr fee
                // on 24GBP
                // 1,200 - 24*1.25 = 1,170GBP
                BEAST_EXPECT(expectHolding(env, alice_, GBP(1'170)));
                // 24GBP is swapped in for 28.125USD
                BEAST_EXPECT(amm.expectBalances(GBP(1'024), USD(1'171.875), amm.tokens()));
            }
            else
            {
                // alice_ buys 28.125USD with 24GBP and pays 25% tr fee
                // on 24GBP
                // 1,200 - 24*1.25 =~ 1,170GBP
                BEAST_EXPECT(
                    expectHolding(env, alice_, STAmount{GBP, UINT64_C(1'169'999999999999), -12}));
                // 24GBP is swapped in for 28.125USD
                BEAST_EXPECT(amm.expectBalances(
                    STAmount{GBP, UINT64_C(1'024'000000000001), -12},
                    USD(1'171.875),
                    amm.tokens()));
            }
            // 25% on 22.5USD is paid in tr fee
            // 22.5*1.25 = 28.125USD
            BEAST_EXPECT(expectHolding(env, carol_, USD(1'222.5)));
        }
        {
            // Payment via offer and AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw_,
                {alice_, bob_, carol_, ed},
                XRP(1'000),
                {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw_, 1.25));
            env.close();

            env(offer(ed, GBP(1'000), EUR(1'000)), Txflags(tfPassive));
            env.close();

            AMM const amm(env, bob_, EUR(1'000), USD(1'400));

            // requested quality limit is 95USD/140GBP = 0.6785
            // trade quality is 59.7321USD/88.0262GBP = 0.6785
            env(pay(alice_, carol_, USD(95)),
                Path(~EUR, ~USD),
                Sendmax(GBP(140)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice_ buys 70.4210EUR with 70.4210GBP via the offer
                // and pays 25% tr fee on 70.4210GBP
                // 1,400 - 70.4210*1.25 = 1400 - 88.0262 = 1311.9736GBP
                BEAST_EXPECT(
                    expectHolding(env, alice_, STAmount{GBP, UINT64_C(1'311'973684210527), -12}));
                // ed doesn't pay tr fee, the balances reflect consumed offer
                // 70.4210GBP/70.4210EUR
                BEAST_EXPECT(expectHolding(
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
                // alice_ buys 70.4210EUR with 70.4210GBP via the offer
                // and pays 25% tr fee on 70.4210GBP
                // 1,400 - 70.4210*1.25 = 1400 - 88.0262 = 1311.9736GBP
                BEAST_EXPECT(
                    expectHolding(env, alice_, STAmount{GBP, UINT64_C(1'311'973684210525), -12}));
                // ed doesn't pay tr fee, the balances reflect consumed offer
                // 70.4210GBP/70.4210EUR
                BEAST_EXPECT(expectHolding(
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
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount(USD, UINT64_C(1'459'732142857143), -12)));
        }
        {
            // Payment via AMM and offer with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw_,
                {alice_, bob_, carol_, ed},
                XRP(1'000),
                {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm(env, bob_, GBP(1'000), EUR(1'000));

            env(offer(ed, EUR(1'000), USD(1'400)), Txflags(tfPassive));
            env.close();

            // requested quality limit is 95USD/140GBP = 0.6785
            // trade quality is 47.7857USD/70.4210GBP = 0.6785
            env(pay(alice_, carol_, USD(95)),
                Path(~EUR, ~USD),
                Sendmax(GBP(140)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice_ buys 53.3322EUR with 56.3368GBP via the amm
                // and pays 25% tr fee on 56.3368GBP
                // 1,400 - 56.3368*1.25 = 1400 - 70.4210 = 1329.5789GBP
                BEAST_EXPECT(
                    expectHolding(env, alice_, STAmount{GBP, UINT64_C(1'329'578947368421), -12}));
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
                // alice_ buys 53.3322EUR with 56.3368GBP via the amm
                // and pays 25% tr fee on 56.3368GBP
                // 1,400 - 56.3368*1.25 = 1400 - 70.4210 = 1329.5789GBP
                BEAST_EXPECT(
                    expectHolding(env, alice_, STAmount{GBP, UINT64_C(1'329'57894736842), -11}));
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
            BEAST_EXPECT(expectHolding(
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
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount(USD, UINT64_C(1'447'785714285714), -12)));
        }
        {
            // Payment via AMM, AMM  with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            fund(
                env,
                gw_,
                {alice_, bob_, carol_, ed},
                XRP(1'000),
                {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm1(env, bob_, GBP(1'000), EUR(1'000));
            AMM const amm2(env, ed, EUR(1'000), USD(1'400));

            // requested quality limit is 90USD/145GBP = 0.6206
            // trade quality is 66.7432USD/107.5308GBP = 0.6206
            env(pay(alice_, carol_, USD(90)),
                Path(~EUR, ~USD),
                Sendmax(GBP(145)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            if (!features[fixAMMv1_1])
            {
                // alice_ buys 53.3322EUR with 107.5308GBP
                // 25% on 86.0246GBP is paid in tr fee
                // 1,400 - 86.0246*1.25 = 1400 - 107.5308 = 1229.4691GBP
                BEAST_EXPECT(
                    expectHolding(env, alice_, STAmount{GBP, UINT64_C(1'292'469135802469), -12}));
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
                // alice_ buys 53.3322EUR with 107.5308GBP
                // 25% on 86.0246GBP is paid in tr fee
                // 1,400 - 86.0246*1.25 = 1400 - 107.5308 = 1229.4691GBP
                BEAST_EXPECT(
                    expectHolding(env, alice_, STAmount{GBP, UINT64_C(1'292'469135802466), -12}));
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
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount(USD, UINT64_C(1'466'743295019157), -12)));
        }
        {
            // Payment by the issuer via AMM, AMM  with limit quality,
            // deliver less than requested
            Env env(*this, features);

            fund(
                env, gw_, {alice_, bob_, carol_}, XRP(1'000), {USD(1'400), EUR(1'400), GBP(1'400)});
            env(rate(gw_, 1.25));
            env.close();

            AMM const amm1(env, alice_, GBP(1'000), EUR(1'000));
            AMM const amm2(env, bob_, EUR(1'000), USD(1'400));

            // requested quality limit is 90USD/120GBP = 0.75
            // trade quality is 81.1111USD/108.1481GBP = 0.75
            env(pay(gw_, carol_, USD(90)),
                Path(~EUR, ~USD),
                Sendmax(GBP(120)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
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
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount{USD, UINT64_C(1'481'111111111111), -12}));
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

            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000), {USD(2'000)});

            AMM const ammBob(env, bob_, XRP(1'000), USD(1'050));
            env(offer(bob_, XRP(100), USD(50)));

            env(pay(alice_, carol_, USD(100)),
                Path(~USD),
                Sendmax(XRP(100)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));

            BEAST_EXPECT(ammBob.expectBalances(XRP(1'050), USD(1'000), ammBob.tokens()));
            BEAST_EXPECT(expectHolding(env, carol_, USD(2'050)));
            BEAST_EXPECT(expectOffers(env, bob_, 1, {{{XRP(100), USD(50)}}}));
        }
    }

    void
    testXRPPathLoop()
    {
        testcase("Circular XRP");

        using namespace jtx;
        {
            // Payment path starting with XRP
            Env env(*this, testableAmendments());
            // Note, if alice_ doesn't have default ripple, then pay
            // fails with tecPATH_DRY.
            fund(env, gw_, {alice_, bob_}, XRP(10'000), {USD(200), EUR(200)}, Fund::All);

            AMM const ammAliceXrpUsd(env, alice_, XRP(100), USD(101));
            AMM const ammAliceXrpEur(env, alice_, XRP(100), EUR(101));
            env.close();

            TER const expectedTer = TER{temBAD_PATH_LOOP};
            env(pay(alice_, bob_, EUR(1)),
                Path(~USD, ~XRP, ~EUR),
                Sendmax(XRP(1)),
                Txflags(tfNoRippleDirect),
                Ter(expectedTer));
        }
        {
            // Payment path ending with XRP
            Env env(*this);
            // Note, if alice_ doesn't have default ripple, then pay fails
            // with tecPATH_DRY.
            fund(env, gw_, {alice_, bob_}, XRP(10'000), {USD(200), EUR(200)}, Fund::All);

            AMM const ammAliceXrpUsd(env, alice_, XRP(100), USD(100));
            AMM const ammAliceXrpEur(env, alice_, XRP(100), EUR(100));
            // EUR -> //XRP -> //USD ->XRP
            env(pay(alice_, bob_, XRP(1)),
                Path(~XRP, ~USD, ~XRP),
                Sendmax(EUR(1)),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
        }
        {
            // Payment where loop is formed in the middle of the path, not
            // on an endpoint
            auto const jpy = gw_["JPY"];
            Env env(*this);
            // Note, if alice_ doesn't have default ripple, then pay fails
            // with tecPATH_DRY.
            fund(env, gw_, {alice_, bob_}, XRP(10'000), {USD(200), EUR(200), jpy(200)}, Fund::All);

            AMM const ammAliceXrpUsd(env, alice_, XRP(100), USD(100));
            AMM const ammAliceXrpEur(env, alice_, XRP(100), EUR(100));
            AMM const ammAliceXrpJpy(env, alice_, XRP(100), jpy(100));

            env(pay(alice_, bob_, jpy(1)),
                Path(~XRP, ~EUR, ~XRP, ~jpy),
                Sendmax(USD(1)),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
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

        fund(env, gw_, {ed}, XRP(100'000'000), {USD(11)});
        env.fund(XRP(100'000'000), alice_, bob_, carol_, dan);
        env.close();
        env.trust(USD(1), bob_);
        env(pay(gw_, bob_, USD(1)));
        env.trust(USD(1), dan);
        env(pay(gw_, dan, USD(1)));
        nOffers(env, 2'000, bob_, XRP(1), USD(1));
        nOffers(env, 1, dan, XRP(1), USD(1));
        AMM const ammEd(env, ed, XRP(9), USD(11));

        // Alice offers to buy 1000 XRP for 1000 USD. She takes Bob's first
        // offer, removes 999 more as unfunded, then hits the step limit.
        env(offer(alice_, USD(1'000), XRP(1'000)));
        if (!features[fixAMMv1_1])
        {
            env.require(Balance(alice_, STAmount{USD, UINT64_C(2'050126257867561), -15}));
        }
        else
        {
            env.require(Balance(alice_, STAmount{USD, UINT64_C(2'050125257867587), -15}));
        }
        env.require(Owners(alice_, 2));
        env.require(Balance(bob_, USD(0)));
        env.require(Owners(bob_, 1'001));
        env.require(Balance(dan, USD(1)));
        env.require(Owners(dan, 2));

        // Carol offers to buy 1000 XRP for 1000 USD. She removes Bob's next
        // 1000 offers as unfunded and hits the step limit.
        env(offer(carol_, USD(1'000), XRP(1'000)));
        env.require(Balance(carol_, USD(kNone)));
        env.require(Owners(carol_, 1));
        env.require(Balance(bob_, USD(0)));
        env.require(Owners(bob_, 1));
        env.require(Balance(dan, USD(1)));
        env.require(Owners(dan, 2));
    }

    void
    testConvertAllOfAnAsset(FeatureBitset features)
    {
        testcase("Convert all of an asset using DeliverMin");

        using namespace jtx;

        {
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));
            env.trust(USD(100), alice_, bob_, carol_);
            env(pay(alice_, bob_, USD(10)), DeliverMin(USD(10)), Ter(temBAD_AMOUNT));
            env(pay(alice_, bob_, USD(10)),
                DeliverMin(USD(-5)),
                Txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(alice_, bob_, USD(10)),
                DeliverMin(XRP(5)),
                Txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(alice_, bob_, USD(10)),
                DeliverMin(Account(carol_)["USD"](5)),
                Txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(alice_, bob_, USD(10)),
                DeliverMin(USD(15)),
                Txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(gw_, carol_, USD(50)));
            AMM const ammCarol(env, carol_, XRP(10), USD(15));
            env(pay(alice_, bob_, USD(10)),
                Paths(XRP),
                DeliverMin(USD(7)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(5)),
                Ter(tecPATH_PARTIAL));
            env.require(
                Balance(alice_, drops(10'000'000'000 - env.current()->fees().base.drops())));
            env.require(Balance(bob_, XRP(10'000)));
        }

        {
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_}, XRP(10'000));
            env.trust(USD(1'100), alice_, bob_);
            env(pay(gw_, bob_, USD(1'100)));
            AMM const ammBob(env, bob_, XRP(1'000), USD(1'100));
            env(pay(alice_, alice_, USD(10'000)),
                Paths(XRP),
                DeliverMin(USD(100)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(100)));
            env.require(Balance(alice_, USD(100)));
        }

        {
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));
            env.trust(USD(1'200), bob_, carol_);
            env(pay(gw_, bob_, USD(1'200)));
            AMM const ammBob(env, bob_, XRP(5'500), USD(1'200));
            env(pay(alice_, carol_, USD(10'000)),
                Paths(XRP),
                DeliverMin(USD(200)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(1'000)),
                Ter(tecPATH_PARTIAL));
            env(pay(alice_, carol_, USD(10'000)),
                Paths(XRP),
                DeliverMin(USD(200)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(1'100)));
            BEAST_EXPECT(ammBob.expectBalances(XRP(6'600), USD(1'000), ammBob.tokens()));
            env.require(Balance(carol_, USD(200)));
        }

        {
            auto const dan = Account("dan");
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_, carol_, dan}, XRP(10'000));
            env.close();
            env.trust(USD(1'100), bob_, carol_, dan);
            env(pay(gw_, bob_, USD(100)));
            env(pay(gw_, dan, USD(1'100)));
            env(offer(bob_, XRP(100), USD(100)));
            env(offer(bob_, XRP(1'000), USD(100)));
            AMM const ammDan(env, dan, XRP(1'000), USD(1'100));
            if (!features[fixAMMv1_1])
            {
                env(pay(alice_, carol_, USD(10'000)),
                    Paths(XRP),
                    DeliverMin(USD(200)),
                    Txflags(tfPartialPayment),
                    Sendmax(XRP(200)));
                env.require(Balance(bob_, USD(0)));
                env.require(Balance(carol_, USD(200)));
                BEAST_EXPECT(ammDan.expectBalances(XRP(1'100), USD(1'000), ammDan.tokens()));
            }
            else
            {
                env(pay(alice_, carol_, USD(10'000)),
                    Paths(XRP),
                    DeliverMin(USD(200)),
                    Txflags(tfPartialPayment),
                    Sendmax(XRPAmount(200'000'001)));
                env.require(Balance(bob_, USD(0)));
                env.require(Balance(carol_, STAmount{USD, UINT64_C(200'00000090909), -11}));
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

        // The initial implementation of DepositAuth had a bug where an
        // account with the DepositAuth flag set could not make a payment
        // to itself.  That bug was fixed in the DepositPreauth amendment.
        Env env(*this, features);
        fund(env, gw_, {alice_, becky}, XRP(5'000));
        env.close();

        env.trust(USD(1'000), alice_);
        env.trust(USD(1'000), becky);
        env.close();

        env(pay(gw_, alice_, USD(500)));
        env.close();

        AMM const ammAlice(env, alice_, XRP(100), USD(140));

        // becky pays herself USD (10) by consuming part of alice_'s offer.
        // Make sure the payment works if PaymentAuth is not involved.
        env(pay(becky, becky, USD(10)), Path(~USD), Sendmax(XRP(10)));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(XRPAmount(107'692'308), USD(130), ammAlice.tokens()));

        // becky decides to require authorization for deposits.
        env(fset(becky, asfDepositAuth));
        env.close();

        // becky pays herself again.
        env(pay(becky, becky, USD(10)), Path(~USD), Sendmax(XRP(10)), Ter(tesSUCCESS));

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

        fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));
        env.trust(USD(1'000), alice_, bob_, carol_);
        env.close();

        env(pay(gw_, alice_, USD(150)));
        env(pay(gw_, carol_, USD(150)));
        AMM const ammCarol(env, carol_, USD(100), XRPAmount(101));

        // Make sure bob_'s trust line is all set up so he can receive USD.
        env(pay(alice_, bob_, USD(50)));
        env.close();

        // bob_ sets the lsfDepositAuth flag.
        env(fset(bob_, asfDepositAuth), Require(Flags(bob_, asfDepositAuth)));
        env.close();

        // None of the following payments should succeed.
        auto failedIouPayments = [this, &env]() {
            env.require(Flags(bob_, asfDepositAuth));

            // Capture bob_'s balances before hand to confirm they don't
            // change.
            PrettyAmount const bobXrpBalance{env.balance(bob_, XRP)};
            PrettyAmount const bobUsdBalance{env.balance(bob_, USD)};

            env(pay(alice_, bob_, USD(50)), Ter(tecNO_PERMISSION));
            env.close();

            // Note that even though alice_ is paying bob_ in XRP, the payment
            // is still not allowed since the payment passes through an
            // offer.
            env(pay(alice_, bob_, drops(1)), Sendmax(USD(1)), Ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(bobXrpBalance == env.balance(bob_, XRP));
            BEAST_EXPECT(bobUsdBalance == env.balance(bob_, USD));
        };

        //  Test when bob_ has an XRP balance > base reserve.
        failedIouPayments();

        // Set bob_'s XRP balance == base reserve.  Also demonstrate that
        // bob_ can make payments while his lsfDepositAuth flag is set.
        env(pay(bob_, alice_, USD(25)));
        env.close();

        {
            STAmount const bobPaysXRP{env.balance(bob_, XRP) - reserve(env, 1)};
            XRPAmount const bobPaysFee{reserve(env, 1) - reserve(env, 0)};
            env(pay(bob_, alice_, bobPaysXRP), Fee(bobPaysFee));
            env.close();
        }

        // Test when bob_'s XRP balance == base reserve.
        BEAST_EXPECT(env.balance(bob_, XRP) == reserve(env, 0));
        BEAST_EXPECT(env.balance(bob_, USD) == USD(25));
        failedIouPayments();

        // Test when bob_ has an XRP balance == 0.
        env(noop(bob_), Fee(reserve(env, 0)));
        env.close();

        BEAST_EXPECT(env.balance(bob_, XRP) == XRP(0));
        failedIouPayments();

        // Give bob_ enough XRP for the fee to clear the lsfDepositAuth flag.
        env(pay(alice_, bob_, drops(env.current()->fees().base)));

        // bob_ clears the lsfDepositAuth and the next payment succeeds.
        env(fclear(bob_, asfDepositAuth));
        env.close();

        env(pay(alice_, bob_, USD(50)));
        env.close();

        env(pay(alice_, bob_, drops(1)), Sendmax(USD(1)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(USD(101), XRPAmount(100), ammCarol.tokens()));
    }

    void
    testRippleState(FeatureBitset features)
    {
        testcase("RippleState Freeze");

        using namespace test::jtx;
        Env env(*this, features);

        Account const g1{"G1"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1'000), g1, alice, bob);
        env.close();

        env.trust(g1["USD"](100), bob);
        env.trust(g1["USD"](205), alice);
        env.close();

        env(pay(g1, bob, g1["USD"](10)));
        env(pay(g1, alice, g1["USD"](205)));
        env.close();

        AMM const ammAlice(env, alice, XRP(500), g1["USD"](105));

        {
            auto lines = getAccountLines(env, bob);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == g1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "100");
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "10");
        }

        {
            auto lines = getAccountLines(env, alice, g1["USD"]);
            if (!BEAST_EXPECT(checkArraySize(lines[jss::lines], 1u)))
                return;
            BEAST_EXPECT(lines[jss::lines][0u][jss::account] == g1.human());
            BEAST_EXPECT(lines[jss::lines][0u][jss::limit] == "205");
            // 105 transferred to AMM
            BEAST_EXPECT(lines[jss::lines][0u][jss::balance] == "100");
        }

        // Account with line unfrozen (proving operations normally work)
        //   test: can make Payment on that line
        env(pay(alice, bob, g1["USD"](1)));

        //   test: can receive Payment on that line
        env(pay(bob, alice, g1["USD"](1)));
        env.close();

        // Is created via a TrustSet with SetFreeze flag
        //   test: sets LowFreeze | HighFreeze flags
        env(trust(g1, bob["USD"](0), tfSetFreeze));
        env.close();

        {
            // Account with line frozen by issuer
            //    test: can buy more assets on that line
            env(offer(bob, g1["USD"](5), XRP(25)));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(XRP(525), g1["USD"](100), ammAlice.tokens()));
        }

        {
            //    test: can not sell assets from that line
            env(offer(bob, XRP(1), g1["USD"](5)), Ter(tecUNFUNDED_OFFER));

            //    test: can receive Payment on that line
            env(pay(alice, bob, g1["USD"](1)));

            //    test: can not make Payment from that line
            env(pay(bob, alice, g1["USD"](1)), Ter(tecPATH_DRY));
        }

        {
            // check G1 account lines
            //    test: shows freeze
            auto lines = getAccountLines(env, g1);
            json::Value bobLine;
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
            json::Value g1Line;
            for (auto const& it : lines[jss::lines])
            {
                if (it[jss::account] == g1.human())
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
            env(trust(g1, bob["USD"](0), tfClearFreeze));
            auto affected =
                env.meta()->getJson(JsonOptions::Values::None)[sfAffectedNodes.fieldName];
            if (!BEAST_EXPECT(checkArraySize(affected, 2u)))
                return;
            auto ff = affected[1u][sfModifiedNode.fieldName][sfFinalFields.fieldName];
            BEAST_EXPECT(
                ff[sfLowLimit.fieldName] ==
                g1["USD"](0).value().getJson(JsonOptions::Values::None));
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

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const a4{"A4"};

        env.fund(XRP(12'000), g1);
        env.fund(XRP(1'000), a1);
        env.fund(XRP(20'000), a2, a3, a4);
        env.close();

        env.trust(g1["USD"](1'200), a1);
        env.trust(g1["USD"](200), a2);
        env.trust(g1["BTC"](100), a3);
        env.trust(g1["BTC"](100), a4);
        env.close();

        env(pay(g1, a1, g1["USD"](1'000)));
        env(pay(g1, a2, g1["USD"](100)));
        env(pay(g1, a3, g1["BTC"](100)));
        env(pay(g1, a4, g1["BTC"](100)));
        env.close();

        AMM const ammG1(env, g1, XRP(10'000), g1["USD"](100));
        env(offer(a1, XRP(10'000), g1["USD"](100)), Txflags(tfPassive));
        env(offer(a2, g1["USD"](100), XRP(10'000)), Txflags(tfPassive));
        env.close();

        {
            // Account without GlobalFreeze (proving operations normally
            // work)
            //    test: visible offers where taker_pays is unfrozen issuer
            auto offers = env.rpc(
                "book_offers", std::string("USD/") + g1.human(), "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
            std::set<std::string> accounts;
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(a2.human()) != std::end(accounts));

            //    test: visible offers where taker_gets is unfrozen issuer
            offers = env.rpc(
                "book_offers", "XRP", std::string("USD/") + g1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
            accounts.clear();
            for (auto const& offer : offers)
            {
                accounts.insert(offer[jss::Account].asString());
            }
            BEAST_EXPECT(accounts.find(a1.human()) != std::end(accounts));
        }

        {
            // Offers/Payments
            //    test: assets can be bought on the market
            // env(offer(A3, G1["BTC"](1), XRP(1)));
            AMM ammA3(env, a3, g1["BTC"](1), XRP(1));

            //    test: assets can be sold on the market
            // AMM is bidirectional

            //    test: direct issues can be sent
            env(pay(g1, a2, g1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(a2, g1, g1["USD"](1)));

            //    test: via rippling can be sent
            env(pay(a2, a1, g1["USD"](1)));

            //    test: via rippling can be sent back
            env(pay(a1, a2, g1["USD"](1)));
            ammA3.withdrawAll(std::nullopt);
        }

        {
            // Account with GlobalFreeze
            //  set GlobalFreeze first
            //    test: SetFlag GlobalFreeze will toggle back to freeze
            env.require(Nflags(g1, asfGlobalFreeze));
            env(fset(g1, asfGlobalFreeze));
            env.require(Flags(g1, asfGlobalFreeze));
            env.require(Nflags(g1, asfNoFreeze));

            //    test: assets can't be bought on the market
            AMM const ammA3(env, a3, g1["BTC"](1), XRP(1), Ter(tecFROZEN));

            //    test: assets can't be sold on the market
            // AMM is bidirectional
        }

        {
            //    test: book_offers shows offers
            //    (should these actually be filtered?)
            auto offers = env.rpc(
                "book_offers", "XRP", std::string("USD/") + g1.human())[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;

            offers = env.rpc(
                "book_offers", std::string("USD/") + g1.human(), "XRP")[jss::result][jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
        }

        {
            // Payments
            //    test: direct issues can be sent
            env(pay(g1, a2, g1["USD"](1)));

            //    test: direct redemptions can be sent
            env(pay(a2, g1, g1["USD"](1)));

            //    test: via rippling cant be sent
            env(pay(a2, a1, g1["USD"](1)), Ter(tecPATH_DRY));
        }
    }

    void
    testOffersWhenFrozen(FeatureBitset features)
    {
        testcase("Offers for Frozen Trust Lines");

        using namespace test::jtx;
        Env env(*this, features);

        Account const g1{"G1"};
        Account const a2{"A2"};
        Account const a3{"A3"};
        Account const a4{"A4"};

        env.fund(XRP(2'000), g1, a3, a4);
        env.fund(XRP(2'000), a2);
        env.close();

        env.trust(g1["USD"](1'000), a2);
        env.trust(g1["USD"](2'000), a3);
        env.trust(g1["USD"](2'001), a4);
        env.close();

        env(pay(g1, a3, g1["USD"](2'000)));
        env(pay(g1, a4, g1["USD"](2'001)));
        env.close();

        AMM const ammA3(env, a3, XRP(1'000), g1["USD"](1'001));

        // removal after successful payment
        //    test: make a payment with partially consuming offer
        env(pay(a2, g1, g1["USD"](1)), Paths(g1["USD"]), Sendmax(XRP(1)));
        env.close();

        BEAST_EXPECT(ammA3.expectBalances(XRP(1'001), g1["USD"](1'000), ammA3.tokens()));

        //    test: someone else creates an offer providing liquidity
        env(offer(a4, XRP(999), g1["USD"](999)));
        env.close();
        // The offer consumes AMM offer
        BEAST_EXPECT(ammA3.expectBalances(XRP(1'000), g1["USD"](1'001), ammA3.tokens()));

        //    test: AMM line is frozen
        auto const a3am = STAmount{Issue{toCurrency("USD"), ammA3.ammAccount()}, 0};
        env(trust(g1, a3am, tfSetFreeze));
        auto const info = ammA3.ammRpcInfo();
        BEAST_EXPECT(info[jss::amm][jss::asset2_frozen].asBool());
        env.close();

        //    test: Can make a payment via the new offer
        env(pay(a2, g1, g1["USD"](1)), Paths(g1["USD"]), Sendmax(XRP(1)));
        env.close();
        // AMM is not consumed
        BEAST_EXPECT(ammA3.expectBalances(XRP(1'000), g1["USD"](1'001), ammA3.tokens()));

        // removal buy successful OfferCreate
        //    test: freeze the new offer
        env(trust(g1, a4["USD"](0), tfSetFreeze));
        env.close();

        //    test: can no longer create a crossing offer
        env(offer(a2, g1["USD"](999), XRP(999)));
        env.close();

        //    test: offer was removed by offer_create
        auto offers = getAccountOffers(env, a4)[jss::offers];
        if (!BEAST_EXPECT(checkArraySize(offers, 0u)))
            return;
    }

    void
    testTxMultisign(FeatureBitset features)
    {
        testcase("Multisign AMM Transactions");

        using namespace jtx;
        Env env{*this, features};
        Account const bogie{"bogie", KeyType::Secp256k1};
        Account const alice{"alice", KeyType::Secp256k1};
        Account const becky{"becky", KeyType::Ed25519};
        Account const zelda{"zelda", KeyType::Secp256k1};
        fund(env, gw_, {alice, becky, zelda}, XRP(20'000), {USD(20'000)});

        // alice_ uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::Secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), Sig(alice));

        // Attach signers to alice_.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), Sig(alie));
        env.close();
        env.require(Owners(alice, 2));

        Msig const ms{becky, bogie};

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
            Ter(tesSUCCESS));
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(10'000), ammAlice.tokens()));

        ammAlice.deposit(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(XRP(11'000), USD(11'000), IOUAmount{11'000'000, 0}));

        ammAlice.withdraw(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(10'000), ammAlice.tokens()));

        ammAlice.vote({}, 1'000);
        BEAST_EXPECT(ammAlice.expectTradingFee(1'000));

        env(ammAlice.bid({.account = alice, .bidMin = 100}), ms).close();
        BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, IOUAmount{4'000}));
        // 4000 tokens burnt
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), USD(10'000), IOUAmount{9'996'000, 0}));
    }

    void
    testToStrand(FeatureBitset features)
    {
        testcase("To Strand");

        using namespace jtx;

        // cannot have more than one offer with the same output issue

        Env env(*this, features);

        fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000), {USD(2'000), EUR(1'000)});

        AMM const bobXrpUsd(env, bob_, XRP(1'000), USD(1'000));
        AMM const bobUsdEur(env, bob_, USD(1'000), EUR(1'000));

        // payment path: XRP -> XRP/USD -> USD/EUR -> EUR/USD
        env(pay(alice_, carol_, USD(100)),
            Path(~USD, ~EUR, ~USD),
            Sendmax(XRP(200)),
            Txflags(tfNoRippleDirect),
            Ter(temBAD_PATH_LOOP));
    }

    void
    testRIPD1373(FeatureBitset features)
    {
        using namespace jtx;
        testcase("RIPD1373");

        {
            Env env(*this, features);
            auto const bobUsd = bob_["USD"];
            auto const bobEur = bob_["EUR"];
            fund(env, gw_, {alice_, bob_}, XRP(10'000));
            env.trust(USD(1'000), alice_, bob_);
            env.trust(EUR(1'000), alice_, bob_);
            env.close();
            fund(env, bob_, {alice_, gw_}, {bobUsd(100), bobEur(100)}, Fund::TokenOnly);

            AMM const ammBobXrpUsd(env, bob_, XRP(100), bobUsd(100));
            env(offer(gw_, XRP(100), USD(100)), Txflags(tfPassive));

            AMM const ammBobUsdEur(env, bob_, bobUsd(100), bobEur(100));
            env(offer(gw_, USD(100), EUR(100)), Txflags(tfPassive));

            TestPath const p = [&] {
                TestPath result;
                result.pushBack(allPathElements(gw_, bobUsd));
                result.pushBack(cpe(EUR.currency));
                return result;
            }();

            PathSet const paths(p);

            env(pay(alice_, alice_, EUR(1)),
                Json(paths.json()),
                Sendmax(XRP(10)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(temBAD_PATH));
        }

        {
            Env env(*this, features);

            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000), {USD(100)});

            AMM const ammBob(env, bob_, XRP(100), USD(100));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice_, carol_, XRP(100)),
                Path(~USD, ~XRP),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000), {USD(100)});

            AMM const ammBob(env, bob_, XRP(100), USD(100));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice_, carol_, XRP(100)),
                Path(~USD, ~XRP),
                Sendmax(XRP(200)),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_SEND_XRP_MAX));
        }
    }

    void
    testLoop(FeatureBitset features)
    {
        testcase("test loop");
        using namespace jtx;

        auto const cny = gw_["CNY"];

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice_, bob_, carol_, gw_);
            env.close();
            env.trust(USD(10'000), alice_, bob_, carol_);
            env.close();
            env(pay(gw_, bob_, USD(100)));
            env(pay(gw_, alice_, USD(100)));
            env.close();

            AMM const ammBob(env, bob_, XRP(100), USD(100));

            // payment path: USD -> USD/XRP -> XRP/USD
            env(pay(alice_, carol_, USD(100)),
                Sendmax(USD(100)),
                Path(~XRP, ~USD),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice_, bob_, carol_, gw_);
            env.close();
            env.trust(USD(10'000), alice_, bob_, carol_);
            env.trust(EUR(10'000), alice_, bob_, carol_);
            env.trust(cny(10'000), alice_, bob_, carol_);

            env(pay(gw_, bob_, USD(200)));
            env(pay(gw_, bob_, EUR(200)));
            env(pay(gw_, bob_, cny(100)));

            AMM const ammBobXrpUsd(env, bob_, XRP(100), USD(100));
            AMM const ammBobUsdEur(env, bob_, USD(100), EUR(100));
            AMM const ammBobEurCny(env, bob_, EUR(100), cny(100));

            // payment path: XRP->XRP/USD->USD/EUR->USD/CNY
            env(pay(alice_, carol_, cny(100)),
                Sendmax(XRP(100)),
                Path(~USD, ~EUR, ~USD, ~cny),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
        }
    }

    void
    testPaths()
    {
        pathFindConsumeAll();
        viaOffersViaGateway();
        receiveMax();
        pathFind01();
        pathFind02();
        pathFind05();
        pathFind06();
    }

    void
    testFlow()
    {
        using namespace jtx;
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas in the transaction engine
        FeatureBitset const all{
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol};

        testFalseDry(all);
        testBookStep(all);
        testTransferRateNoOwnerFee(all);
        testTransferRateNoOwnerFee(all - fixAMMv1_1 - fixAMMv1_3);
        testLimitQuality();
        testXRPPathLoop();
    }

    void
    testCrossingLimits()
    {
        using namespace jtx;
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas in the transaction engine
        FeatureBitset const all{
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol};
        testStepLimit(all);
        testStepLimit(all - fixAMMv1_1 - fixAMMv1_3);
    }

    void
    testDeliverMin()
    {
        using namespace jtx;
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas in the transaction engine
        FeatureBitset const all{
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol};
        testConvertAllOfAnAsset(all);
        testConvertAllOfAnAsset(all - fixAMMv1_1 - fixAMMv1_3);
    }

    void
    testDepositAuth()
    {
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas in the transaction engine
        FeatureBitset const all{
            jtx::testableAmendments() - featureSingleAssetVault - featureLendingProtocol};
        testPayment(all);
        testPayIOU();
    }

    void
    testFreeze()
    {
        using namespace test::jtx;
        // For now, just disable SAV entirely, which locks in the small Number
        // mantissas in the transaction engine
        FeatureBitset const sa{
            testableAmendments() - featureSingleAssetVault - featureLendingProtocol};
        testRippleState(sa);
        testGlobalFreeze(sa);
        testOffersWhenFrozen(sa);
    }

    void
    testMultisign()
    {
        testTxMultisign(jtx::testableAmendments());
    }

    void
    testPayStrand()
    {
        auto const all = jtx::testableAmendments();

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

BEAST_DEFINE_TESTSUITE_PRIO(AMMExtended, app, xrpl, 1);

}  // namespace xrpl::test
