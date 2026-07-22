#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl::test {
class AMMClawbackMPT_test : public beast::unit_test::Suite
{
    void
    testInvalidRequest(FeatureBitset features)
    {
        testcase("test invalid request");
        using namespace jtx;

        for (auto const& feature : {features, features - featureSingleAssetVault})
        {
            Env env(*this, feature);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            auto const usd = gw["USD"];
            env.trust(usd(10000), alice);
            env(pay(gw, alice, usd(100)));
            env.close();

            AMM amm(env, gw, btc(100), usd(100));

            // holder does not exist
            env(amm::ammClawback(gw, Account("unknown"), usd, btc, std::nullopt),
                Ter(terNO_ACCOUNT));

            // can not clawback from self.
            env(amm::ammClawback(gw, gw, usd, btc, std::nullopt), Ter(temMALFORMED));

            // provided Asset does not match issuer gw
            {
                env(amm::ammClawback(
                        gw, alice, Issue{gw["USD"].currency, alice.id()}, btc, std::nullopt),
                    Ter(temMALFORMED));
                env(amm::ammClawback(gw, alice, MPTIssue{makeMptID(1, alice)}, usd, std::nullopt),
                    Ter(temMALFORMED));
            }

            // Amount does not match asset
            {
                env(amm::ammClawback(
                        gw, alice, usd, btc, STAmount{Issue{gw["USD"].currency, alice.id()}, 1}),
                    Ter(temBAD_AMOUNT));
                env(amm::ammClawback(
                        gw, alice, btc, usd, STAmount{MPTIssue{makeMptID(1, alice)}, 10}),
                    Ter(temBAD_AMOUNT));
            }

            // Amount is not greater than 0
            {
                env(amm::ammClawback(gw, alice, btc, usd, btc(-1)), Ter(temBAD_AMOUNT));
                env(amm::ammClawback(gw, alice, btc, usd, btc(0)), Ter(temBAD_AMOUNT));
            }

            // clawback from account not holding lptoken
            env(amm::ammClawback(gw, bob, btc, usd, btc(1000)), Ter(tecAMM_BALANCE));

            // can not perform regular claw from amm pool
            {
                Issue const ammUsd(usd.currency, amm.ammAccount());
                auto amount = amountFromString(ammUsd, "10");
                auto const err =
                    feature[featureSingleAssetVault] ? tecPSEUDO_ACCOUNT : tecAMM_ACCOUNT;
                env(claw(gw, amount), Ter(err));
            }

            // AMM does not exist
            {
                // withdraw all tokens will delete the AMM
                amm.withdrawAll(gw);
                BEAST_EXPECT(!amm.ammExists());
                env.close();
                env(amm::ammClawback(gw, alice, usd, btc, std::nullopt), Ter(terNO_AMM));
            }
        }

        // tfMPTCanClawback is not enabled
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT const btc =
                MPTTester({.env = env, .issuer = gw, .holders = {alice}, .pay = 40'000});

            auto const usd = gw["USD"];
            env.trust(usd(10000), alice);
            env(pay(gw, alice, usd(10000)));
            env.close();

            AMM amm(env, gw, btc(100), usd(100));
            env.close();
            amm.deposit(alice, 1'000);
            env.close();

            // can not clawback when tfMPTCanClawback is not enabled
            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt), Ter(tecNO_PERMISSION));
        }

        // can not claw with tfClawTwoAssets if the assets are not issued by the
        // same issuer
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(10000), alice);
            env(pay(gw, alice, usd(10000)));
            env.close();

            // todo: check tfMPTCanTransfer in xrpl.org
            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM const amm(env, alice, btc(100), usd(100));
            env.close();

