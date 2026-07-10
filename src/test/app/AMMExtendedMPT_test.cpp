#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/delivermin.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/require.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

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
#include <xrpl/protocol/Quality.h>
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
#include <tuple>

namespace xrpl::test {

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

        fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));

        MPTTester const eth(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_, carol_},
             .pay = 200'000'000'000'000'000,
             .flags = kMptDexFlags});

        MPTTester const btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_, carol_},
             .pay = 2'000'000'000'000'000,
             .flags = kMptDexFlags});

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env(offer(carol_, btc(49'000'000'000'000), XRP(49)));
        env(offer(carol_, btc(51'000'000'000'000), XRP(51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env(offer(carol_, XRP(50), eth(50'000'000'000'000)));
        env(offer(carol_, XRP(50), eth(50'000'000'000'000)));

        // Good quality path
        AMM const ammCarol(env, carol_, btc(1'000'000'000'000'000), eth(100'100'000'000'000'000));

        PathSet const paths(TestPath(XRP, MPT(eth)), TestPath(MPT(eth)));

        env(pay(alice_, bob_, eth(100'000'000'000'000)),
            Json(paths.json()),
            Sendmax(btc(1'000'000'000'000'000)),
            Txflags(tfPartialPayment));

        BEAST_EXPECT(ammCarol.expectBalances(
            btc(1'001'000'000'374'816), eth(100'000'000'000'000'000), ammCarol.tokens()));

        env.require(Balance(bob_, eth(200'100'000'000'000'000)));
        BEAST_EXPECT(isOffer(env, carol_, btc(49'000'000'000'000), XRP(49)));
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
                auto const& btc = MPT(ammAlice[1]);
                auto const baseFee = env.current()->fees().base;
                auto carolBTC = env.balance(carol_, btc);
                auto carolXRP = env.balance(carol_, XRP);
                // Order that can't be filled
                env(offer(carol_, btc(100), XRP(100)), Txflags(tfFillOrKill), Ter(tecKILLED));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), btc(10'000), ammAlice.tokens()));
                // fee = AMM
                env.require(Balance(carol_, carolXRP - baseFee));
                env.require(Balance(carol_, carolBTC));

                BEAST_EXPECT(expectOffers(env, carol_, 0));
                carolXRP = env.balance(carol_, XRP);

                // Order that can be filled
                env(offer(carol_, XRP(100), btc(100)), Txflags(tfFillOrKill), Ter(tesSUCCESS));
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(10'100), ammAlice.tokens()));
                env.require(Balance(carol_, carolXRP + XRP(100) - baseFee));
                env.require(Balance(carol_, carolBTC - btc(100)));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
            },
            {{XRP(10'100), gAmmmpt(10'000)}},
            0,
            std::nullopt,
            {features});

        // Immediate or Cancel - cross as much as possible
        // and add nothing on the books.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const& btc = MPT(ammAlice[1]);
                auto const baseFee = env.current()->fees().base;
                auto carolBTC = env.balance(carol_, btc);
                auto carolXRP = env.balance(carol_, XRP);
                env(offer(carol_, XRP(200), btc(200)),
                    Txflags(tfImmediateOrCancel),
                    Ter(tesSUCCESS));

                // AMM generates a synthetic offer of 100BTC/100XRP
                // to match the CLOB offer quality.
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(10'100), ammAlice.tokens()));
                // +AMM - offer * fee
                env.require(Balance(carol_, carolXRP + XRP(100) - baseFee));
                env.require(Balance(carol_, carolBTC - btc(100)));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
            },
            {{XRP(10'100), gAmmmpt(10'000)}},
            0,
            std::nullopt,
            {features});

        // tfPassive -- place the offer without crossing it.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Carol creates a passive offer that could cross AMM.
                // Carol's offer should stay in the ledger.
                auto const& btc = MPT(ammAlice[1]);
                env(offer(carol_, XRP(100), btc(100), tfPassive));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), btc(10'000), ammAlice.tokens()));
                BEAST_EXPECT(expectOffers(env, carol_, 1, {{{XRP(100), btc(100)}}}));
            },
            {{XRP(10'100), gAmmmpt(10'000)}},
            0,
            std::nullopt,
            {features});

        // tfPassive -- cross only offers of better quality.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const& btc = MPT(ammAlice[1]);
                env(offer(alice_, btc(110), XRP(100)));
                env.close();

                // Carol creates a passive offer. That offer should cross
                // AMM and leave Alice's offer untouched.
                env(offer(carol_, XRP(100), btc(100), tfPassive));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(XRP(10'900), btc(9083), ammAlice.tokens()));
                BEAST_EXPECT(expectOffers(env, carol_, 0));
                BEAST_EXPECT(expectOffers(env, alice_, 1));
            },
            {{XRP(11'000), gAmmmpt(9'000)}},
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

        fund(env, gw_, {bob_, alice_}, XRP(300'000));

        MPTTester const btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_},
             .pay = 100'000'000,
             .flags = kMptDexFlags});

        AMM const ammAlice(env, alice_, XRP(150'000), btc(50'000'000));

        // Existing offer pays better than this wants.
        // Partially consume existing offer.
        // Pay 1'000'000 BTC, get 3061224490 Drops.
        auto const xrpTransferred = XRPAmount{3'061'224'490};
        env(offer(bob_, btc(1'000'000), XRP(4'000)));

        BEAST_EXPECT(ammAlice.expectBalances(
            XRP(150'000) + xrpTransferred, btc(49'000'000), IOUAmount{273'861'278752583, -5}));

        env.require(Balance(bob_, btc(101'000'000)));
        BEAST_EXPECT(
            expectLedgerEntryRoot(env, bob_, XRP(300'000) - xrpTransferred - 2 * txFee(env, 1)));
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

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_}, .flags = kMptDexFlags});
        env(pay(gw_, alice_, btc(500'000'000)));

        AMM const ammAlice(env, alice_, XRP(150'000), btc(51'000'000));
        env(offer(bob_, btc(1'000'000), XRP(3'000)));

        BEAST_EXPECT(ammAlice.expectBalances(XRP(153'000), btc(50'000'000), ammAlice.tokens()));

        env.require(Balance(bob_, btc(1'000'000)));
        env.require(Balance(bob_, XRP(200'000) - XRP(3'000) - env.current()->fees().base * 2));
    }

    void
    testCurrencyConversionEntire(FeatureBitset features)
    {
        testcase("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw_, {alice_, bob_}, XRP(10'000));
        env.require(Owners(bob_, 0));

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_}, .flags = kMptDexFlags});
        env(pay(gw_, bob_, btc(1'000'000'000)));

        env.require(Owners(alice_, 1), Owners(bob_, 1));

        env(pay(gw_, alice_, btc(100'000'000)));
        AMM const ammBob(env, bob_, btc(200'000'000), XRP(1'500));

        env(pay(alice_, alice_, XRP(500)), Sendmax(btc(100'000'000)));

        BEAST_EXPECT(ammBob.expectBalances(btc(300'000'000), XRP(1'000), ammBob.tokens()));
        env.require(Balance(alice_, btc(0)));

        auto jrr = ledgerEntryRoot(env, alice_);
        env.require(Balance(alice_, XRP(10'000) + XRP(500) - env.current()->fees().base * 2));
    }

    void
    testCurrencyConversionInParts(FeatureBitset features)
    {
        testcase("Currency Conversion: In Parts");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw_, bob_);
        env.fund(XRP(40'000), alice_);

        MPTTester const btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_},
             .pay = 30'000'000'000,
             .flags = kMptDexFlags});
        env(pay(gw_, alice_, btc(10'000'000'000)));

        AMM const ammAlice(env, alice_, XRP(10'000), btc(10'000'000'000));
        env.close();

        // Alice converts BTC to XRP which should fail
        // due to PartialPayment.
        env(pay(alice_, alice_, XRP(100)), Sendmax(btc(100'000'000)), Ter(tecPATH_PARTIAL));

        // Alice converts BTC to XRP, should succeed because
        // we permit partial payment
        env(pay(alice_, alice_, XRP(100)), Sendmax(btc(100'000'000)), Txflags(tfPartialPayment));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(
            XRPAmount{9'900'990'100}, btc(10'100'000'000), ammAlice.tokens()));
        // initial 40,000'000'000 - 10,000'000'000AMM - 100'000'000pay
        env.require(Balance(alice_, btc(29'900'000'000)));
        // initial 40,000 - 10,0000AMM + 99.009900pay - fee*3
        BEAST_EXPECT(expectLedgerEntryRoot(
            env,
            alice_,
            XRP(40'000) - XRP(10'000) + XRPAmount{99'009'900} - ammCrtFee(env) - txFee(env, 3)));
    }

    void
    testCrossCurrencyStartXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Start with XRP");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw_);
        env.fund(XRP(40'000), alice_);
        env.fund(XRP(1'000), bob_);

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_}, .flags = kMptDexFlags});
        env(pay(gw_, alice_, btc(10'100'000'000)));

        AMM const ammAlice(env, alice_, XRP(10'000), btc(10'100'000'000));
        env.close();

        env(pay(alice_, bob_, btc(100'000'000)), Sendmax(XRP(100)));
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), btc(10'000'000'000), ammAlice.tokens()));
        env.require(Balance(bob_, btc(100'000'000)));
    }

    void
    testCrossCurrencyEndXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: End with XRP");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw_);
        env.fund(XRP(40'100), alice_);
        env.fund(XRP(1'000), bob_);

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_}, .flags = kMptDexFlags});
        env(pay(gw_, alice_, btc(40'000'000'000)));

        AMM const ammAlice(env, alice_, XRP(10'100), btc(10'000'000'000));
        env.close();

        env(pay(alice_, bob_, XRP(100)), Sendmax(btc(100'000'000)));
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(10'100'000'000), ammAlice.tokens()));
        BEAST_EXPECT(expectLedgerEntryRoot(env, bob_, XRP(1'000) + XRP(100) - txFee(env, 1)));
    }

    void
    testCrossCurrencyBridged(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Bridged");

        using namespace jtx;

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env(*this);
            auto const dan = Account{"dan"};
            env.fund(XRP(60'000), alice_, bob_, carol_, gw_, dan);
            env.close();
            auto const eth = issue1(
                {.env = env,
                 .token = "ETH",
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, dan},
                 .limit = 10'000'000'000'000'000});
            auto const btc = issue2(
                {.env = env,
                 .token = "BTC",
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, dan},
                 .limit = 10'000'000'000'000'000});
            env(pay(gw_, alice_, btc(500'000'000'000'000)));
            env(pay(gw_, carol_, btc(6'000'000'000'000'000)));
            env(pay(gw_, dan, eth(400'000'000'000'000)));
            env.close();
            env.close();
            AMM const ammCarol(env, carol_, btc(5'000'000'000'000'000), XRP(50'000));

            env(offer(dan, XRP(500), eth(50'000'000'000'000)));
            env.close();

            json::Value jtp{json::ValueType::Array};
            jtp[0u][0u][jss::currency] = "XRP";
            env(pay(alice_, bob_, eth(30'000'000'000'000)),
                Json(jss::Paths, jtp),
                Sendmax(btc(333'000'000'000'000)));
            env.close();
            BEAST_EXPECT(ammCarol.expectBalances(
                XRP(49'700), btc(5'030'181'086'519'115), ammCarol.tokens()));
            BEAST_EXPECT(expectOffers(env, dan, 1, {{Amounts{XRP(200), eth(20'000'000'000'000)}}}));
            env.require(Balance(bob_, eth(30'000'000'000'000)));
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
        auto const startingXrp =
            XRP(100) + env.current()->fees().accountReserve(2) + env.current()->fees().base * 3;

        env.fund(startingXrp, gw_, alice_);
        env.fund(XRP(2'000), bob_);
        env.close();

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_}, .flags = kMptDexFlags});

        // Created only to increase one reserve count for alice
        MPTTester const eth(
            {.env = env, .issuer = gw_, .holders = {alice_}, .flags = kMptDexFlags});

        env(pay(gw_, bob_, btc(1'200'000'000'000'000)));

        AMM const ammBob(env, bob_, XRP(1'000), btc(1'200'000'000'000'000));
        // Alice has 400 - (2 reserve of 50 = 300 reserve) = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(alice_, btc(200'000'000'000'000), XRP(200)));

        // The pool gets only 100XRP for ~109.09e12BTC, even though
        // it can exchange more.
        BEAST_EXPECT(
            ammBob.expectBalances(XRP(1'100), btc(1'090'909'090'909'091), ammBob.tokens()));

        env.require(Balance(alice_, btc(109'090'909'090'909)));
        env.require(Balance(alice_, XRP(300)));
    }

    void
    testOfferCreateThenCross(FeatureBitset features)
    {
        testcase("Offer Create, then Cross");

        using namespace jtx;

        Env env{*this, features};

        fund(env, gw_, {alice_, bob_}, XRP(200'000));

        MPTTester const btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_},
             .transferFee = 500,
             .flags = kMptDexFlags});

        env(pay(gw_, bob_, btc(1'000'000'000'000)));
        env(pay(gw_, alice_, btc(200'000'000'000'000)));

        AMM const ammAlice(env, alice_, btc(150'000'000'000'000), XRP(150'100));
        env(offer(bob_, XRP(100), btc(100'000'000'000)));

        BEAST_EXPECT(
            ammAlice.expectBalances(btc(150'100'000'000'000), XRP(150'000), ammAlice.tokens()));

        // Bob pays 0.005 transfer fee.
        env.require(Balance(bob_, btc(899'500'000'000)));
    }

    void
    testSellFlagBasic(FeatureBitset features)
    {
        testcase("Offer tfSell: Basic Sell");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(30'000), gw_, bob_, carol_);
        env.fund(XRP(39'900), alice_);

        MPTTester const btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_, carol_},
             .pay = 30'000,
             .flags = kMptDexFlags});
        env(pay(gw_, alice_, btc(10'100)));

        AMM const ammAlice(env, alice_, XRP(9'900), btc(10'100));

        env(offer(carol_, btc(100), XRP(100)), Json(jss::Flags, tfSell));
        env.close();
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(9'999), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, carol_, 0));
        env.require(Balance(carol_, btc(30'101)));
        BEAST_EXPECT(
            expectLedgerEntryRoot(env, carol_, XRP(30'000) - XRP(100) - 2 * txFee(env, 1)));
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

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_}, .flags = kMptDexFlags});
        env(pay(gw_, bob_, btc(2'200'000'000)));

        AMM const ammBob(env, bob_, XRP(1'000), btc(2'200'000'000));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100'000'000 BTC for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more BTC than asked for.
        env(offer(alice_, btc(100'000'000), XRP(200)), Json(jss::Flags, tfSell));
        BEAST_EXPECT(ammBob.expectBalances(XRP(1'100), btc(2'000'000'000), ammBob.tokens()));
        env.require(Balance(alice_, btc(200'000'000)));
        BEAST_EXPECT(expectLedgerEntryRoot(env, alice_, XRP(250)));
        BEAST_EXPECT(expectOffers(env, alice_, 0));
    }

    void
    testGatewayCrossCurrency(FeatureBitset features)
    {
        testcase("Client Issue: Gateway Cross Currency");

        using namespace jtx;

        Env env{*this, features};

        auto const startingXrp = XRP(100.1) + reserve(env, 1) + env.current()->fees().base * 2;
        env.fund(startingXrp, gw_, alice_, bob_);

        MPTTester const xts(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_},
             .pay = 1'000'000'000'000'000,
             .flags = kMptDexFlags});
        MPTTester const xxx(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_},
             .pay = 1'000'000'000'000'000,
             .flags = kMptDexFlags});

        AMM const ammAlice(env, alice_, xts(1'000'000'000'000'000), xxx(1'000'000'000'000'000));

        json::Value payment;
        payment[jss::secret] = toBase58(generateSeed("bob"));
        payment[jss::id] = env.seq(bob_);
        payment[jss::build_path] = true;
        payment[jss::tx_json] = pay(bob_, bob_, xxx(10'000'000'000'000));
        payment[jss::tx_json][jss::Sequence] =
            env.current()->read(keylet::account(bob_.id()))->getFieldU32(sfSequence);
        payment[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
        payment[jss::tx_json][jss::SendMax] =
            xts(15'000'000'000'000).value().getJson(JsonOptions::Values::None);
        payment[jss::tx_json][jss::Flags] = tfPartialPayment;
        auto const jrr = env.rpc("json", "submit", to_string(payment));
        BEAST_EXPECT(jrr[jss::result][jss::status] == "success");
        BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");

        BEAST_EXPECT(ammAlice.expectBalances(
            xts(1'010'101'010'101'011), xxx(990'000'000'000'000), ammAlice.tokens()));
        env.require(Balance(bob_, xts(989'898'989'898'989)));
        env.require(Balance(bob_, xxx(1'010'000'000'000'000)));
    }

    void
    testBridgedCross(FeatureBitset features)
    {
        testcase("Bridged Crossing");

        using namespace jtx;

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 15'000'000'000,
                 .flags = kMptDexFlags});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 15'000'000'000,
                 .flags = kMptDexFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP AMM is created.
            //   o carol has ETH but wants BTC.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), btc(10'100'000'000));
            AMM const ammBob(env, bob_, eth(10'000'000'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol_, btc(100'000'000), eth(100'000'000)));
            env.close();

            BEAST_EXPECT(
                ammAlice.expectBalances(XRP(10'100), btc(10'000'000'000), ammAlice.tokens()));
            BEAST_EXPECT(ammBob.expectBalances(XRP(10'000), eth(10'100'000'000), ammBob.tokens()));
            env.require(Balance(carol_, btc(15'100'000'000)));
            env.require(Balance(carol_, eth(14'900'000'000)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
        }

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 15'000'000'000,
                 .flags = kMptDexFlags});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 15'000'000'000,
                 .flags = kMptDexFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP offer is created.
            //   o carol has ETH but wants BTC.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and bob's offer are created, then autobridging
            // will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), btc(10'100'000'000));
            env(offer(bob_, eth(100'000'000), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol_, btc(100'000'000), eth(100'000'000)));
            env.close();

            BEAST_EXPECT(
                ammAlice.expectBalances(XRP(10'100), btc(10'000'000'000), ammAlice.tokens()));
            env.require(Balance(carol_, btc(15'100'000'000)));
            env.require(Balance(carol_, eth(14'900'000'000)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 15'000'000'000,
                 .flags = kMptDexFlags});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 15'000'000'000,
                 .flags = kMptDexFlags});

            // The scenario:
            //   o BTC/XRP offer is created.
            //   o ETH/XRP AMM is created.
            //   o carol has ETH but wants BTC.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before AMM and alice's offer are created, then
            // autobridging will not occur.
            env(offer(alice_, XRP(100), btc(100'000'000)));
            env.close();
            AMM const ammBob(env, bob_, eth(10'000'000'000), XRP(10'100));

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Carol's offer.
            env(offer(carol_, btc(100'000'000), eth(100'000'000)));
            env.close();

            BEAST_EXPECT(ammBob.expectBalances(XRP(10'000), eth(10'100'000'000), ammBob.tokens()));
            env.require(Balance(carol_, btc(15'100'000'000)));
            env.require(Balance(carol_, eth(14'900'000'000)));
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

        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_},
                 .pay = 20'000'000'000,
                 .flags = kMptDexFlags});
            AMM const ammBob(env, bob_, XRP(20'000), btc(200'000'000));
            // alice submits a tfSell | tfFillOrKill offer that does not cross.
            env(offer(alice_, btc(2'100'000), XRP(210), tfSell | tfFillOrKill), Ter(tecKILLED));

            BEAST_EXPECT(ammBob.expectBalances(XRP(20'000), btc(200'000'000), ammBob.tokens()));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }
        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_},
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});
            AMM const ammBob(env, bob_, XRP(20'000), btc(200'000'000'000'000));
            // alice submits a tfSell | tfFillOrKill offer that crosses.
            // Even though tfSell is present it doesn't matter this time.
            env(offer(alice_, btc(2'000'000'000'000), XRP(220), tfSell | tfFillOrKill));
            env.close();
            BEAST_EXPECT(
                ammBob.expectBalances(XRP(20'220), btc(197'823'936'696'341), ammBob.tokens()));
            env.require(Balance(alice_, btc(1'002'176'063'303'659)));
            BEAST_EXPECT(expectOffers(env, alice_, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses and
            // returns more than was asked for (because of the tfSell flag).
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_},
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});
            AMM const ammBob(env, bob_, XRP(20'000), btc(200'000'000'000'000));

            env(offer(alice_, btc(10'000'000'000'000), XRP(1'500), tfSell | tfFillOrKill));
            env.close();

            BEAST_EXPECT(
                ammBob.expectBalances(XRP(21'500), btc(186'046'511'627'907), ammBob.tokens()));
            env.require(Balance(alice_, btc(1'013'953'488'372'093)));
            BEAST_EXPECT(expectOffers(env, alice_, 0));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that doesn't cross.
            // This would have succeeded with a regular tfSell, but the
            // fillOrKill prevents the transaction from crossing since not
            // all of the offer is consumed because AMM generated offer,
            // which matches alice's offer quality is ~ 10XRP/0.01996e3BTC.
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_},
                 .pay = 10'000'000'000,
                 .flags = kMptDexFlags});
            AMM const ammBob(env, bob_, XRP(5000), btc(10'000'000));

            env(offer(alice_, btc(1'000'000), XRP(501), tfSell | tfFillOrKill), Ter(tecKILLED));
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

        // AMM XRP/BTC. Alice places BTC/XRP offer.
        {
            Env env(*this, features);
            env.fund(XRP(30'000), gw_, bob_, carol_);
            env.fund(XRP(40'000), alice_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 30'000'000,
                 .flags = kMptDexFlags});
            env(pay(gw_, alice_, btc(10'100'000)));

            AMM const ammAlice(env, alice_, XRP(10'000), btc(10'100'000));
            env.close();

            env(offer(carol_, btc(100'000), XRP(100)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), btc(10'000'000), ammAlice.tokens()));
            env.require(Balance(carol_, btc(30'100'000)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
        }

        {
            Env env(*this, features);
            env.fund(XRP(30'000), gw_, bob_, carol_);
            env.fund(XRP(40'100), alice_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 30'000'000,
                 .flags = kMptDexFlags});
            env(pay(gw_, alice_, btc(10'000'000)));

            AMM const ammAlice(env, alice_, XRP(10'100), btc(10'000'000));
            env.close();

            env(offer(carol_, XRP(100), btc(100'000)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(10'100'000), ammAlice.tokens()));
            // Carol pays 25% transfer fee
            env.require(Balance(carol_, btc(29'875'000)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
        }

        {
            // Bridged crossing.
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = kMptDexFlags});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = kMptDexFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP Offer is created.
            //   o carol has ETH but wants BTC.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), btc(10'100'000));
            env(offer(bob_, eth(100'000), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // fully consumes Bob's offer.
            env(offer(carol_, btc(100'000), eth(100'000)));
            env.close();

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'100), btc(10'000'000), ammAlice.tokens()));
            env.require(Balance(carol_, btc(15'100'000)));
            // Carol pays 25% transfer fee.
            env.require(Balance(carol_, eth(14'875'000)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }

        {
            // Bridged crossing. The transfer fee is paid on the step not
            // involving AMM as src/dst.
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = kMptDexFlags});
            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 15'000'000,
                 .flags = kMptDexFlags});

            // The scenario:
            //   o BTC/XRP AMM is created.
            //   o ETH/XRP Offer is created.
            //   o carol has ETH but wants BTC.
            // Note that Carol's offer must come last.  If Carol's offer is
            // placed before AMM is created, then autobridging will not occur.
            AMM const ammAlice(env, alice_, XRP(10'000), btc(10'050'000));
            env(offer(bob_, eth(100'000), XRP(100)));
            env.close();

            // Carol makes an offer that consumes AMM liquidity and
            // partially consumes Bob's offer.
            env(offer(carol_, btc(50'000), eth(50'000)));
            env.close();
            // This test verifies that the amount removed from an offer
            // accounts for the transfer fee that is removed from the
            // account but not from the remaining offer.

            // AMM doesn't pay the transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(XRP(10'050), btc(10'000'000), ammAlice.tokens()));
            env.require(Balance(carol_, btc(15'050'000)));
            // Carol pays 25% transfer fee.
            env.require(Balance(carol_, eth(14'937'500)));
            BEAST_EXPECT(expectOffers(env, carol_, 0));
            BEAST_EXPECT(expectOffers(env, bob_, 1, {{Amounts{eth(50'000), XRP(50)}}}));
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

        env.fund(XRP(30'000) + f, alice_, bob_);
        env.close();

        MPTTester const btc(
            {.env = env, .issuer = bob_, .holders = {alice_}, .flags = kMptDexFlags});

        AMM const ammBob(env, bob_, XRP(10'000), btc(10'100));

        env(offer(alice_, btc(100), XRP(100)));
        env.close();

        BEAST_EXPECT(ammBob.expectBalances(XRP(10'100), btc(10'000), ammBob.tokens()));
        BEAST_EXPECT(expectOffers(env, alice_, 0));
        env.require(Balance(alice_, btc(100)));
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

        MPTTester const aBux(
            {.env = env, .issuer = ann, .holders = {bob, cam, carol}, .flags = kMptDexFlags});

        MPTTester const bBux(
            {.env = env, .issuer = bob, .holders = {ann, cam, carol}, .flags = kMptDexFlags});

        env(pay(ann, cam, aBux(350'000'000'000'000)));
        env(pay(bob, cam, bBux(350'000'000'000'000)));
        env(pay(bob, carol, bBux(4'000'000'000'000'000)));
        env(pay(ann, carol, aBux(4'000'000'000'000'000)));

        AMM const ammCarol(env, carol, aBux(3'000'000'000'000'000), bBux(3'300'000'000'000'000));

        // cam puts an offer on the books that her upcoming offer could cross.
        // But this offer should be deleted, not crossed, by her upcoming
        // offer.
        env(offer(cam, aBux(290'000'000'000'000), bBux(300'000'000'000'000), tfPassive));
        env.close();
        env.require(Balance(cam, aBux(350'000'000'000'000)));
        env.require(Balance(cam, bBux(350'000'000'000'000)));
        env.require(offers(cam, 1));

        // This offer caused the assert.
        env(offer(cam, bBux(300'000'000'000'000), aBux(300'000'000'000'000)));

        // AMM is consumed up to the first cam Offer quality
        BEAST_EXPECT(ammCarol.expectBalances(
            aBux(3'093'541'659'651'604), bBux(3'200'215'509'984'418), ammCarol.tokens()));
        BEAST_EXPECT(expectOffers(
            env, cam, 1, {{Amounts{bBux(200'215'509'984'418), aBux(200'215'509'984'419)}}}));
    }

    void
    testRequireAuth(FeatureBitset features)
    {
        testcase("RequireAuth");

        using namespace jtx;

        Env env{*this, features};
        env.fund(XRP(400'000), gw_, alice_, bob_);

        MPTTester btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_},
             .flags = tfMPTRequireAuth | kMptDexFlags});

        // Authorize bob and alice
        btc.authorize({.holder = alice_});
        btc.authorize({.holder = bob_});

        env(pay(gw_, alice_, btc(1'000)));
        env.close();

        // Alice is able to create AMM since the GW has authorized her
        AMM const ammAlice(env, alice_, btc(1'000), XRP(1'050));

        env(pay(gw_, bob_, btc(50)));
        env.close();

        env.require(Balance(bob_, btc(50)));

        // Bob's offer should cross Alice's AMM
        env(offer(bob_, XRP(50), btc(50)));
        env.close();

        BEAST_EXPECT(ammAlice.expectBalances(btc(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob_, 0));
        env.require(Balance(bob_, btc(0)));
    }

    void
    testMissingAuth(FeatureBitset features)
    {
        testcase("Missing Auth");

        using namespace jtx;

        Env env{*this, features};

        env.fund(XRP(400'000), gw_, alice_, bob_);
        env.close();

        MPTTester btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_},
             .flags = tfMPTRequireAuth | kMptDexFlags});

        // Alice doesn't have the funds
        {
            AMM const ammAlice(env, alice_, btc(1'000), XRP(1'000), Ter(tecNO_AUTH));
        }

        btc.authorize({.holder = bob_});
        env(pay(gw_, bob_, btc(50)));
        env.close();
        env.require(Balance(bob_, btc(50)));

        // Alice should not be able to create AMM without authorization.
        {
            AMM const ammAlice(env, alice_, btc(1'000), XRP(1'000), Ter(tecNO_AUTH));
        }

        // Finally, authorize alice. Now alice's AMM create should succeed.
        btc.authorize({.holder = alice_});
        env(pay(gw_, alice_, btc(1'000)));
        env.close();

        AMM const ammAlice(env, alice_, btc(1'000), XRP(1'050));

        // Authorize AMM.
        // BTC.authorize({.account = ammAlice.ammAccount()});
        // env.close();

        // Now bob creates his offer again, which crosses with alice's AMM.
        env(offer(bob_, XRP(50), btc(50)));
        env.close();

        BEAST_EXPECT(ammAlice.expectBalances(btc(1'050), XRP(1'000), ammAlice.tokens()));
        BEAST_EXPECT(expectOffers(env, bob_, 0));
        env.require(Balance(bob_, btc(0)));
    }

    void
    testOffers()
    {
        using namespace jtx;
        FeatureBitset const all{testableAmendments()};
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
    pathFindConsumeAll()
    {
        testcase("path find consume all");
        using namespace jtx;

        Env env = pathTestEnv();
        env.fund(XRP(100'000'260), alice_);
        env.fund(XRP(30'000), gw_, bob_, carol_);

        MPTTester const eth(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_, carol_},
             .pay = 100'000'000'000'000,
             .flags = kMptDexFlags});

        AMM const ammCarol(env, carol_, XRP(100), eth(100'000'000'000'000));

        STPathSet st;
        STAmount sa;
        STAmount da;
        std::tie(st, sa, da) = findPaths(
            env, alice_, bob_, bob_["AUD"](-1), std::optional<STAmount>(XRP(100'000'000)));
        BEAST_EXPECT(st.empty());
        std::tie(st, sa, da) =
            findPaths(env, alice_, bob_, eth(-1), std::optional<STAmount>(XRP(100'000'000)));
        // Alice sends all requested 100,000,000XRP
        BEAST_EXPECT(sa == XRP(100'000'000));
        // Bob gets ~99.99e12ETH. This is the amount Bob
        // can get out of AMM for 100,000,000XRP.
        BEAST_EXPECT(equal(da, eth(99'999'900'000'100)));
    }

    // carol holds ETH, sells ETH for XRP
    // bob will hold ETH
    // alice pays bob ETH using XRP
    void
    viaOffersViaGateway()
    {
        testcase("via gateway");
        using namespace jtx;

        Env env = pathTestEnv();
        env.fund(XRP(10'000), alice_, bob_, carol_, gw_);
        env.close();

        MPTTester const eth(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_, carol_},
             .transferFee = 10'000,
             .flags = kMptDexFlags});

        MPTTester const btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice_, bob_, carol_},
             .transferFee = 10'000,
             .flags = kMptDexFlags});

        env(pay(gw_, carol_, eth(51)));
        env.close();
        AMM const ammCarol(env, carol_, XRP(40), eth(51));
        env(pay(alice_, bob_, eth(10)), Sendmax(XRP(100)), Paths(XRP));
        env.close();
        // AMM offer is 51.282052XRP/11ETH, 11ETH/1.1 = 10ETH to bob
        BEAST_EXPECT(ammCarol.expectBalances(XRP(51), eth(40), ammCarol.tokens()));
        env.require(Balance(bob_, eth(10)));

        auto const result = findPaths(env, alice_, bob_, btc(25));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    receiveMax()
    {
        testcase("Receive max");
        using namespace jtx;
        auto const charlie = Account("charlie");
        {
            // XRP -> MPT receive max
            Env env = pathTestEnv();
            env.fund(XRP(30'000), alice_, bob_, charlie, gw_);

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, charlie},
                 .pay = 11'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const ammCharlie(env, charlie, XRP(10), eth(11'000'000'000'000));
            auto [st, sa, da] = findPaths(env, alice_, bob_, eth(-1), XRP(1).value());
            BEAST_EXPECT(sa == XRP(1));
            BEAST_EXPECT(equal(da, eth(1'000'000'000'000)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw_.id() &&
                    pathElem.getMPTID() == eth.issuanceID());
            }
        }
        {
            // MPT -> XRP receive max
            Env env = pathTestEnv();
            env.fund(XRP(30'000), alice_, bob_, charlie, gw_);

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, charlie},
                 .pay = 11'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const ammCharlie(env, charlie, XRP(11), eth(10'000'000'000'000));
            env.close();
            auto [st, sa, da] =
                findPaths(env, alice_, bob_, drops(-1), eth(1'000'000'000'000).value());
            BEAST_EXPECT(sa == eth(1'000'000'000'000));
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
        testcase("Path Find: XRP -> XRP and XRP -> MPT");
        using namespace jtx;
        Env env = pathTestEnv();
        Account a1{"A1"};
        Account a2{"A2"};
        Account a3{"A3"};
        Account const g1{"G1"};
        Account const g2{"G2"};
        Account g3{"G3"};
        Account m1{"M1"};

        env.fund(XRP(100'000), a1);
        env.fund(XRP(10'000), a2);
        env.fund(XRP(1'000), a3, g1, g2, g3);
        env.fund(XRP(20'000), m1);
        env.close();

        MPTTester const xyzG1(
            {.env = env, .issuer = g1, .holders = {a1, m1, a2}, .flags = kMptDexFlags});

        MPTTester const xyzG2(
            {.env = env, .issuer = g2, .holders = {a2, m1, a1}, .flags = kMptDexFlags});

        MPTTester const abcG3(
            {.env = env, .issuer = g3, .holders = {a1, a2, m1, a3}, .flags = kMptDexFlags});

        MPTTester const abcA2(
            {.env = env, .issuer = a2, .holders = {g3, a1}, .flags = kMptDexFlags});

        env(pay(g1, a1, xyzG1(3'500'000'000)));
        env(pay(g3, a1, abcG3(1'200'000'000)));
        env(pay(g1, m1, xyzG1(25'000'000'000)));
        env(pay(g2, m1, xyzG2(25'000'000'000)));
        env(pay(g3, m1, abcG3(25'000'000'000)));
        env(pay(a2, g3, abcA2(101'000'000)));
        env.close();

        AMM const ammM1XyzG1XyzG2(env, m1, xyzG1(1'000'000'000), xyzG2(1'000'000'000));
        AMM const ammM1XrpAbcG3(env, m1, XRP(10'000), abcG3(1'000'000'000));
        AMM const ammG3AbcG3AbcA2(env, g3, abcG3(100'000'000), abcA2(101'000'000));
        env.close();

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
            auto const& sendAmt = abcG3(10'000'000);
            std::tie(st, sa, da) = findPaths(env, a2, g3, sendAmt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRPAmount{101'010'102}));
            BEAST_EXPECT(same(st, stpath(ipe(MPT(abcG3)))));
        }

        {
            auto const& sendAmt = abcA2(1'000'000);
            std::tie(st, sa, da) = findPaths(env, a1, a2, sendAmt, std::nullopt, xrpCurrency());
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, XRPAmount{10'010'011}));
            BEAST_EXPECT(same(st, stpath(ipe(MPT(abcG3)), ipe(MPT(abcA2)))));
        }
    }

    void
    pathFind02()
    {
        testcase("Path Find: non-XRP -> XRP");
        using namespace jtx;
        Env env = pathTestEnv();
        Account a1{"A1"};
        Account a2{"A2"};
        Account const g3{"G3"};
        Account m1{"M1"};

        env.fund(XRP(1'000), a1, a2, g3);
        env.fund(XRP(11'000), m1);
        env.close();

        MPTTester const eth(
            {.env = env,
             .issuer = g3,
             .holders = {a1, a2, m1},
             .pay = 1'000'000'000,
             .flags = kMptDexFlags});

        AMM const ammM1(env, m1, eth(1'000'000'000), XRP(10'010));

        STPathSet st;
        STAmount sa, da;

        auto const& sendAmt = XRP(10);

        std::tie(st, sa, da) =
            findPathsByElement(env, a1, a2, sendAmt, std::nullopt, ipe(MPT(eth)));
        BEAST_EXPECT(equal(da, sendAmt));
        BEAST_EXPECT(equal(sa, eth(1'000'000)));
        BEAST_EXPECT(same(st, stpath(ipe(xrpIssue()))));
    }

    void
    pathFind06()
    {
        testcase("Path Find: non-XRP -> non-XRP, same issuanceID");
        using namespace jtx;
        {
            Env env = pathTestEnv();
            Account a1{"A1"};
            Account a2{"A2"};
            Account const a3{"A3"};
            Account const g1{"G1"};
            Account const g2{"G2"};
            Account m1{"M1"};

            env.fund(XRP(11'000), m1);
            env.fund(XRP(1'000), a1, a2, a3, g1, g2);
            env.close();

            MPTTester const hkdG1(
                {.env = env,
                 .issuer = g1,
                 .holders = {a1, m1},
                 .pay = 5'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const hkdG2(
                {.env = env,
                 .issuer = g2,
                 .holders = {a2, m1},
                 .pay = 5'000'000'000,
                 .flags = kMptDexFlags});

            AMM const ammM1(env, m1, hkdG1(1'000'000'000), hkdG2(1'010'000'000));

            auto const& sendAmt = hkdG2(10'000'000);
            STPathSet st;
            STAmount sa, da;
            std::tie(st, sa, da) = jtx::findPaths(
                env, g1, a2, sendAmt, std::nullopt, hkdG1.issuanceID(), std::nullopt, std::nullopt);
            BEAST_EXPECT(equal(da, sendAmt));
            BEAST_EXPECT(equal(sa, hkdG1(10'000'000)));
            BEAST_EXPECT(same(st, stpath(ipe(MPT(hkdG2)))));
        }
    }

    void
    testFalseDry(FeatureBitset features)
    {
        testcase("falseDryChanges");

        using namespace jtx;

        Env env(*this, features);
        env.memoize(bob_);

        env.fund(XRP(10'000), alice_, gw_);
        fund(env, gw_, {carol_}, XRP(10'000), {}, Fund::Acct);
        auto const ammxrpPool = env.current()->fees().increment * 2;
        env.fund(reserve(env, 5) + ammCrtFee(env) + ammxrpPool, bob_);
        env.close();

        MPTTester const eth(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_, carol_}, .flags = kMptDexFlags});

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_, carol_}, .flags = kMptDexFlags});

        env(pay(gw_, alice_, eth(50'000)));
        env(pay(gw_, bob_, btc(150'000)));

        // Bob has _just_ slightly less than 50 xrp available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incoming xrp to the XRP/BTC offer will require two
        // recursive calls to the ETH/XRP offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry.
        // This is the second error case to test (when flowV1 is used).
        env(offer(bob_, eth(50'000), XRP(50)));
        AMM const ammBob(env, bob_, ammxrpPool, btc(150'000));

        env(pay(alice_, carol_, btc(1'000'000'000)),
            Path(~XRP, ~MPT(btc)),
            Sendmax(eth(500'000)),
            Txflags(tfNoRippleDirect | tfPartialPayment));

        auto const carolBTC = env.balance(carol_, MPT(btc));
        BEAST_EXPECT(carolBTC > btc(0) && carolBTC < btc(50'000));
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
                env.fund(XRP(30'000), alice_, bob_, carol_, gw_);
                env.close();
                auto const eth = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 100'000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 100'000'000});
                env(pay(gw_, alice_, btc(500000)));
                env(pay(gw_, bob_, btc(500000)));
                env(pay(gw_, carol_, btc(500000)));
                env(pay(gw_, alice_, eth(500000)));
                env(pay(gw_, bob_, eth(500000)));
                env(pay(gw_, carol_, eth(500000)));
                env.close();
                AMM const ammBob(env, bob_, btc(100'000), eth(150'000));

                env(pay(alice_, carol_, eth(50'000)), Path(~eth), Sendmax(btc(50'000)));

                env.require(Balance(alice_, btc(450'000)));
                env.require(Balance(bob_, btc(400'000)));
                env.require(Balance(bob_, eth(350'000)));
                env.require(Balance(carol_, eth(550'000)));
                BEAST_EXPECT(ammBob.expectBalances(btc(150'000), eth(100'000), ammBob.tokens()));
            };
            testHelper2TokensMix(test);
        }

        {
            // simple MPT/XRP XRP/MPT offer
            Env env(*this, features);
            env.fund(XRP(10'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 100'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 150'000,
                 .flags = kMptDexFlags});

            AMM const ammBobBtcXrp(env, bob_, btc(100'000), XRP(150));
            AMM const ammBobXrpEth(env, bob_, XRP(100), eth(150'000));

            env(pay(alice_, carol_, eth(50'000)), Path(~XRP, ~MPT(eth)), Sendmax(btc(50'000)));

            env.require(Balance(alice_, btc(50'000)));
            env.require(Balance(bob_, btc(0)));
            env.require(Balance(bob_, eth(0)));
            env.require(Balance(carol_, eth(200'000)));
            BEAST_EXPECT(
                ammBobBtcXrp.expectBalances(btc(150'000), XRP(100), ammBobBtcXrp.tokens()));
            BEAST_EXPECT(
                ammBobXrpEth.expectBalances(XRP(150), eth(100'000), ammBobXrpEth.tokens()));
        }
        {
            // simple XRP -> MPT through offer and sendmax
            Env env(*this, features);
            XRPAmount const baseFee{env.current()->fees().base};
            env.fund(XRP(10'000), gw_, alice_, bob_, carol_);

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 150'000,
                 .flags = kMptDexFlags});

            AMM const ammBob(env, bob_, XRP(100), eth(150'000));

            env(pay(alice_, carol_, eth(50'000)), Path(~MPT(eth)), Sendmax(XRP(50)));
            BEAST_EXPECT(expectLedgerEntryRoot(env, alice_, XRP(10'000) - XRP(50) - 2 * baseFee));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob_, XRP(10'000) - XRP(100) - ammCrtFee(env) - baseFee));
            env.require(Balance(bob_, eth(0)));
            env.require(Balance(carol_, eth(200'000)));
            BEAST_EXPECT(ammBob.expectBalances(XRP(150), eth(100'000), ammBob.tokens()));
        }
        {
            // simple MPT -> XRP through offer and sendmax
            Env env(*this, features);
            XRPAmount const baseFee{env.current()->fees().base};
            env.fund(XRP(10'000), gw_, alice_, bob_, carol_);

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 100'000,
                 .flags = kMptDexFlags});

            AMM const ammBob(env, bob_, eth(100'000), XRP(150));

            env(pay(alice_, carol_, XRP(50)), Path(~XRP), Sendmax(eth(50'000)));

            env.require(Balance(alice_, eth(50'000)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob_, XRP(10'000) - XRP(150) - ammCrtFee(env) - baseFee));
            env.require(Balance(bob_, eth(0)));
            BEAST_EXPECT(expectLedgerEntryRoot(env, carol_, XRP(10'000 + 50) - baseFee));
            BEAST_EXPECT(ammBob.expectBalances(eth(150'000), XRP(100), ammBob.tokens()));
        }

        // test unfunded offers are removed when payment succeeds
        {
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);
                env.fund(XRP(10'000), alice_, bob_, carol_, gw_);
                env.close();
                auto const btc = issue1(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 1000'000000});
                auto const eth = issue2(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 1000'000000});
                auto const gbp = issue3(
                    {.env = env,
                     .token = "GBP",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 1000'000000});

                env(pay(gw_, alice_, btc(60'000)));
                env(pay(gw_, bob_, eth(200'000)));
                env(pay(gw_, bob_, gbp(150'000)));
                env(offer(bob_, btc(50'000), eth(50'000)));
                env(offer(bob_, btc(40'000), gbp(50'000)));
                env.close();
                AMM const ammBob(env, bob_, gbp(100'000), eth(150'000));

                // unfund offer
                env(pay(bob_, gw_, gbp(50'000)));
                BEAST_EXPECT(isOffer(env, bob_, btc(50'000), eth(50'000)));
                BEAST_EXPECT(isOffer(env, bob_, btc(40'000), gbp(50'000)));
                env(pay(alice_, carol_, eth(50'000)),
                    Path(~eth),
                    Path(~gbp, ~eth),
                    Sendmax(btc(60'000)));
                env.require(Balance(alice_, btc(10'000)));
                env.require(Balance(bob_, btc(50'000)));
                env.require(Balance(bob_, eth(0)));
                env.require(Balance(bob_, gbp(0)));
                env.require(Balance(carol_, eth(50'000)));
                // used in the payment
                BEAST_EXPECT(!isOffer(env, bob_, btc(50'000), eth(50'000)));
                // found unfunded
                BEAST_EXPECT(!isOffer(env, bob_, btc(40'000), gbp(50'000)));
                // unchanged
                BEAST_EXPECT(ammBob.expectBalances(gbp(100'000), eth(150'000), ammBob.tokens()));
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

            env.fund(XRP(10'000), bob_, carol_, gw_);
            env.close();
            // Sets rippling on, this is different from
            // the original test
            fund(env, gw_, {alice_}, XRP(10'000), {}, Fund::Acct);

            MPTTester btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .flags = kMptDexFlags});

            MPTTester eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .flags = kMptDexFlags});

            MPTTester gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .flags = kMptDexFlags});

            env(pay(gw_, alice_, btc(60'000'000)));
            env(pay(gw_, bob_, btc(100'000'000)));
            env(pay(gw_, bob_, eth(100'000'000)));
            env(pay(gw_, bob_, gbp(50'000'000)));
            env(pay(gw_, carol_, gbp(1'000'000)));
            env.close();

            // This is multiplath, which generates limited # of offers
            AMM const ammBobBtcEth(env, bob_, btc(50'000'000), eth(50'000'000));
            env(offer(bob_, btc(60'000'000), gbp(50'000'000)));
            env(offer(carol_, btc(1'000'000'000), gbp(1'000'000)));
            env(offer(bob_, gbp(50'000'000), eth(50'000'000)));

            // unfund offer
            env(pay(bob_, gw_, gbp(50'000'000)));
            BEAST_EXPECT(ammBobBtcEth.expectBalances(
                btc(50'000'000), eth(50'000'000), ammBobBtcEth.tokens()));
            BEAST_EXPECT(isOffer(env, bob_, btc(60'000'000), gbp(50'000'000)));
            BEAST_EXPECT(isOffer(env, carol_, btc(1'000'000'000), gbp(1'000'000)));
            BEAST_EXPECT(isOffer(env, bob_, gbp(50'000'000), eth(50'000'000)));

            auto flowJournal = env.app().getLogs().journal("Flow");
            auto const flowResult = [&] {
                STAmount const deliver(eth(51'000'000));
                STAmount smax(btc(61'000'000));
                PaymentSandbox sb(env.current().get(), TapNone);
                STPathSet paths;
                auto ipe = [](MPTTester const& iss) {
                    return STPathElement(
                        STPathElement::TypeMpt | STPathElement::TypeIssuer,
                        xrpAccount(),
                        PathAsset{iss.issuanceID()},
                        iss.issuer());
                };
                {
                    // BTC -> ETH
                    STPath const p1({ipe(eth)});
                    paths.pushBack(p1);
                    // BTC -> GBP -> ETH
                    STPath const p2({ipe(gbp), ipe(eth)});
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
                    {
                        offerDelete(sb, ok, flowJournal);
                    }
                }
                sb.apply(view);
                return true;
            });

            // used in payment, but since payment failed should be untouched
            BEAST_EXPECT(ammBobBtcEth.expectBalances(
                btc(50'000'000), eth(50'000'000), ammBobBtcEth.tokens()));
            BEAST_EXPECT(isOffer(env, carol_, btc(1'000'000'000), gbp(1'000'000)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob_, btc(60'000'000), gbp(50'000'000)));
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
                env.fund(XRP(30'000), alice_, bob_, carol_, gw_);
                env.close();
                auto const eth = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 10'000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 10'000'000});

                env(pay(gw_, alice_, eth(1'000'000)));
                env(pay(gw_, bob_, btc(1'000'000)));
                env(pay(gw_, bob_, eth(1'000'000)));
                env.close();

                AMM const ammBob(env, bob_, eth(8'000), XRPAmount{21});
                env(offer(bob_, drops(1), btc(1'000'000)), Txflags(tfPassive));

                env(pay(alice_, carol_, btc(1'000)),
                    Path(~XRP, ~btc),
                    Sendmax(eth(400)),
                    Txflags(tfNoRippleDirect | tfPartialPayment));

                env.require(Balance(carol_, btc(1'000)));
                BEAST_EXPECT(ammBob.expectBalances(eth(8400), XRPAmount{20}, ammBob.tokens()));
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
            env.fund(XRP(1'000), gw_, alice_, bob_, carol_);

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const amm(env, bob_, gbp(1'000'000'000'000'000), btc(1'000'000'000'000'000));

            env(pay(alice_, carol_, btc(100'000'000'000'000)),
                Path(~MPT(btc)),
                Sendmax(gbp(150'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice buys 107.1428e12BTC with 120e12GBP and pays 25% tr fee on
            // 120e12GBP 1,000e12 - 120e12*1.25 = 850e12GBP
            env.require(Balance(alice_, gbp(850'000'000'000'000)));

            BEAST_EXPECT(amm.expectBalances(
                gbp(1'120'000'000'000'000), btc(892'857'142'857'143), amm.tokens()));

            // 25% of 85.7142e12BTC is paid in tr fee
            // 85.7142e12*1.25 = 107.1428e12BTC
            env.require(Balance(carol_, btc(1'085'714'285'714'285)));
        }
        {
            // Payment via offer and AMM
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_, ed);

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            env(offer(ed, gbp(1'000'000'000'000'000), eth(1'000'000'000'000'000)),
                Txflags(tfPassive));
            env.close();

            AMM const amm(env, bob_, eth(1'000'000'000'000'000), btc(1'000'000'000'000'000));

            env(pay(alice_, carol_, btc(100'000'000'000'000)),
                Path(~MPT(eth), ~MPT(btc)),
                Sendmax(gbp(150'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            // alice buys 120e12ETH with 120e12GBP via the offer
            // and pays 25% tr fee on 120e12GBP
            // 1,000e12 - 120e12*1.25 = 850e12GBP
            env.require(Balance(alice_, gbp(850'000'000'000'000)));
            // consumed offer is 120e12GBP/120e12ETH
            // ed doesn't pay tr fee
            env.require(Balance(ed, eth(880'000'000'000'000)));
            env.require(Balance(ed, gbp(1'120'000'000'000'000)));
            BEAST_EXPECT(expectOffers(
                env, ed, 1, {Amounts{gbp(880'000'000'000'000), eth(880'000'000'000'000)}}));
            // 25% on 96e12ETH is paid in tr fee 96e12*1.25 = 120e12ETH
            // 96e12ETH is swapped in for 87.5912e12BTC
            BEAST_EXPECT(amm.expectBalances(
                eth(1'096'000'000'000'000), btc(912'408'759'124'088), amm.tokens()));
            // 25% on 70.0729e12BTC is paid in tr fee 70.0729e12*1.25
            // = 87.5912e12BTC
            env.require(Balance(carol_, btc(1'070'072'992'700'729)));
        }
        {
            // Payment via AMM, AMM
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_, ed);

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const amm1(env, bob_, gbp(1'000'000'000'000'000), eth(1'000'000'000'000'000));
            AMM const amm2(env, ed, eth(1'000'000'000'000'000), btc(1'000'000'000'000'000));

            env(pay(alice_, carol_, btc(100'000'000'000'000)),
                Path(~MPT(eth), ~MPT(btc)),
                Sendmax(gbp(150'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();

            env.require(Balance(alice_, gbp(850'000'000'000'000)));

            // alice buys 107.1428e12ETH with 120e12GBP and pays 25% tr fee on
            // 120e12GBP 1,000e12 - 120e12*1.25 = 850e12GBP 120e12GBP is swapped
            // in for 107.1428e12ETH
            BEAST_EXPECT(amm1.expectBalances(
                gbp(1'120'000'000'000'000), eth(892'857'142'857'143), amm1.tokens()));
            // 25% on 85.7142e12ETH is paid in tr fee 85.7142e12*1.25 =
            // 107.1428e12ETH 85.7142e12ETH is swapped in for 78.9473e12BTC
            BEAST_EXPECT(amm2.expectBalances(
                eth(1'085'714'285'714'285), btc(921'052'631'578'948), amm2.tokens()));

            // 25% on 63.1578e12BTC is paid in tr fee 63.1578e12*1.25
            // = 78.9473e12BTC
            env.require(Balance(carol_, btc(1'063'157'894'736'841)));
        }
        {
            // AMM offer crossing
            Env env(*this, features);

            env.fund(XRP(1'000), gw_, alice_, bob_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_},
                 .transferFee = 25'000,
                 .pay = 1'100'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_},
                 .transferFee = 25'000,
                 .pay = 1'100'000,
                 .flags = kMptDexFlags});

            AMM const amm(env, bob_, btc(1'000'000), eth(1'100'000));
            env(offer(alice_, eth(100'000), btc(100'000)));
            env.close();

            // 100e3BTC is swapped in for 100e3ETH
            BEAST_EXPECT(amm.expectBalances(btc(1'100'000), eth(1'000'000), amm.tokens()));
            // alice pays 25% tr fee on 100e3BTC 1100e3-100e3*1.25 = 975e3BTC
            env.require(Balance(alice_, btc(975'000)));
            env.require(Balance(alice_, eth(1'200'000)));
            BEAST_EXPECT(expectOffers(env, alice_, 0));
        }
        {
            // Payment via AMM with limit quality
            Env env(*this, features);

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'000'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const amm(env, bob_, gbp(1'000'000'000'000'000), btc(1'000'000'000'000'000));

            // requested quality limit is 100e12BTC/178.58e12GBP = 0.55997
            // trade quality is 100e12BTC/178.5714 = 0.55999e12
            env(pay(alice_, carol_, btc(100'000'000'000'000)),
                Path(~MPT(btc)),
                Sendmax(gbp(178'580'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 125e12BTC with 142.8571e12GBP and pays 25% tr fee
            // on 142.8571e12GBP
            // 1,000e12 - 142.8571e12*1.25 = 821.4285e12GBP
            env.require(Balance(alice_, gbp(821'428'571'428'571)));
            // 142.8571e12GBP is swapped in for 125e12BTC
            BEAST_EXPECT(amm.expectBalances(
                gbp(1'142'857'142'857'143), btc(875'000'000'000'000), amm.tokens()));
            // 25% on 100e12BTC is paid in tr fee
            // 100e12*1.25 = 125e12BTC
            env.require(Balance(carol_, btc(1'100'000'000'000'000)));
        }
        {
            // Payment via AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'200'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'200'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const amm(env, bob_, gbp(1'000'000'000'000'000), btc(1'200'000'000'000'000));

            // requested quality limit is 90e12BTC/120e12GBP = 0.75
            // trade quality is 22.5e12BTC/30e12GBP = 0.75
            env(pay(alice_, carol_, btc(90'000'000'000'000)),
                Path(~MPT(btc)),
                Sendmax(gbp(120'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 28.125e12BTC with 24e12GBP and pays 25% tr fee
            // on 24e12GBP
            // 1,200e12 - 24e12*1.25 =~ 1,170e12GBP
            env.require(Balance(alice_, gbp(1'170'000'000'000'000)));
            // 24e12GBP is swapped in for 28.125e12BTC
            BEAST_EXPECT(amm.expectBalances(
                gbp(1'024'000'000'000'000), btc(1'171'875'000'000'000), amm.tokens()));

            // 25% on 22.5e12BTC is paid in tr fee
            // 22.5*1.25 = 28.125e12BTC
            env.require(Balance(carol_, btc(1'222'500'000'000'000)));
        }
        {
            // Payment via offer and AMM with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_, ed);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            env(offer(ed, gbp(1'000'000'000'000'000), eth(1'000'000'000'000'000)),
                Txflags(tfPassive));
            env.close();

            AMM const amm(env, bob_, eth(1'000'000'000'000'000), btc(1'400'000'000'000'000));

            // requested quality limit is 95e12BTC/140e12GBP = 0.6785
            // trade quality is 59.7321e12BTC/88.0262e12GBP = 0.6785
            env(pay(alice_, carol_, btc(95'000'000'000'000)),
                Path(~MPT(eth), ~MPT(btc)),
                Sendmax(gbp(140'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 70.4210e12ETH with 70.4210e12GBP via the offer
            // and pays 25% tr fee on 70.4210e12GBP
            // 1,400e12 - 70.4210e12*1.25 = 1400e12 - 88.0262e12 =
            // 1311.9736e12GBP
            env.require(Balance(alice_, gbp(1'311'973'684'210'525)));
            // ed doesn't pay tr fee, the balances reflect consumed offer
            // 70.4210e12GBP/70.4210e12ETH
            env.require(Balance(ed, eth(1'329'578'947'368'420)));
            env.require(Balance(ed, gbp(1'470'421'052'631'580)));
            BEAST_EXPECT(expectOffers(
                env, ed, 1, {Amounts{gbp(929'578'947'368'420), eth(929'578'947'368'420)}}));
            // 25% on 56.3368e12ETH is paid in tr fee 56.3368e12*1.25
            // = 70.4210e12ETH 56.3368e12ETH is swapped in for 74.6651e12BTC
            BEAST_EXPECT(amm.expectBalances(
                eth(1'056'336'842'105'264), btc(1'325'334'821'428'571), amm.tokens()));

            // 25% on 59.7321e12BTC is paid in tr fee 59.7321e12*1.25
            // = 74.6651e12BTC
            env.require(Balance(carol_, btc(1'459'732'142'857'143)));
        }
        {
            // Payment via AMM and offer with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_, ed);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const amm(env, bob_, gbp(1'000'000'000'000'000), eth(1'000'000'000'000'000));

            env(offer(ed, eth(1'000'000'000'000'000), btc(1'400'000'000'000'000)),
                Txflags(tfPassive));
            env.close();

            // requested quality limit is 95e12BTC/140e12GBP = 0.6785
            // trade quality is 47.7857e12BTC/70.4210e12GBP = 0.6785
            env(pay(alice_, carol_, btc(95'000'000'000'000)),
                Path(~MPT(eth), ~MPT(btc)),
                Sendmax(gbp(140'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 53.3322e12ETH with 56.3368e12GBP via the amm
            // and pays 25% tr fee on 56.3368e12GBP
            // 1,400e12 - 56.3368e12*1.25 = 1400e12 - 70.4210e12 =
            // 1329.5789e12GBP
            env.require(Balance(alice_, gbp(1'329'578'947'368'420)));
            //// 25% on 56.3368e12ETH is paid in tr fee 56.3368e12*1.25
            ///= 70.4210e12ETH
            // 56.3368e12GBP is swapped in for 53.3322e12ETH
            BEAST_EXPECT(amm.expectBalances(
                gbp(1'056'336'842'105'264), eth(946'667'729'591'836), amm.tokens()));

            // 25% on 42.6658e12ETH is paid in tr fee 42.6658e12*1.25
            // = 53.3322e12ETH 42.6658e12ETH/59.7321e12BTC
            env.require(Balance(ed, btc(1'340'267'857'142'857)));
            env.require(Balance(ed, eth(1'442'665'816'326'531)));
            BEAST_EXPECT(expectOffers(
                env, ed, 1, {Amounts{eth(957'334'183'673'469), btc(1'340'267'857'142'857)}}));
            // 25% on 47.7857e12BTC is paid in tr fee 47.7857e12*1.25
            // = 59.7321e12BTC
            env.require(Balance(carol_, btc(1'447'785714285714)));
        }
        {
            // Payment via AMM, AMM  with limit quality, deliver less
            // than requested
            Env env(*this, features);
            Account const ed("ed");

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_, ed);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, ed},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const amm1(env, bob_, gbp(1'000'000'000'000'000), eth(1'000'000'000'000'000));
            AMM const amm2(env, ed, eth(1'000'000'000'000'000), btc(1'400'000'000'000'000));

            // requested quality limit is 90e12BTC/145e12GBP = 0.6206
            // trade quality is 66.7432e12BTC/107.5308e12GBP = 0.6206
            env(pay(alice_, carol_, btc(90'000'000'000'000)),
                Path(~MPT(eth), ~MPT(btc)),
                Sendmax(gbp(145'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // alice buys 53.3322e12ETH with 107.5308e12GBP
            // 25% on 86.0246e12GBP is paid in tr fee
            // 1,400e12 - 86.0246e12*1.25 = 1400e12 - 107.5308e12 =
            // 1229.4691e12GBP
            env.require(Balance(alice_, gbp(1'292'469'135'802'465)));
            // 86.0246e12GBP is swapped in for 79.2106e12ETH
            BEAST_EXPECT(amm1.expectBalances(
                gbp(1'086'024'691'358'028), eth(920'789'377'955'618), amm1.tokens()));
            // 25% on 63.3684e12ETH is paid in tr fee 63.3684e12*1.25
            // = 79.2106e12ETH 63.3684e12ETH is swapped in for 83.4291e12BTC
            BEAST_EXPECT(amm2.expectBalances(
                eth(1'063'368'497'635'505), btc(1'316'570'881'226'053), amm2.tokens()));

            // 25% on 66.7432e12BTC is paid in tr fee 66.7432e12*1.25
            // = 83.4291e12BTC
            env.require(Balance(carol_, btc(1'466'743'295'019'157)));
        }
        {
            // Payment by the issuer via AMM, AMM  with limit quality,
            // deliver less than requested
            Env env(*this, features);

            env.fund(XRP(1'000), gw_, alice_, bob_, carol_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const gbp(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .transferFee = 25'000,
                 .pay = 1'400'000'000'000'000,
                 .flags = kMptDexFlags});

            AMM const amm1(env, alice_, gbp(1'000'000'000'000'000), eth(1'000'000'000'000'000));
            AMM const amm2(env, bob_, eth(1'000'000'000'000'000), btc(1'400'000'000'000'000));

            // requested quality limit is 90e12BTC/120e12GBP = 0.75
            // trade quality is 81.1111e12BTC/108.1481e12GBP = 0.75
            env(pay(gw_, carol_, btc(90'000'000'000'000)),
                Path(~MPT(eth), ~MPT(btc)),
                Sendmax(gbp(120'000'000'000'000)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();

            // 108.1481e12GBP is swapped in for 97.5935e12ETH
            BEAST_EXPECT(amm1.expectBalances(
                gbp(1'108'148'148'148'150), eth(902'406'417'112'298), amm1.tokens()));
            // 25% on 78.0748e12ETH is paid in tr fee 78.0748e12*1.25
            // = 97.5935e12ETH 78.0748e12ETH is swapped in for 101.3888e12BTC
            BEAST_EXPECT(amm2.expectBalances(
                eth(1'078'074'866'310'161), btc(1'298'611'111'111'111), amm2.tokens()));

            // 25% on 81.1111e12BTC is paid in tr fee 81.1111e12*1.25 =
            // 101.3888e12BTC
            env.require(Balance(carol_, btc(1'481'111'111'111'111)));
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
            env.fund(XRP(10'000), gw_, alice_, bob_, carol_);

            MPTTester const eth(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 2'000'000,
                 .flags = kMptDexFlags});

            AMM const ammBob(env, bob_, XRP(1'000), eth(1'050'000));
            env(offer(bob_, XRP(100), eth(50'000)));

            env(pay(alice_, carol_, eth(100'000)),
                Path(~MPT(eth)),
                Sendmax(XRP(100)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));

            BEAST_EXPECT(ammBob.expectBalances(XRP(1'050), eth(1'000'000), ammBob.tokens()));
            env.require(Balance(carol_, eth(2'050'000)));
            BEAST_EXPECT(expectOffers(env, bob_, 1, {{{XRP(100), eth(50'000)}}}));
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
                env.fund(XRP(30'000), alice_, bob_, gw_);
                env.close();
                auto const eth = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw_,
                     .holders = {alice_, bob_},
                     .limit = 2000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_},
                     .limit = 2000'000});

                env(pay(gw_, alice_, btc(200'000)));
                env(pay(gw_, bob_, btc(200'000)));
                env(pay(gw_, alice_, eth(200'000)));
                env(pay(gw_, bob_, eth(200'000)));
                env.close();

                AMM const ammAliceXrpBtc(env, alice_, XRP(100), btc(101'000));
                AMM const ammAliceXrpEth(env, alice_, XRP(100), eth(101'000));
                env(pay(alice_, bob_, eth(1'000)),
                    Path(~btc, ~XRP, ~eth),
                    Sendmax(XRP(1)),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
            };
            testHelper2TokensMix(test);
        }

        // Payment path ending with XRP
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice_, bob_, gw_);
                env.close();
                auto const eth = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw_,
                     .holders = {alice_, bob_},
                     .limit = 2000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_},
                     .limit = 2000'000});

                env(pay(gw_, alice_, btc(200'000)));
                env(pay(gw_, bob_, btc(200'000)));
                env(pay(gw_, alice_, eth(200'000)));
                env(pay(gw_, bob_, eth(200'000)));
                env.close();

                AMM const ammAliceXrpBtc(env, alice_, XRP(100), btc(100'000));
                AMM const ammAliceXrpEth(env, alice_, XRP(100), eth(100'000));
                // ETH -> //XRP -> //BTC ->XRP
                env(pay(alice_, bob_, XRP(1)),
                    Path(~XRP, ~btc, ~XRP),
                    Sendmax(eth(1'000)),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
            };
            testHelper2TokensMix(test);
        }

        // Payment where loop is formed in the middle of the path, not
        // on an endpoint
        {
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this);
                env.fund(XRP(10'000), gw_, alice_, bob_);
                env.close();
                auto const eth = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw_,
                     .holders = {alice_, bob_},
                     .limit = 2000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_},
                     .limit = 2000'000});
                auto const jpy = issue2(
                    {.env = env,
                     .token = "JPY",
                     .issuer = gw_,
                     .holders = {alice_, bob_},
                     .limit = 2000'000});

                env(pay(gw_, alice_, btc(200'000)));
                env(pay(gw_, bob_, btc(200'000)));
                env(pay(gw_, alice_, eth(200'000)));
                env(pay(gw_, bob_, eth(200'000)));
                env(pay(gw_, alice_, jpy(200'000)));
                env(pay(gw_, bob_, jpy(200'000)));
                env.close();

                AMM const ammAliceXrpBtc(env, alice_, XRP(100), btc(100'000));
                AMM const ammAliceXrpEth(env, alice_, XRP(100), eth(100'000));
                AMM const ammAliceXrpJpy(env, alice_, XRP(100), jpy(100'000));

                env(pay(alice_, bob_, jpy(1'000)),
                    Path(~XRP, ~eth, ~XRP, ~jpy),
                    Sendmax(btc(1'000)),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
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

            env.fund(XRP(100'000'000), gw_, alice_, bob_, carol_, dan, ed);

            MPTTester const btc(
                {.env = env, .issuer = gw_, .holders = {bob_, dan, ed}, .flags = kMptDexFlags});

            env(pay(gw_, ed, btc(11'000'000'000'000)));
            env(pay(gw_, bob_, btc(1'000'000'000'000)));
            env(pay(gw_, dan, btc(1'000'000'000'000)));

            nOffers(env, 2'000, bob_, XRP(1), btc(1'000'000'000'000));
            nOffers(env, 1, dan, XRP(1), btc(1'000'000'000'000));
            AMM const ammEd(env, ed, XRP(9), btc(11'000'000'000'000));

            // Alice offers to buy 1000 XRP for 1000e12 BTC. She takes Bob's
            // first offer, removes 999 more as unfunded, then hits the step
            // limit.
            env(offer(alice_, btc(1'000'000'000'000'000), XRP(1'000)));
            env.require(Balance(alice_, btc(2'050'125'257'867)));
            env.require(Owners(alice_, 2));
            env.require(Balance(bob_, btc(0)));
            env.require(Owners(bob_, 1'001));
            env.require(Balance(dan, btc(1'000'000'000'000)));
            env.require(Owners(dan, 2));

            // Carol offers to buy 1000 XRP for 1000e12 BTC. She removes Bob's
            // next 1000 offers as unfunded and hits the step limit.
            env(offer(carol_, btc(1'000'000'000'000'000), XRP(1'000)));
            env.require(Balance(carol_, MPT(btc)(kNone)));
            env.require(Owners(carol_, 1));
            env.require(Balance(bob_, btc(0)));
            env.require(Owners(bob_, 1));
            env.require(Balance(dan, btc(1'000'000'000'000)));
            env.require(Owners(dan, 2));
        }

        // MPT/IOU, similar to the case above
        {
            Env env(*this, features);
            auto const dan = Account("dan");
            auto const ed = Account("ed");

            env.fund(XRP(100'000), gw_, alice_, bob_, carol_, dan, ed);
            env.close();

            MPTTester const usd(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, dan, ed},
                 .pay = 10000'000'000,
                 .flags = kMptDexFlags});

            env.trust(BTC(11'000'000'000'000), ed);
            env(pay(gw_, ed, BTC(11'000'000'000'000)));
            env.trust(BTC(1'000'000'000'000), bob_);
            env(pay(gw_, bob_, BTC(1'000'000'000'000)));
            env.trust(BTC(1'000'000'000'000), dan);
            env(pay(gw_, dan, BTC(1'000'000'000'000)));
            env.close();

            nOffers(env, 2'000, bob_, usd(1000000), BTC(1'000'000'000'000));
            nOffers(env, 1, dan, usd(1000000), BTC(1'000'000'000'000));
            AMM const ammEd(env, ed, usd(9000000), BTC(11'000'000'000'000));
            env(offer(alice_, BTC(1'000'000'000'000'000), usd(1'000000000)));

            env.require(Balance(alice_, STAmount{BTC, UINT64_C(2050125257867'587), -3}));
            env.require(Owners(alice_, 3));
            env.require(Balance(bob_, BTC(0)));
            env.require(Owners(bob_, 1'002));
            env.require(Balance(dan, BTC(1000000000000)));
            env.require(Owners(dan, 3));
        }

        // IOU/MPT, similar to the case above
        {
            Env env(*this, features);
            auto const dan = Account("dan");
            auto const ed = Account("ed");

            env.fund(XRP(100'000), gw_, alice_, bob_, carol_, dan, ed);
            env.close();

            env.trust(USD(10000'000'000), alice_);
            env(pay(gw_, alice_, USD(10000'000'000)));
            env.trust(USD(10000'000'000), bob_);
            env(pay(gw_, bob_, USD(10000'000'000)));
            env.trust(USD(10000'000'000), carol_);
            env(pay(gw_, carol_, USD(10000'000'000)));
            env.trust(USD(10000'000'000), dan);
            env(pay(gw_, dan, USD(10000'000'000)));
            env.trust(USD(10000'000'000), ed);
            env(pay(gw_, ed, USD(10000'000'000)));
            env.close();

            MPTTester const btc(
                {.env = env, .issuer = gw_, .holders = {bob_, dan, ed}, .flags = kMptDexFlags});

            env(pay(gw_, ed, btc(11'000'000'000'000)));
            env(pay(gw_, bob_, btc(1'000'000'000'000)));
            env(pay(gw_, dan, btc(1'000'000'000'000)));
            env.close();

            nOffers(env, 2'000, bob_, USD(1000000), btc(1'000'000'000'000));
            nOffers(env, 1, dan, USD(1000000), btc(1'000'000'000'000));
            AMM const ammEd(env, ed, USD(9000000), btc(11'000'000'000'000));
            env(offer(alice_, btc(1'000'000'000'000'000), USD(1'000000000)));

            env.require(Balance(alice_, btc(2050125628933)));
            env.require(Owners(alice_, 3));
            env.require(Balance(bob_, btc(0)));
            env.require(Owners(bob_, 1'002));
            env.require(Balance(dan, btc(1000000000000)));
            env.require(Owners(dan, 3));
        }

        // MPT/MPT, similar to the case above
        {
            Env env(*this, features);
            auto const dan = Account("dan");
            auto const ed = Account("ed");

            env.fund(XRP(100'000), gw_, alice_, bob_, carol_, dan, ed);
            env.close();

            MPTTester const btc(
                {.env = env, .issuer = gw_, .holders = {bob_, dan, ed}, .flags = kMptDexFlags});
            MPTTester const usd(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_, dan, ed},
                 .pay = 10000'000'000,
                 .flags = kMptDexFlags});

            env(pay(gw_, ed, btc(11'000'000'000'000)));
            env(pay(gw_, bob_, btc(1'000'000'000'000)));
            env(pay(gw_, dan, btc(1'000'000'000'000)));
            env.close();

            nOffers(env, 2'000, bob_, usd(1000000), btc(1'000'000'000'000));
            nOffers(env, 1, dan, usd(1000000), btc(1'000'000'000'000));
            AMM const ammEd(env, ed, usd(9000000), btc(11'000'000'000'000));
            env(offer(alice_, btc(1'000'000'000'000'000), usd(1'000000000)));

            env.require(Balance(alice_, btc(2050125257867)));
            env.require(Owners(alice_, 3));
            env.require(Balance(bob_, btc(0)));
            env.require(Owners(bob_, 1'002));
            env.require(Balance(dan, btc(1000000000000)));
            env.require(Owners(dan, 3));
        }
    }

    void
    testConvertAllOfAnAsset(FeatureBitset features)
    {
        testcase("Convert all of an asset using DeliverMin");

        using namespace jtx;

        {
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .flags = kMptDexFlags});

            env(pay(alice_, bob_, btc(10'000)), DeliverMin(btc(10'000)), Ter(temBAD_AMOUNT));
            env(pay(alice_, bob_, btc(10'000)),
                DeliverMin(btc(-5'000)),
                Txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(alice_, bob_, btc(10'000)),
                DeliverMin(XRP(5)),
                Txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(alice_, bob_, btc(10'000)),
                DeliverMin(btc(5'000)),
                Txflags(tfPartialPayment),
                Ter(tecPATH_DRY));
            env(pay(alice_, bob_, btc(10'000)),
                DeliverMin(btc(15'000)),
                Txflags(tfPartialPayment),
                Ter(temBAD_AMOUNT));
            env(pay(gw_, carol_, btc(50'000)));
            AMM const ammCarol(env, carol_, XRP(10), btc(15'000));
            env(pay(alice_, bob_, btc(10'000)),
                Paths(XRP),
                DeliverMin(btc(7'000)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(5)),
                Ter(tecPATH_PARTIAL));
            env.require(
                Balance(alice_, drops(10'000'000'000 - (3 * env.current()->fees().base.drops()))));
            env.require(Balance(bob_, drops(10'000'000'000 - env.current()->fees().base.drops())));
        }

        {
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_}, XRP(10'000));

            MPTTester const btc(
                {.env = env, .issuer = gw_, .holders = {alice_, bob_}, .flags = kMptDexFlags});

            env(pay(gw_, bob_, btc(1'100'000)));
            AMM const ammBob(env, bob_, XRP(1'000), btc(1'100'000));
            env(pay(alice_, alice_, btc(10'000'000)),
                Paths(XRP),
                DeliverMin(btc(100'000)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(100)));
            env.require(Balance(alice_, btc(100'000)));
        }

        // IOU/MPT mix, similar to the above case
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice_, bob_, carol_, gw_);
                env.close();
                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 3000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 1'000'000});

                env(pay(gw_, alice_, usd(10'000)));
                env(pay(gw_, bob_, usd(10'000)));
                env(pay(gw_, bob_, btc(1'200)));
                env.close();

                AMM const ammBob(env, bob_, usd(1'000), btc(1'100));
                env(pay(alice_, alice_, btc(10'000)),
                    Paths(usd),
                    DeliverMin(btc(100)),
                    Txflags(tfPartialPayment),
                    Sendmax(usd(100)));
                env.require(Balance(alice_, btc(100)));
            };
            testHelper2TokensMix(test);
        }

        {
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));

            MPTTester const btc(
                {.env = env, .issuer = gw_, .holders = {bob_, carol_}, .flags = kMptDexFlags});

            env(pay(gw_, bob_, btc(1'200'000)));
            AMM const ammBob(env, bob_, XRP(5'500), btc(1'200'000));
            env(pay(alice_, carol_, btc(10'000'000)),
                Paths(XRP),
                DeliverMin(btc(200'000)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(1'000)),
                Ter(tecPATH_PARTIAL));
            env(pay(alice_, carol_, btc(10'000'000)),
                Paths(XRP),
                DeliverMin(btc(200'000)),
                Txflags(tfPartialPayment),
                Sendmax(XRP(1'100)));
            BEAST_EXPECT(ammBob.expectBalances(XRP(6'600), btc(1'000'000), ammBob.tokens()));
            env.require(Balance(carol_, btc(200'000)));
        }

        // IOU/MPT mix, similar to the above case
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice_, bob_, carol_, gw_);
                env.close();
                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 3000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 1'000'000});

                env(pay(gw_, alice_, usd(100'000)));
                env(pay(gw_, bob_, usd(100'000)));
                env(pay(gw_, carol_, usd(100'000)));

                env(pay(gw_, bob_, btc(1'200)));
                env.close();

                AMM const ammBob(env, bob_, usd(5'500), btc(1'200));
                env(pay(alice_, carol_, btc(10'000)),
                    Paths(usd),
                    DeliverMin(btc(200)),
                    Txflags(tfPartialPayment),
                    Sendmax(usd(1'000)),
                    Ter(tecPATH_PARTIAL));
                env(pay(alice_, carol_, btc(10'000)),
                    Paths(usd),
                    DeliverMin(btc(200)),
                    Txflags(tfPartialPayment),
                    Sendmax(usd(1'100)));
                BEAST_EXPECT(ammBob.expectBalances(usd(6'600), btc(1'000), ammBob.tokens()));
                env.require(Balance(carol_, btc(200)));
            };
            testHelper2TokensMix(test);
        }

        {
            auto const dan = Account("dan");
            Env env(*this, features);
            fund(env, gw_, {alice_, bob_, carol_, dan}, XRP(10'000));

            MPTTester const btc(
                {.env = env, .issuer = gw_, .holders = {bob_, carol_, dan}, .flags = kMptDexFlags});

            env(pay(gw_, bob_, btc(100'000'000)));
            env(pay(gw_, dan, btc(1'100'000'000)));
            env(offer(bob_, XRP(100), btc(100'000'000)));
            env(offer(bob_, XRP(1'000), btc(100'000'000)));
            AMM const ammDan(env, dan, XRP(1'000), btc(1'100'000'000));

            env(pay(alice_, carol_, btc(10'000'000'000)),
                Paths(XRP),
                DeliverMin(btc(200'000'000)),
                Txflags(tfPartialPayment),
                Sendmax(XRPAmount(200'000'001)));
            env.require(Balance(bob_, btc(0)));
            env.require(Balance(carol_, btc(200'000'000)));
            BEAST_EXPECT(
                ammDan.expectBalances(XRPAmount{1'100'000'001}, btc(1000'000000), ammDan.tokens()));
        }
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace jtx;
        Account const becky{"becky"};

        Env env(*this, features);
        fund(env, gw_, {alice_, becky}, XRP(5'000));

        MPTTester const btc(
            {.env = env, .issuer = gw_, .holders = {alice_, becky}, .flags = kMptDexFlags});

        env(pay(gw_, alice_, btc(500'000)));
        env.close();

        AMM const ammAlice(env, alice_, XRP(100), btc(140'000));

        // becky pays herself BTC (10'000) by consuming part of alice's offer.
        // Make sure the payment works if PaymentAuth is not involved.
        env(pay(becky, becky, btc(10'000)), Path(~MPT(btc)), Sendmax(XRP(10)));
        env.close();
        BEAST_EXPECT(
            ammAlice.expectBalances(XRPAmount(107'692'308), btc(130'000), ammAlice.tokens()));

        // becky decides to require authorization for deposits.
        env(fset(becky, asfDepositAuth));
        env.close();

        // becky pays herself again.
        env(pay(becky, becky, btc(10'000)), Path(~MPT(btc)), Sendmax(XRP(10)), Ter(tesSUCCESS));

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

        fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));

        MPTTester btc(
            {.env = env, .issuer = gw_, .holders = {alice_, bob_, carol_}, .flags = kMptDexFlags});

        env(pay(gw_, alice_, btc(150'000)));
        env(pay(gw_, carol_, btc(150'000)));
        AMM const ammCarol(env, carol_, btc(100'000), XRPAmount(101));

        env(pay(alice_, bob_, btc(50'000)));
        env.close();

        // bob sets the lsfDepositAuth flag.
        env(fset(bob_, asfDepositAuth), Require(Flags(bob_, asfDepositAuth)));
        env.close();

        // None of the following payments should succeed.
        auto failedMptPayments = [this, &env, &btc]() {
            env.require(Flags(bob_, asfDepositAuth));

            // Capture bob's balances before hand to confirm they don't
            // change.
            PrettyAmount const bobXrpBalance{env.balance(bob_, XRP)};
            PrettyAmount const bobBTCBalance{env.balance(bob_, MPT(btc))};

            env(pay(alice_, bob_, btc(50'000)), Ter(tecNO_PERMISSION));
            env.close();

            // Note that even though alice is paying bob in XRP, the payment
            // is still not allowed since the payment passes through an
            // offer.
            env(pay(alice_, bob_, drops(1)), Sendmax(btc(1'000)), Ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(bobXrpBalance == env.balance(bob_, XRP));
            BEAST_EXPECT(bobBTCBalance == env.balance(bob_, MPT(btc)));
        };

        //  Test when bob has an XRP balance > base reserve.
        failedMptPayments();

        // Set bob's XRP balance == base reserve.  Also demonstrate that
        // bob can make payments while his lsfDepositAuth flag is set.
        env(pay(bob_, alice_, btc(25'000)));
        env.close();

        {
            STAmount const bobPaysXRP{env.balance(bob_, XRP) - reserve(env, 1)};
            XRPAmount const bobPaysFee{reserve(env, 1) - reserve(env, 0)};
            env(pay(bob_, alice_, bobPaysXRP), Fee(bobPaysFee));
            env.close();
        }

        // Test when bob's XRP balance == base reserve.
        BEAST_EXPECT(env.balance(bob_, XRP) == reserve(env, 0));
        BEAST_EXPECT(env.balance(bob_, MPT(btc)) == btc(25'000));
        failedMptPayments();

        // Test when bob has an XRP balance == 0.
        env(noop(bob_), Fee(reserve(env, 0)));
        env.close();

        BEAST_EXPECT(env.balance(bob_, XRP) == XRP(0));
        failedMptPayments();

        // Give bob enough XRP for the fee to clear the lsfDepositAuth flag.
        env(pay(alice_, bob_, drops(env.current()->fees().base)));

        // bob clears the lsfDepositAuth and the next payment succeeds.
        env(fclear(bob_, asfDepositAuth));
        env.close();

        env(pay(alice_, bob_, btc(50'000)));
        env.close();

        env(pay(alice_, bob_, drops(1)), Sendmax(btc(1'000)));
        env.close();
        BEAST_EXPECT(ammCarol.expectBalances(btc(101'000), XRPAmount(100), ammCarol.tokens()));
    }

    void
    testIndividualLock(FeatureBitset features)
    {
        testcase("Individual Lock");

        using namespace test::jtx;
        Env env(*this, features);

        Account const g1{"G1"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1'000), g1, alice, bob);

        MPTTester btc(
            {.env = env,
             .issuer = g1,
             .holders = {alice, bob},
             .flags = tfMPTCanLock | kMptDexFlags});

        env(pay(g1, bob, btc(10)));
        env(pay(g1, alice, btc(205)));
        env.close();

        AMM const ammAlice(env, alice, XRP(500), btc(105));

        env.require(Balance(bob, btc(10)));
        env.require(Balance(alice, btc(100)));

        // Account with MPT unlocked (proving operations normally work)
        // can make Payment
        env(pay(alice, bob, btc(1)));

        // can receive Payment
        env(pay(bob, alice, btc(1)));
        env.close();

        // Lock MPT for bob
        btc.set({.holder = bob, .flags = tfMPTLock});

        {
            env(offer(bob, btc(5), XRP(25)), Ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(XRP(500), btc(105), ammAlice.tokens()));
        }

        {
            // can not sell assets
            env(offer(bob, XRP(1), btc(5)), Ter(tecUNFUNDED_OFFER));

            // different from IOU
            // can not receive Payment when locked
            env(pay(alice, bob, btc(1)), Ter(tecPATH_DRY));

            // can not make Payment when locked
            env(pay(bob, alice, btc(1)), Ter(tecPATH_DRY));

            env.require(Balance(bob, btc(10)));
        }

        {
            // Unlock
            btc.set({.holder = bob, .flags = tfMPTUnlock});
            env(offer(bob, XRP(1), btc(5)));
            env(pay(bob, alice, btc(1)));
            env(pay(alice, bob, btc(1)));
            env.close();
        }
    }

    void
    testGlobalLock(FeatureBitset features)
    {
        testcase("Global Lock");

        using namespace test::jtx;
        Env env(*this, features);

        Account const g1{"G1"};
        Account a1{"A1"};
        Account a2{"A2"};
        Account a3{"A3"};
        Account a4{"A4"};

        env.fund(XRP(12'000), g1);
        env.fund(XRP(1'000), a1);
        env.fund(XRP(20'000), a2, a3, a4);

        MPTTester const eth(
            {.env = env,
             .issuer = g1,
             .holders = {a1, a2, a3, a4},
             .flags = tfMPTCanLock | kMptDexFlags});

        MPTTester btc(
            {.env = env,
             .issuer = g1,
             .holders = {a1, a2, a3, a4},
             .flags = tfMPTCanLock | kMptDexFlags});

        env(pay(g1, a1, eth(1'000)));
        env(pay(g1, a2, eth(100)));
        env(pay(g1, a3, btc(100)));
        env(pay(g1, a4, btc(100)));
        env.close();

        AMM const ammG1(env, g1, XRP(10'000), eth(100));
        env(offer(a1, XRP(10'000), eth(100)), Txflags(tfPassive));
        env(offer(a2, eth(100), XRP(10'000)), Txflags(tfPassive));
        env.close();

        {
            // Account without Global Lock (proving operations normally
            // work)
            // visible offers where taker_pays is unlocked issuer
            auto offers = getAccountOffers(env, a2)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;

            // visible offers where taker_gets is unlocked issuer
            offers = getAccountOffers(env, a1)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;
        }

        {
            // Offers/Payments
            // assets can be bought on the market
            AMM ammA3(env, a3, btc(1), XRP(1));

            // assets can be sold on the market
            // AMM is bidirectional
            env(pay(g1, a2, eth(1)));
            env(pay(a2, g1, eth(1)));
            env(pay(a2, a1, eth(1)));
            env(pay(a1, a2, eth(1)));
            ammA3.withdrawAll(std::nullopt);
        }

        {
            // Account with Global Lock
            //  set Global Lock first
            btc.set({.flags = tfMPTLock});

            // assets can't be bought on the market
            AMM const ammA3(env, a3, btc(1), XRP(1), Ter(tecLOCKED));

            // direct issues can be sent
            env(pay(g1, a2, btc(1)));
            env(pay(a2, g1, btc(1)));
            // locked
            env(pay(a2, a1, btc(1)), Ter(tecPATH_DRY));
            env(pay(a1, a2, btc(1)), Ter(tecPATH_DRY));
        }

        {
            auto offers = getAccountOffers(env, a2)[jss::offers];
            if (!BEAST_EXPECT(checkArraySize(offers, 1u)))
                return;

            offers = getAccountOffers(env, a1)[jss::offers];
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

        Account const g1{"G1"};
        Account a2{"A2"};
        Account a3{"A3"};
        Account a4{"A4"};

        env.fund(XRP(2'000), g1, a3, a4);
        env.fund(XRP(2'000), a2);
        env.close();

        MPTTester btc(
            {.env = env,
             .issuer = g1,
             .holders = {a2, a3, a4},
             .flags = tfMPTCanLock | kMptDexFlags});

        env(pay(g1, a3, btc(2'000)));
        env(pay(g1, a4, btc(2'001)));
        env.close();

        AMM const ammA3(env, a3, XRP(1'000), btc(1'001));

        // removal after successful payment
        //    test: make a payment with partially consuming offer
        env(pay(a2, g1, btc(1)), Paths(MPT(btc)), Sendmax(XRP(1)));
        env.close();

        BEAST_EXPECT(ammA3.expectBalances(XRP(1'001), btc(1'000), ammA3.tokens()));

        //    test: someone else creates an offer providing liquidity
        env(offer(a4, XRP(999), btc(999)));
        env.close();
        // The offer consumes AMM offer
        BEAST_EXPECT(ammA3.expectBalances(XRP(1'000), btc(1'001), ammA3.tokens()));

        //    test: AMM is Locked
        btc.set({.holder = ammA3.ammAccount(), .flags = tfMPTLock});
        auto const info = ammA3.ammRpcInfo();
        BEAST_EXPECT(info[jss::amm][jss::asset2_frozen].asBool());
        env.close();

        //    test: Can make a payment via the new offer
        env(pay(a2, g1, btc(1)), Paths(MPT(btc)), Sendmax(XRP(1)));
        env.close();
        // AMM is not consumed
        BEAST_EXPECT(ammA3.expectBalances(XRP(1'000), btc(1'001), ammA3.tokens()));

        // removal buy successful OfferCreate
        //    test: lock the new offer
        btc.set({.holder = a4, .flags = tfMPTUnlock});
        env.close();

        //    test: can no longer create a crossing offer
        env(offer(a2, btc(999), XRP(999)));
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
        fund(env, gw_, {alice, becky, zelda}, XRP(20'000));

        MPTTester const btc(
            {.env = env,
             .issuer = gw_,
             .holders = {alice, becky, zelda},
             .pay = 20'000'000'000,
             .flags = kMptDexFlags});

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::Secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), Sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), Sig(alie));
        env.close();
        static constexpr int kSignerListOwners{2};
        env.require(Owners(alice, kSignerListOwners + 0));

        Msig const ms{becky, bogie};

        // Multisign all AMM transactions
        AMM ammAlice(
            env,
            alice,
            XRP(10'000),
            btc(10'000),
            false,
            0,
            ammCrtFee(env).drops(),
            std::nullopt,
            std::nullopt,
            ms,
            Ter(tesSUCCESS));
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(10'000), ammAlice.tokens()));

        ammAlice.deposit(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(XRP(11'000), btc(11'000), IOUAmount{11'000'000, 0}));

        ammAlice.withdraw(alice, 1'000'000);
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(10'000), ammAlice.tokens()));

        ammAlice.vote({}, 1'000);
        BEAST_EXPECT(ammAlice.expectTradingFee(1'000));

        env(ammAlice.bid({.account = alice, .bidMin = 100}), ms).close();
        BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, IOUAmount{4'000}));
        // 4000 tokens burnt
        BEAST_EXPECT(ammAlice.expectBalances(XRP(10'000), btc(10'000), IOUAmount{9'996'000, 0}));
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
                env.fund(XRP(30'000), alice_, bob_, carol_, gw_);
                env.close();
                auto const eth = issue1(
                    {.env = env,
                     .token = "ETH",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 1'000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw_,
                     .holders = {alice_, bob_, carol_},
                     .limit = 1'000'000});
                env(pay(gw_, alice_, btc(50000)));
                env(pay(gw_, bob_, btc(50000)));
                env(pay(gw_, carol_, btc(50000)));
                env(pay(gw_, alice_, eth(50000)));
                env(pay(gw_, bob_, eth(50000)));
                env(pay(gw_, carol_, eth(50000)));
                env.close();
                AMM const bobXrpBtc(env, bob_, XRP(1'000), btc(1'000));
                AMM const bobBtcEth(env, bob_, btc(1'000), eth(1'000));

                // payment path: XRP -> XRP/BTC -> BTC/ETH -> ETH/BTC
                env(pay(alice_, carol_, btc(100)),
                    Path(~btc, ~eth, ~btc),
                    Sendmax(XRP(200)),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
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
            fund(env, gw_, {alice_, bob_}, XRP(10'000));

            MPTTester btc(
                {.env = env,
                 .issuer = bob_,
                 .holders = {alice_, gw_},
                 .pay = 100'000'000,
                 .flags = kMptDexFlags});

            MPTTester eth(
                {.env = env,
                 .issuer = bob_,
                 .holders = {alice_, gw_},
                 .pay = 100'000'000,
                 .flags = kMptDexFlags});

            AMM const ammXrpBtc(env, bob_, XRP(100), btc(100'000));
            env(offer(gw_, XRP(100), btc(100'000)), Txflags(tfPassive));

            AMM const ammBtcEth(env, bob_, btc(100'000), eth(100'000));
            env(offer(gw_, btc(100'000), eth(100'000)), Txflags(tfPassive));

            TestPath const p = [&] {
                TestPath result;
                result.pushBack(allPathElements(gw_, MPT(btc)));
                result.pushBack(cpe(eth.issuanceID()));
                return result;
            }();

            PathSet const paths(p);

            env(pay(alice_, alice_, eth(1'000)),
                Json(paths.json()),
                Sendmax(XRP(10)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(temBAD_PATH));
        }

        {
            Env env(*this, features);

            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 100'000,
                 .flags = kMptDexFlags});

            AMM const ammBob(env, bob_, XRP(100), btc(100));

            // payment path: XRP -> XRP/BTC -> BTC/XRP
            env(pay(alice_, carol_, XRP(100)),
                Path(~MPT(btc), ~XRP),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            fund(env, gw_, {alice_, bob_, carol_}, XRP(10'000));

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .pay = 100'000,
                 .flags = kMptDexFlags});

            AMM const ammBob(env, bob_, XRP(100), btc(100));

            // payment path: XRP -> XRP/BTC -> BTC/XRP
            env(pay(alice_, carol_, XRP(100)),
                Path(~MPT(btc), ~XRP),
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

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice_, bob_, carol_, gw_);

            MPTTester const btc(
                {.env = env,
                 .issuer = gw_,
                 .holders = {alice_, bob_, carol_},
                 .flags = kMptDexFlags});

            env(pay(gw_, bob_, btc(100'000'000)));
            env(pay(gw_, alice_, btc(100'000'000)));
            env.close();

            AMM const ammBob(env, bob_, XRP(100), btc(100'000'000));

            // payment path: BTC -> BTC/XRP -> XRP/BTC
            env(pay(alice_, carol_, btc(100'000'000)),
                Sendmax(btc(100'000'000)),
                Path(~XRP, ~MPT(btc)),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
        }

        {
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice_, bob_, carol_, gw_);
                env.close();

                auto const btc = issue1(
                    {.env = env, .token = "BTC", .issuer = gw_, .holders = {alice_, bob_, carol_}});
                auto const eth = issue2(
                    {.env = env, .token = "ETH", .issuer = gw_, .holders = {alice_, bob_, carol_}});
                auto const cny = issue3(
                    {.env = env, .token = "CNY", .issuer = gw_, .holders = {alice_, bob_, carol_}});

                env(pay(gw_, bob_, btc(200)));
                env(pay(gw_, bob_, eth(200)));
                env(pay(gw_, bob_, cny(100)));
                env.close();

                AMM const ammBobXrpBtc(env, bob_, XRP(100), btc(100));
                AMM const ammBobBtcEth(env, bob_, btc(100), eth(100));
                AMM const ammBobEthCny(env, bob_, eth(100), cny(100));

                // payment path: XRP->XRP/BTC->BTC/ETH->BTC/CNY
                env(pay(alice_, carol_, cny(100)),
                    Sendmax(XRP(100)),
                    Path(~btc, ~eth, ~btc, ~cny),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
            };
            testHelper3TokensMix(test);
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
        pathFind06();
    }

    void
    testFlow()
    {
        using namespace jtx;
        FeatureBitset const all{testableAmendments()};

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
        FeatureBitset const all{testableAmendments()};
        testStepLimit(all);
    }

    void
    testDeliverMin()
    {
        using namespace jtx;
        FeatureBitset const all{testableAmendments()};
        testConvertAllOfAnAsset(all);
    }

    void
    testDepositAuth()
    {
        auto const supported{jtx::testableAmendments()};
        testPayment(supported);
        testPayMPT();
    }

    void
    testLock()
    {
        using namespace test::jtx;
        auto const sa = testableAmendments();
        testIndividualLock(sa);
        testGlobalLock(sa);
        testOffersWhenLocked(sa);
    }

    void
    testMultisign()
    {
        using namespace jtx;
        auto const all = testableAmendments();

        testTxMultisign(all);
    }

    void
    testPayStrand()
    {
        using namespace jtx;
        auto const all = testableAmendments();

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

BEAST_DEFINE_TESTSUITE_PRIO(AMMExtendedMPT, app, xrpl, 1);

}  // namespace xrpl::test