            {
                // Return temINVALID_FLAG because the issuer set
                // tfClawTwoAssets, but the issuer only issues USD in the pool.
                // The issuer is not allowed to set tfClawTwoAssets flag if he
                // did not issue both assets in the pool.
                env(amm::ammClawback(gw, alice, usd, btc, std::nullopt),
                    Txflags(tfClawTwoAssets),
                    Ter(temINVALID_FLAG));
            }
        }

        // Test if the issuer did not set asfAllowTrustLineClawback, but the MPT
        // is set tfMPTCanClawback, the issuer can claw MPT.
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM const amm(env, alice, btc(100), XRP(100));
            env.close();

            // If asfAllowTrustLineClawback is not set, the issuer can
            // still claw MPT because the MPT's tfMPTCanClawback is set.
            env(amm::ammClawback(gw, alice, btc, XRP, std::nullopt));
        }
    }

    void
    testFeatureDisabled(FeatureBitset features)
    {
        testcase("test feature disabled.");
        using namespace jtx;
        Env env{*this, features};
        Account const gw("gateway"), alice("alice");
        env.fund(XRP(30'000), gw, alice);
        env.close();
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();

        MPT const btc = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .pay = 10'000,
             .flags = tfMPTCanClawback | kMptDexFlags});

        AMM const amm(env, alice, XRP(1'000), btc(1'000));

        // disable featureAMMClawback
        env.disableFeature(featureAMMClawback);
        env(amm::ammClawback(gw, alice, btc, XRP, std::nullopt), Ter(temDISABLED));

        // enable featureAMMClawback and disable featureMPTokensV2
        env.enableFeature(featureAMMClawback);
        env.disableFeature(featureMPTokensV2);
        env(amm::ammClawback(gw, alice, btc, XRP, btc(100)), Ter(temDISABLED));

        // enable featureMPTokensV2
        env.enableFeature(featureMPTokensV2);
        env(amm::ammClawback(gw, alice, btc, XRP, btc(200)));
    }

    void
    testAMMClawbackAmount(FeatureBitset features)
    {
        testcase("test AMMClawback specific amount");
        using namespace jtx;

        // AMMClawback from MPT/IOU issued by different issuers
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(50000)));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM const amm(env, alice, btc(1000000000), usd(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(1'000'000000), usd(2000), IOUAmount{1414'213'562373095, -9}));

            // can not set tfClawTwoAssets because the assets are not issued by
            // the same issuer.
            env(amm::ammClawback(gw2, alice, btc, usd, btc(1000)),
                Txflags(tfClawTwoAssets),
                Ter(temINVALID_FLAG));

            auto aliceUSD = env.balance(alice, usd);
            auto aliceBTC = env.balance(alice, btc);
            // gw clawback 1000 USD from alice
            env(amm::ammClawback(gw, alice, usd, btc, usd(1000)));
            env.close();

            BEAST_EXPECT(
                amm.expectBalances(btc(500'000000), usd(1000), IOUAmount{707'106'7811865475, -10}));
            // USD is clawed back,
            env.require(Balance(alice, aliceUSD));
            // a proportional amount of BTC is returned to alice
            env.require(Balance(alice, aliceBTC + btc(500'000000)));
            aliceBTC = env.balance(alice, btc);

            // gw2 clawback 250'000000 BTC from alice
            env(amm::ammClawback(gw2, alice, btc, usd, btc(250'000000)));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(250'000000), usd(500), IOUAmount{353'553'3905932737, -10}));
            env.require(Balance(alice, aliceUSD + usd(500)));
            env.require(Balance(alice, aliceBTC));
            aliceUSD = env.balance(alice, usd);

            // gw2 clawback 500'000000 BTC which exceeds the balance,
            // this will clawback all and the amm will be deleted.
            env(amm::ammClawback(gw2, alice, btc, usd, btc(500'000000)));
            env.close();
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceUSD + usd(500)));
            env.require(Balance(alice, aliceBTC));
        }

        // AMMClawback from MPT/XRP pool
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(1000000000), XRP(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(1'000'000000), XRP(2000), IOUAmount{1'414'213'562'373095, -6}));

            amm.deposit(bob, btc(2'000'000000), XRP(4000));
            BEAST_EXPECT(amm.expectBalances(
                btc(3'000'000000), XRP(6000), IOUAmount{4'242'640'687'119285, -6}));

            auto aliceXRP = env.balance(alice, XRP);
            auto aliceBTC = env.balance(alice, btc);
            auto bobXRP = env.balance(bob, XRP);
            auto bobBTC = env.balance(bob, btc);

            // can not claw XRP
            env(amm::ammClawback(gw, alice, XRP, btc, XRP(1000)), Ter(temMALFORMED));
            // can not set tfClawTwoAssets
            env(amm::ammClawback(gw, alice, btc, XRP, btc(1000)),
                Txflags(tfClawTwoAssets),
                Ter(temINVALID_FLAG));

            // gw clawback 500 BTC from alice
            env(amm::ammClawback(gw, alice, btc, XRP, btc(500)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(2'999'999501),
                STAmount{XRP, UINT64_C(5'999'999001)},
                IOUAmount{4'242'639'980'012504, -6}));
            env.require(Balance(alice, aliceXRP + drops(999)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(bob, bobXRP));
            env.require(Balance(bob, bobBTC));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1'414'212'855'266314, -6}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{2'828'427'124'74619, -5}));
            aliceXRP = env.balance(alice, XRP);

            // gw clawback 1000'000000 BTC from bob
            env(amm::ammClawback(gw, bob, btc, XRP, btc(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(1'999'999501),
                STAmount{XRP, UINT64_C(3'999'999002)},
                IOUAmount{2828426418'110813, -6}));
            env.require(Balance(alice, aliceXRP));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(bob, bobXRP + XRPAmount(1999999999)));
            env.require(Balance(bob, bobBTC));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1'414'212'855'266314, -6}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{1'414'213'562'844499, -6}));
            bobXRP = env.balance(bob, XRP);

            // gw clawback 1000'000000 BTC from alice, which exceeds her balance
            // will clawback all her balance
            env(amm::ammClawback(gw, alice, btc, XRP, btc(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(1'000'000001), XRPAmount(2'000'000002), IOUAmount{1'414'213'562'844499, -6}));
            env.require(Balance(alice, aliceXRP + STAmount{XRP, UINT64_C(1'999'999000)}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(bob, bobXRP));
            env.require(Balance(bob, bobBTC));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{1'414'213'562'844499, -6}));
            aliceXRP = env.balance(alice, XRP);

            // gw clawback from bob, which exceeds his balance
            env(amm::ammClawback(gw, bob, btc, XRP, btc(2'000'000000)));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceXRP));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(bob, bobXRP + XRPAmount(2000000002)));
            env.require(Balance(bob, bobBTC));
        }

        // AMMClawback from MPT/MPT pool, different issuers
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, gw2, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            MPT const eth = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(2'000'000000), eth(3'000'000000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(2'000'000000), eth(3'000'000000), IOUAmount{2'449'489'742'783178, -6}));

            amm.deposit(bob, btc(4'000'000000), eth(6'000'000000));
            BEAST_EXPECT(amm.expectBalances(
                btc(6'000'000000), eth(9'000'000000), IOUAmount{7'348'469'228'349534, -6}));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceETH = env.balance(alice, eth);
            auto bobBTC = env.balance(bob, btc);
            auto bobETH = env.balance(bob, eth);

            // gw clawback BTC from alice
            env(amm::ammClawback(gw, alice, btc, eth, btc(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(5'000'000000), eth(7'500'000000), IOUAmount{6'123'724'356'957944, -6}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH + eth(1'500'000000)));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{4'898'979'485'566356, -6}));
            aliceETH = env.balance(alice, eth);

            // gw2 clawback ETH from bob
            env(amm::ammClawback(gw2, bob, eth, btc, eth(3'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(3'000'000000), eth(4'500'000000), IOUAmount{3'674'234'614'174766, -6}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC + btc(2'000'000000)));
            env.require(Balance(bob, bobETH));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{2'449'489'742'783178, -6}));
            bobBTC = env.balance(bob, btc);

            // gw2 clawback ETH from alice, which exceeds her balance
            env(amm::ammClawback(gw2, alice, eth, btc, eth(4'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(2'000'000001), eth(3'000'000001), IOUAmount{2'449'489'742'783178, -6}));
            env.require(Balance(alice, aliceBTC + btc(999'999999)));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH));
            aliceBTC = env.balance(alice, btc);

            // gw clawback BTC from bob, which exceeds his balance
            env(amm::ammClawback(gw, bob, btc, eth, btc(3'000'000000)));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH + eth(3'000'000001)));
        }
    }

    void
    testAMMClawbackAll(FeatureBitset features)
    {
        testcase("test AMMClawback all");
        using namespace jtx;

        // AMMClawback all from MPT/IOU issued by different issuers
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, gw2, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(50000)));
            env.trust(usd(200000), bob);
            env(pay(gw, bob, usd(60000)));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(2000000000), usd(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(btc(2'000'000000), usd(2000), IOUAmount(2000000)));

            // gw clawback all BTC from alice
            amm.deposit(bob, btc(1'000'000000), usd(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(btc(3'000'000000), usd(3000), IOUAmount(3000000)));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceUSD = env.balance(alice, usd);
            auto bobBTC = env.balance(bob, btc);
            auto bobUSD = env.balance(bob, usd);

            // gw2 clawback all BTC from alice
            env(amm::ammClawback(gw2, alice, btc, usd, std::nullopt));
            env.close();
            BEAST_EXPECT(amm.expectBalances(btc(1'000'000000), usd(1000), IOUAmount(1000000)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + usd(2000)));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobUSD));
            aliceUSD = env.balance(alice, usd);

            // gw clawback all USD from bob
            env(amm::ammClawback(gw, bob, usd, btc, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(bob, bobBTC + btc(1'000'000000)));
            env.require(Balance(bob, bobUSD));
        }

        // AMMClawback all from MPT/XRP pool
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(5000), XRP(10'000));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(5'000), XRP(10'000), IOUAmount{7'071'067'811865475, -9}));

            amm.deposit(bob, btc(10'000), XRP(20'000));
            BEAST_EXPECT(
                amm.expectBalances(btc(15'000), XRP(30'000), IOUAmount{21'213'203'43559642, -8}));

            auto aliceXRP = env.balance(alice, XRP);
            auto aliceBTC = env.balance(alice, btc);
            auto bobXRP = env.balance(bob, XRP);
            auto bobBTC = env.balance(bob, btc);

            // gw clawback all BTC from alice
            env(amm::ammClawback(gw, alice, btc, XRP, std::nullopt));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(10'000), XRP(20'000), IOUAmount{14'142'135'62373094, -8}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceXRP + XRP(10'000)));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobXRP));
            aliceXRP = env.balance(alice, XRP);

            // gw clawback all BTC from bob
            env(amm::ammClawback(gw, bob, btc, XRP, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceXRP));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobXRP + XRP(20'000)));
        }

        // AMMClawback all from MPT/MPT pool, different issuers
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, gw2, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            MPT const eth = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(20'000), eth(50'000));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(20'000), eth(50'000), IOUAmount{31'622'77660168379, -11}));

            amm.deposit(bob, btc(40'000), eth(100'000));
            BEAST_EXPECT(
                amm.expectBalances(btc(60'000), eth(150'000), IOUAmount{94'868'32980505137, -11}));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceETH = env.balance(alice, eth);
            auto bobBTC = env.balance(bob, btc);
            auto bobETH = env.balance(bob, eth);

            // gw clawback all BTC from bob
            env(amm::ammClawback(gw, bob, btc, eth, std::nullopt));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(20'000), eth(50'000), IOUAmount{31'622'77660168379, -11}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH + eth(100'000)));
            bobETH = env.balance(bob, eth);

            // gw2 clawback all ETH from alice
            env(amm::ammClawback(gw2, alice, eth, btc, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceBTC + btc(20'000)));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH));
        }
    }

    void
    testAMMClawbackAmountSameIssuer(FeatureBitset features)
    {
        testcase("test AMMClawback specific amount, assets have the same issuer");
        using namespace jtx;

        // AMMClawback from MPT/IOU issued by the same issuer
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(50000)));
            env.trust(usd(100000), bob);
            env(pay(gw, bob, usd(40000)));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(1'000'000000), usd(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(1'000'000000), usd(2000), IOUAmount{1414'213'562373095, -9}));

            amm.deposit(bob, btc(500'000000), usd(1000));
            BEAST_EXPECT(amm.expectBalances(
                btc(1'500'000000),
                STAmount{usd, UINT64_C(2'999'999999999999), -12},
                IOUAmount{2'121'320'343559642, -9}));

            auto aliceUSD = env.balance(alice, usd);
            auto aliceBTC = env.balance(alice, btc);
            auto bobUSD = env.balance(bob, usd);
            auto bobBTC = env.balance(bob, btc);

            // gw clawback 500 USD from alice.
            env(amm::ammClawback(gw, alice, usd, btc, usd(500)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(1250'000001), usd(2500), IOUAmount{1'767'766'952966369, -9}));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(alice, aliceBTC + btc(249'999999)));
            env.require(Balance(bob, bobUSD));
            env.require(Balance(bob, bobBTC));
            aliceBTC = env.balance(alice, btc);
            // gw clawback 250'000000 BTC and 500 USD from bob
            // with tfClawTwoAssets
            env(amm::ammClawback(gw, bob, btc, usd, btc(250'000000)), Txflags(tfClawTwoAssets));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(1'000'000002),
                STAmount{usd, UINT64_C(2000'0000004), -7},
                IOUAmount{1'414'213'562655938, -9}));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(bob, bobUSD));
            env.require(Balance(bob, bobBTC));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1'060'660'171779822, -9}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{353'553'390876116, -9}));

            // gw clawback USD from alice exceeding her balance
            env(amm::ammClawback(gw, alice, usd, btc, usd(5'000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(250'000001),
                STAmount{usd, UINT64_C(500'0000004), -7},
                IOUAmount{353'553'390876116, -9}));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(alice, aliceBTC + btc(750'000001)));
            env.require(Balance(bob, bobUSD));
            env.require(Balance(bob, bobBTC));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{353'553'390876116, -9}));
            aliceBTC = env.balance(alice, btc);

            // gw clawback BTC from bob which exceeds his balance with
            // tfClawTwoAssets
            env(amm::ammClawback(gw, bob, btc, usd, btc(300'000000)), Txflags(tfClawTwoAssets));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(alice, aliceBTC));
            // USD is also clawed back from bob because of tfClawTwoAssets,
            // bob's USD balance will not change
            env.require(Balance(bob, bobUSD));
            env.require(Balance(bob, bobBTC));
        }

        // AMMClawback from MPT/MPT issued by the same issuer
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            MPT const eth = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(2'000'000000), eth(3'000'000000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(2'000'000000), eth(3'000'000000), IOUAmount{2'449'489'742'783178, -6}));

            amm.deposit(bob, btc(4'000'000000), eth(6'000'000000));
            BEAST_EXPECT(amm.expectBalances(
                btc(6'000'000000), eth(9'000'000000), IOUAmount{7'348'469'228'349534, -6}));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceETH = env.balance(alice, eth);
            auto bobBTC = env.balance(bob, btc);
            auto bobETH = env.balance(bob, eth);

            // gw clawback BTC from alice
            env(amm::ammClawback(gw, alice, btc, eth, btc(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(5'000'000000), eth(7'500'000000), IOUAmount{6'123'724'356'957944, -6}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH + eth(1'500'000000)));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{4'898'979'485'566356, -6}));
            aliceETH = env.balance(alice, eth);

            // gw clawback ETH and BTC from bob with tfClawTwoAssets
            env(amm::ammClawback(gw, bob, eth, btc, eth(3'000'000000)), Txflags(tfClawTwoAssets));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(3'000'000000), eth(4'500'000000), IOUAmount{3'674'234'614'174766, -6}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{2'449'489'742'783178, -6}));

            // gw clawback BTC from alice, which exceeds her balance with
            // tfClawTwoAssets
            env(amm::ammClawback(gw, alice, btc, eth, btc(3'000'000000)), Txflags(tfClawTwoAssets));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                btc(2'000'000001), eth(3'000'000001), IOUAmount{2'449'489'742'783178, -6}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount{2'449'489'742'783178, -6}));

            // gw clawback ETH from bob, which is the same as his balance
            env(amm::ammClawback(gw, bob, eth, btc, eth(3'000'000001)));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC + btc(2'000'000001)));
            env.require(Balance(bob, bobETH));
        }
    }

    void
    testAMMClawbackAllSameIssuer(FeatureBitset features)
    {
        testcase("test AMMClawback all, assets have the same issuer");
        using namespace jtx;

        // AMMClawback all from MPT/IOU issued by the same issuer
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(50000)));
            env.trust(usd(200000), bob);
            env(pay(gw, bob, usd(60000)));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(2'000'000000), usd(8'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(btc(2'000'000000), usd(8'000), IOUAmount(4'000'000)));

            amm.deposit(bob, btc(1'000'000000), usd(4'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(btc(3'000'000000), usd(12'000), IOUAmount(6'000'000)));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceUSD = env.balance(alice, usd);
            auto bobBTC = env.balance(bob, btc);
            auto bobUSD = env.balance(bob, usd);

            // gw clawback all BTC and USD from alice
            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt), Txflags(tfClawTwoAssets));
            env.close();

            BEAST_EXPECT(amm.expectBalances(btc(1'000'000000), usd(4'000), IOUAmount(2'000'000)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(2'000'000)));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobUSD));

            // gw clawback all USD from bob
            env(amm::ammClawback(gw, bob, usd, btc, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(bob, bobBTC + btc(1'000'000000)));
            env.require(Balance(bob, bobUSD));
        }

        // AMMClawback all from MPT/MPT issued by the same issuer
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            MPT const eth = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, btc(20'000), eth(10'000));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(20'000), eth(10'000), IOUAmount{14'142'13562373095, -11}));

            amm.deposit(bob, btc(40'000), eth(20'000));
            BEAST_EXPECT(
                amm.expectBalances(btc(60'000), eth(30'000), IOUAmount{42'426'40687119285, -11}));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceETH = env.balance(alice, eth);
            auto bobBTC = env.balance(bob, btc);
            auto bobETH = env.balance(bob, eth);

            // gw clawback all ETH from bob
            env(amm::ammClawback(gw, bob, eth, btc, std::nullopt));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(20'000), eth(10'000), IOUAmount{14'142'13562373095, -11}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC + btc(40'000)));
            env.require(Balance(bob, bobETH));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{14'142'13562373095, -11}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
            bobBTC = env.balance(bob, btc);

            // gw clawback all ETH and BTC from alice with tfClawTwoAssets
            env(amm::ammClawback(gw, alice, eth, btc, std::nullopt), Txflags(tfClawTwoAssets));
            env.close();

            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(bob, bobBTC));
            env.require(Balance(bob, bobETH));
        }
    }

    void
    testAMMClawbackIssuesEachOther(FeatureBitset features)
    {
        testcase("test AMMClawback when issuing token for each other");
        using namespace jtx;

        // AMMClawback from MPT/IOU issued by each other
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            env.fund(XRP(1000000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), gw2);
            env(pay(gw, gw2, usd(5000)));
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(5000)));

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, gw},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, gw, usd(1000), btc(2000));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(usd(1000), btc(2000), IOUAmount{1414'213562373095, -12}));

            amm.deposit(gw2, usd(2000), btc(4000));
            BEAST_EXPECT(
                amm.expectBalances(usd(3000), btc(6000), IOUAmount{4242'640687119285, -12}));

            amm.deposit(alice, usd(3000), btc(6000));
            BEAST_EXPECT(
                amm.expectBalances(usd(6000), btc(12000), IOUAmount{8485'281374238570, -12}));

            BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{1414'213562373095, -12}));
            BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{2828'427124746190, -12}));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{4242'640687119285, -12}));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceUSD = env.balance(alice, usd);
            auto gwBTC = env.balance(gw, btc);
            auto gw2USD = env.balance(gw2, usd);

            // gw claws back 1000 USD from gw2.
            env(amm::ammClawback(gw, gw2, usd, btc, usd(1000)));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(usd(5000), btc(10000), IOUAmount{7071'067811865474, -12}));
            BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{1414'213562373095, -12}));
            BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{1414'213562373094, -12}));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{4242'640687119285, -12}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(gw, gwBTC));
            env.require(Balance(gw2, gw2USD));

            // gw2 claws back 1000 BTC from gw.
            env(amm::ammClawback(gw2, gw, btc, usd, btc(1000)), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(usd(4500), btc(9001), IOUAmount{6363'961030678927, -12}));

            BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{707'1067811865480, -13}));
            BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{1414'213562373094, -12}));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{4242'640687119285, -12}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
            env.require(Balance(gw, gwBTC));
            env.require(Balance(gw2, gw2USD));

            // gw2 claws back 4000 BTC from alice
            env(amm::ammClawback(gw2, alice, btc, usd, btc(4000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                STAmount{usd, UINT64_C(2500'222197533607), -12},
                btc(5001),
                IOUAmount{3535'84814069829, -11}));

            BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount{707'1067811865480, -13}));
            BEAST_EXPECT(amm.expectLPTokens(gw2, IOUAmount{1414'213562373094, -12}));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount{1414'527797138648, -12}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + STAmount{usd, UINT64_C(1999'777802466393), -12}));
            env.require(Balance(gw, gwBTC));
            env.require(Balance(gw2, gw2USD));
        }

        // AMMClawback from MPT/MPT issued by each other
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {gw2, alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            MPT const eth = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {gw, alice},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, gw, btc(10'000), eth(50'000));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(10'000), eth(50'000), IOUAmount{22'360'67977499789, -11}));

            amm.deposit(gw2, btc(20'000), eth(100'000));
            BEAST_EXPECT(
                amm.expectBalances(btc(30'000), eth(150'000), IOUAmount{67'082'03932499367, -11}));

            amm.deposit(alice, btc(40'000), eth(200'000));
            BEAST_EXPECT(
                amm.expectBalances(btc(70'000), eth(350'000), IOUAmount{156'524'7584249852, -10}));

            auto aliceBTC = env.balance(alice, btc);
            auto aliceETH = env.balance(alice, eth);
            auto gw2BTC = env.balance(gw2, btc);
            auto gwETH = env.balance(gw, eth);

            // gw claws back 1000 BTC from gw2.
            env(amm::ammClawback(gw, gw2, btc, eth, btc(1000)));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(69'001), eth(345'001), IOUAmount{154'288'6904474855, -10}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(gw, gwETH));
            env.require(Balance(gw2, gw2BTC));

            // gw2 claws back all ETH from gw
            env(amm::ammClawback(gw2, gw, eth, btc, std::nullopt));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(59'001), eth(295'001), IOUAmount{131'928'0106724876, -10}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH));
            env.require(Balance(gw, gwETH));
            env.require(Balance(gw2, gw2BTC));

            // gw claws back all BTC from alice
            env(amm::ammClawback(gw, alice, btc, eth, std::nullopt));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(btc(19'001), eth(95'001), IOUAmount{42'485'29157249607, -11}));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceETH + eth(200'000)));
            env.require(Balance(gw, gwETH));
            env.require(Balance(gw2, gw2BTC));
        }
    }

    void
    testAssetFrozenOrLocked(FeatureBitset features)
    {
        testcase("test AMMClawback when asset is frozen or locked");
        using namespace jtx;

        // test AMMClawback when MPT globally locked or IOU globally frozen
        {
            Env env{*this, features};
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(1'000'000), gw, alice);

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            auto const usd = gw["USD"];
            env.trust(usd(1'000'000), alice);
            env(pay(gw, alice, usd(500'000)));

            MPTTester btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanClawback | tfMPTCanLock | kMptDexFlags});
            AMM const ammAlice(env, alice, usd(10'000), btc(10'000));
            BEAST_EXPECT(ammAlice.expectBalances(usd(10'000), btc(10'000), IOUAmount(10'000)));
            env.close();

            auto aliceBTC = env.balance(alice, MPT(btc));
            auto aliceUSD = env.balance(alice, usd);

            // globally locked and claw back 1000 BTC.
            // this should be successful
            btc.set({.flags = tfMPTLock});
            env(amm::ammClawback(gw, alice, MPT(btc), usd, btc(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(usd(9'000), btc(9'000), IOUAmount(9'000)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + usd(1'000)));
            aliceUSD = env.balance(alice, usd);

            // unlock and claw back 2000 BTC
            btc.set({.flags = tfMPTUnlock});
            env(amm::ammClawback(gw, alice, MPT(btc), usd, btc(2'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(usd, UINT64_C(7'000'000000000001), -12), btc(7'001), IOUAmount(7'000)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + usd(2'000)));
            aliceUSD = env.balance(alice, usd);

            // globally freeze trustline and claw back 1000 USD.
            // this should be successful
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, usd, MPT(btc), usd(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(usd, UINT64_C(6000'000000000002), -12),
                btc(6'001),
                IOUAmount(6'000'000000000001, -12)));
            env.require(Balance(alice, aliceBTC + btc(1'000)));
            env.require(Balance(alice, aliceUSD));
            aliceBTC = env.balance(alice, MPT(btc));

            // globally unfreeze trustline and claw back 2000 USD
            // and 2000 BTC with tfClawTwoAssets
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, usd, MPT(btc), usd(2'000)), Txflags(tfClawTwoAssets));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(usd, UINT64_C(4'000'000000000002), -12),
                btc(4'001),
                IOUAmount(4'000'000000000001, -12)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
        }

        // test AMMClawback when MPT individually locked or IOU individually
        // frozen
        {
            Env env{*this, features};
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(1'000'000), gw, alice);

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            auto const usd = gw["USD"];
            env.trust(usd(1'000'000), alice);
            env(pay(gw, alice, usd(500'000)));

            MPTTester btc(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanClawback | tfMPTCanLock | kMptDexFlags});
            AMM const ammAlice(env, alice, usd(10'000), btc(10'000));
            BEAST_EXPECT(ammAlice.expectBalances(usd(10'000), btc(10'000), IOUAmount(10'000)));
            env.close();

            auto aliceBTC = env.balance(alice, MPT(btc));
            auto aliceUSD = env.balance(alice, usd);

            // individually locked and claw back 2000 BTC from alice
            btc.set({.holder = alice, .flags = tfMPTLock});
            env(amm::ammClawback(gw, alice, MPT(btc), usd, btc(2'000)));
            BEAST_EXPECT(ammAlice.expectBalances(usd(8'000), btc(8'000), IOUAmount(8'000)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + usd(2'000)));
            aliceUSD = env.balance(alice, usd);

            // individually freeze trustline and claw back 1000 USD from alice
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, usd, MPT(btc), usd(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(usd(7'000), btc(7'000), IOUAmount(7'000)));
            env.require(Balance(alice, aliceBTC + btc(1'000)));
            env.require(Balance(alice, aliceUSD));
            aliceBTC = env.balance(alice, MPT(btc));

            // unlock MPT and claw back 3000 BTC from alice
            btc.set({.holder = alice, .flags = tfMPTUnlock});
            env(amm::ammClawback(gw, alice, MPT(btc), usd, btc(3'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount{usd, UINT64_C(4000'000000000001), -12}, btc(4'001), IOUAmount(4'000)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + usd(3'000)));
            aliceUSD = env.balance(alice, usd);

            // unlock trustline and claw back 1000 USD from alice
            env(trust(gw, alice["USD"](0), tfClearFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, usd, MPT(btc), usd(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(usd, UINT64_C(3'000'000000000002), -12),
                btc(3'001),
                IOUAmount(3000'000000000001, -12)));
            env.require(Balance(alice, aliceBTC + btc(1'000)));
            env.require(Balance(alice, aliceUSD));
        }
    }

    void
    testSingleDepositAndClawback(FeatureBitset features)
    {
        testcase("test single depoit and clawback");
        using namespace jtx;

        // MPT/XRP
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(1000000000), gw, alice);
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            // gw creates AMM pool of BTC/XRP.
            AMM amm(env, gw, XRP(100), btc(400), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(amm.expectBalances(XRP(100), btc(400), IOUAmount(200000)));
            amm.deposit(alice, btc(400));
            env.close();
            BEAST_EXPECT(amm.expectBalances(XRP(100), btc(800), IOUAmount{282842'712474619, -9}));

            auto aliceBTC = env.balance(alice, MPT(btc));
            auto aliceXRP = env.balance(alice, XRP);

            // gw clawback 100 BTC from alice
            env(amm::ammClawback(gw, alice, MPT(btc), XRP, btc(100)));
            BEAST_EXPECT(amm.expectBalances(
                XRPAmount(87500001), btc(701), IOUAmount{247'487'3734152917, -10}));

            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceXRP + XRPAmount(12'499999)));
        }

        // MPT/IOU
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(1000000000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(gw, asfAllowTrustLineClawback));

            // gw issues 1000 USD to Alice.
            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(1000)));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            // gw creates AMM pool of BTC/USD.
            AMM amm(env, gw, usd(100), btc(400), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(amm.expectBalances(usd(100), btc(400), IOUAmount(200)));
            amm.deposit(alice, btc(400));
            env.close();
            BEAST_EXPECT(amm.expectBalances(usd(100), btc(800), IOUAmount{282'842712474619, -12}));

            auto aliceBTC = env.balance(alice, MPT(btc));
            auto aliceUSD = env.balance(alice, usd);

            // gw clawback 100 BTC from alice
            env(amm::ammClawback(gw, alice, MPT(btc), usd, btc(100)));
            BEAST_EXPECT(amm.expectBalances(
                STAmount{usd, UINT64_C(87'50000000000003), -14},
                btc(701),
                IOUAmount{247'4873734152917, -13}));

            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + usd(12.5)));
            aliceUSD = env.balance(alice, usd);

            // gw clawback 30 USD from alice with tfClawTwoAssets, which exceeds
            // her balance
            env(amm::ammClawback(gw, alice, usd, MPT(btc), usd(30)), Txflags(tfClawTwoAssets));
            BEAST_EXPECT(amm.expectBalances(
                STAmount{usd, UINT64_C(70'71067811865476), -14}, btc(567), IOUAmount(200)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount(200)));
        }

        // MPT/MPT
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(1000000000), gw, alice);
            env.close();

            MPT const usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            // gw creates AMM pool of BTC/USD.
            AMM amm(env, gw, usd(100), btc(400), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(amm.expectBalances(usd(100), btc(400), IOUAmount(200)));
            amm.deposit(alice, btc(400));
            env.close();
            BEAST_EXPECT(amm.expectBalances(usd(100), btc(800), IOUAmount{282'842712474619, -12}));

            auto aliceBTC = env.balance(alice, MPT(btc));
            auto aliceUSD = env.balance(alice, usd);

            // gw clawback 100 BTC from alice
            env(amm::ammClawback(gw, alice, MPT(btc), usd, btc(100)));
            BEAST_EXPECT(amm.expectBalances(usd(88), btc(701), IOUAmount{247'4873734152917, -13}));

            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD + usd(12)));
            aliceUSD = env.balance(alice, usd);

            // gw clawback 30 USD from alice with tfClawTwoAssets, which exceeds
            // her balance
            env(amm::ammClawback(gw, alice, usd, MPT(btc), usd(30)), Txflags(tfClawTwoAssets));
            BEAST_EXPECT(amm.expectBalances(usd(72), btc(567), IOUAmount(200)));
            env.require(Balance(alice, aliceBTC));
            env.require(Balance(alice, aliceUSD));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount(200)));
        }
    }

    void
    testLastHolderLPTokenBalance(FeatureBitset features)
    {
        testcase(
            "test last holder's lptoken balance not equal to AMM's lptoken "
            "balance before clawback");
        using namespace jtx;
        std::string logs;

        // MPT/IOU
        {
            Env env(*this, features, std::make_unique<CaptureLogs>(&logs));
            Account const gw{"gateway"}, alice{"alice"}, bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(50000)));
            env.trust(usd(100000), bob);
            env(pay(gw, bob, usd(40000)));
            env.close();

            MPT const eur = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, usd(2), eur(1));
            amm.deposit(alice, IOUAmount{1'576123487565916, -15});
            amm.deposit(bob, IOUAmount{1'000});
            amm.withdraw(alice, IOUAmount{1'576123487565916, -15});
            amm.withdrawAll(bob);

            auto const lpToken =
                getAccountLines(env, alice, amm.lptIssue())[jss::lines][0u][jss::balance]
                    .asString();
            auto const lpTokenBalance =
                amm.ammRpcInfo()[jss::amm][jss::lp_token][jss::value].asString();
            if (features[featureSingleAssetVault] || features[featureLendingProtocol])
            {
                BEAST_EXPECT(lpToken == "1.414213562374011" && lpTokenBalance == "1.4142135623741");
            }
            else
            {
                BEAST_EXPECT(lpToken == "1.414213562374011" && lpTokenBalance == "1.414213562374");
            }

            auto res = isOnlyLiquidityProvider(*env.current(), amm.lptIssue(), alice);
            BEAST_EXPECT(res && res.value());

            if (features[fixAMMv1_3] && features[fixAMMClawbackRounding])
            {
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt));
                BEAST_EXPECT(!amm.ammExists());
            }
            else if (
                features[fixAMMv1_3] &&
                (features[featureSingleAssetVault] || features[featureLendingProtocol]))
            {
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt));
                // Without the Rounding feature and with new Number a dust pool
                // amount remains
                BEAST_EXPECT(amm.ammExists());
            }
            else if (!features[featureSingleAssetVault] && !features[featureLendingProtocol])
            {
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt), Ter(tecINTERNAL));
                BEAST_EXPECT(amm.ammExists());
            }
            else
            {
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt), Ter(tecAMM_BALANCE));
                BEAST_EXPECT(amm.ammExists());
            }
        }

        // MPT/MPT
        {
            Env env(*this, features, std::make_unique<CaptureLogs>(&logs));
            Account const gw{"gateway"}, alice{"alice"}, bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            MPT const usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            MPT const eur = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM amm(env, alice, usd(2), eur(1));
            amm.deposit(alice, IOUAmount{1'576123487565916, -15});
            amm.deposit(bob, IOUAmount{1'000});
            amm.withdraw(alice, IOUAmount{1'576123487565916, -15});
            amm.withdrawAll(bob);

            auto const lpToken =
                getAccountLines(env, alice, amm.lptIssue())[jss::lines][0u][jss::balance]
                    .asString();
            auto const lpTokenBalance =
                amm.ammRpcInfo()[jss::amm][jss::lp_token][jss::value].asString();
            if (!features[featureSingleAssetVault] && !features[featureLendingProtocol])
            {
                BEAST_EXPECT(lpToken == "1.414213562374011" && lpTokenBalance == "1.414213562374");
            }
            else
            {
                BEAST_EXPECT(lpToken == "1.414213562374011" && lpTokenBalance == "1.4142135623741");
            }

            auto res = isOnlyLiquidityProvider(*env.current(), amm.lptIssue(), alice);
            BEAST_EXPECT(res && res.value());

            if (features[fixAMMv1_3] && features[fixAMMClawbackRounding])
            {
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt));
                BEAST_EXPECT(!amm.ammExists());
            }
            else if (
                features[fixAMMv1_3] &&
                (features[featureSingleAssetVault] || features[featureLendingProtocol]))
            {
                // Without the Rounding feature and with new Number a dust pool
                // amount remains
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt));
                BEAST_EXPECT(amm.ammExists());
            }
            else if (!features[featureSingleAssetVault] && !features[featureLendingProtocol])
            {
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt), Ter(tecINTERNAL));
                BEAST_EXPECT(amm.ammExists());
            }
            else if (features[featureMPTokensV2])
            {
                env(amm::ammClawback(gw, alice, usd, eur, std::nullopt), Ter(tecAMM_BALANCE));
                BEAST_EXPECT(amm.ammExists());
            }
        }
    }

    void
    testClawAssetCheck(FeatureBitset features)
    {
        testcase("claw asset check for MPT and IOU");
        using namespace jtx;

        // IOU/MPT, MPT not clawable
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(1000)));
            env.close();

            MPT const btc =
                MPTTester({.env = env, .issuer = gw, .holders = {alice}, .pay = 40'000});

            AMM const amm(env, alice, usd(200), btc(100));
            // Asset BTC is not clawable without tfMPTCanClawback.
            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt), Ter(tecNO_PERMISSION));

            // Although USD is clawable with asfAllowTrustLineClawback.
            // When tfClawTwoAssets is set, we will claw Asser2 as well.
            // But Asset2 is not clawable. tfMPTCanClawback was not set for BTC.
            env(amm::ammClawback(gw, alice, usd, btc, std::nullopt),
                Txflags(tfClawTwoAssets),
                Ter(tecNO_PERMISSION));

            // Can only claw the other asset
            env(amm::ammClawback(gw, alice, usd, btc, std::nullopt));
        }

        // IOU/MPT, IOU not clawable
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(1000)));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            // Asset USD is not clawable without asfAllowTrustLineClawback.
            AMM const amm(env, alice, usd(200), btc(100));
            env(amm::ammClawback(gw, alice, usd, btc, std::nullopt), Ter(tecNO_PERMISSION));

            // Although BTC is clawable with tfMPTCanClawback.
            // When tfClawTwoAssets is set, we will claw Asset2 as well.
            // But Asset2 is not clawable. asfAllowTrustLineClawback was not set
            // by the issuer.
            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt),
                Txflags(tfClawTwoAssets),
                Ter(tecNO_PERMISSION));

            // Can only claw the other asset
            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt));
        }

        // IOU/MPT both clawable
        {
            Env env(*this, features);
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            Account const alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const usd = gw["USD"];
            env.trust(usd(100000), alice);
            env(pay(gw, alice, usd(1000)));
            env.close();

            MPT const btc = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | kMptDexFlags});

            AMM const amm(env, alice, usd(200), btc(100));

            // the account trying to claw MPT is not its issuer
            // will return temMALFORMED in preflight.
            env(amm::ammClawback(gw, alice, btc, usd, std::nullopt), Ter(temMALFORMED));
        }

        // only issuer can claw. IOU/MPT mix
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                Account const gw("gateway"), alice("alice"), bob("bob");
                env.fund(XRP(30'000), alice, bob, gw);
                env.close();
                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice},
                     .limit = 1'000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = bob,
                     .holders = {alice},
                     .limit = 1'000'000});
                env(pay(gw, alice, usd(50000)));
                env(pay(bob, alice, btc(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, usd(10000), btc(10100));
                // BTC's issuer is bob, alice can not clawback
                env(amm::ammClawback(gw, alice, btc, usd, std::nullopt), Ter(temMALFORMED));
            };
            testHelper2TokensMix(test);
        }

        // set tfClawTwoAssets, but the two assets are from different issuer.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                Account const gw("gateway"), alice("alice"), bob("bob");
                env.fund(XRP(30'000), alice, bob, gw);
                env.close();
                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice},
                     .limit = 1'000'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = bob,
                     .holders = {alice},
                     .limit = 1'000'000});
                env(pay(gw, alice, usd(50000)));
                env(pay(bob, alice, btc(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, usd(10000), btc(10100));
                // BTC's issuer is bob. But with tfClawTwoAssets, we will claw
                // both. It will fail because the other asset USD's issuer is
                // gw.
                env(amm::ammClawback(bob, alice, btc, usd, std::nullopt),
                    Txflags(tfClawTwoAssets),
                    Ter(temINVALID_FLAG));
            };
            testHelper2TokensMix(test);
        }
    }

    void
    run() override
    {
        FeatureBitset const all{jtx::testableAmendments() | fixAMMClawbackRounding};

        testInvalidRequest(all);
        testFeatureDisabled(all);
        testAMMClawbackAmount(all);
        testAMMClawbackAll(all);
        testAMMClawbackAmountSameIssuer(all);
        testAMMClawbackAllSameIssuer(all);
        testAMMClawbackIssuesEachOther(all);
        testAssetFrozenOrLocked(all);
        testSingleDepositAndClawback(all);
        testLastHolderLPTokenBalance(all);
        testLastHolderLPTokenBalance(all - fixAMMv1_3 - fixAMMClawbackRounding);
        testLastHolderLPTokenBalance(
            all - fixAMMv1_3 - fixAMMClawbackRounding - featureSingleAssetVault -
            featureLendingProtocol);
        testLastHolderLPTokenBalance(all - fixAMMClawbackRounding);
        testClawAssetCheck(all);
    }
};

BEAST_DEFINE_TESTSUITE(AMMClawbackMPT, app, xrpl);

}  // namespace xrpl::test
