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
#include <test/jtx/CaptureLogs.h>

#include <xrpld/app/misc/AMMUtils.h>

#include <xrpl/protocol/Feature.h>

namespace ripple {
namespace test {
class AMMClawbackMPT_test : public beast::unit_test::suite
{
    void
    testInvalidRequest(FeatureBitset features)
    {
        testcase("test invalid request");
        using namespace jtx;

        for (auto const& feature :
             {features, features - featureSingleAssetVault})
        {
            Env env(*this, feature);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback});

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(100)));
            env.close();

            AMM amm(env, gw, BTC(100), USD(100));

            // holder does not exist
            env(amm::ammClawback(
                    gw, Account("unknown"), USD, BTC, std::nullopt),
                ter(terNO_ACCOUNT));

            // can not clawback from self.
            env(amm::ammClawback(gw, gw, USD, BTC, std::nullopt),
                ter(temMALFORMED));

            // provided Asset does not match issuer gw
            {
                env(amm::ammClawback(
                        gw,
                        alice,
                        Issue{gw["USD"].currency, alice.id()},
                        BTC,
                        std::nullopt),
                    ter(temMALFORMED));
                env(amm::ammClawback(
                        gw,
                        alice,
                        MPTIssue{makeMptID(1, alice)},
                        USD,
                        std::nullopt),
                    ter(temMALFORMED));
            }

            // Amount does not match asset
            {
                env(amm::ammClawback(
                        gw,
                        alice,
                        USD,
                        BTC,
                        STAmount{Issue{gw["USD"].currency, alice.id()}, 1}),
                    ter(temBAD_AMOUNT));
                env(amm::ammClawback(
                        gw,
                        alice,
                        BTC,
                        USD,
                        STAmount{MPTIssue{makeMptID(1, alice)}, 10}),
                    ter(temBAD_AMOUNT));
            }

            // Amount is not greater than 0
            {
                env(amm::ammClawback(gw, alice, BTC, USD, BTC(-1)),
                    ter(temBAD_AMOUNT));
                env(amm::ammClawback(gw, alice, BTC, USD, BTC(0)),
                    ter(temBAD_AMOUNT));
            }

            // clawback from account not holding lptoken
            env(amm::ammClawback(gw, bob, BTC, USD, BTC(1000)),
                ter(tecAMM_BALANCE));

            // can not perform regular claw from amm pool
            {
                Issue usd(USD.currency, amm.ammAccount());
                auto amount = amountFromString(usd, "10");
                auto const err = feature[featureSingleAssetVault]
                    ? tecPSEUDO_ACCOUNT
                    : tecAMM_ACCOUNT;
                env(claw(gw, amount), ter(err));
            }

            // AMM does not exist
            {
                // withdraw all tokens will delete the AMM
                amm.withdrawAll(gw);
                BEAST_EXPECT(!amm.ammExists());
                env.close();
                env(amm::ammClawback(gw, alice, USD, BTC, std::nullopt),
                    ter(terNO_AMM));
            }
        }

        // tfMPTCanClawback is not enabled
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice}, .pay = 40'000});

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(10000)));
            env.close();

            AMM amm(env, gw, BTC(100), USD(100));
            env.close();
            amm.deposit(alice, 1'000);
            env.close();

            // can not clawback when tfMPTCanClawback is not enabled
            env(amm::ammClawback(gw, alice, BTC, USD, std::nullopt),
                ter(tecNO_PERMISSION));
        }

        // can not claw with tfClawTwoAssets if the assets are not issued by the
        // same issuer
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(10000), alice);
            env(pay(gw, alice, USD(10000)));
            env.close();

            // todo: check tfMPTCanTransfer in xrpl.org
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(100), USD(100));
            env.close();

            {
                // Return temINVALID_FLAG because the issuer set
                // tfClawTwoAssets, but the issuer only issues USD in the pool.
                // The issuer is not allowed to set tfClawTwoAssets flag if he
                // did not issue both assets in the pool.
                env(amm::ammClawback(gw, alice, USD, BTC, std::nullopt),
                    txflags(tfClawTwoAssets),
                    ter(temINVALID_FLAG));
            }
        }

        // Test if the issuer did not set asfAllowTrustLineClawback, but the MPT
        // is set tfMPTCanClawback, the issuer can claw MPT.
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(10000), gw, alice);
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(100), XRP(100));
            env.close();

            // If asfAllowTrustLineClawback is not set, the issuer can
            // still claw MPT because the MPT's tfMPTCanClawback is set.
            env(amm::ammClawback(gw, alice, BTC, XRP, std::nullopt));
        }
    }

    void
    testFeatureDisabled(FeatureBitset features)
    {
        testcase("test feature disabled.");
        using namespace jtx;
        Env env{*this, features};
        Account gw("gateway"), alice("alice");
        env.fund(XRP(30'000), gw, alice);
        env.close();
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();

        MPT BTC = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .pay = 10'000,
             .flags = tfMPTCanClawback | tfMPTCanTransfer});

        AMM amm(env, alice, XRP(1'000), BTC(1'000));

        // disable featureAMMClawback
        env.disableFeature(featureAMMClawback);
        env(amm::ammClawback(gw, alice, BTC, XRP, std::nullopt),
            ter(temDISABLED));

        // enable featureAMMClawback and disable featureMPTokensV2
        env.enableFeature(featureAMMClawback);
        env.disableFeature(featureMPTokensV2);
        env(amm::ammClawback(gw, alice, BTC, XRP, BTC(100)), ter(temDISABLED));

        // enable featureMPTokensV2
        env.enableFeature(featureMPTokensV2);
        env(amm::ammClawback(gw, alice, BTC, XRP, BTC(200)));
    }

    void
    testAMMClawbackAmount(FeatureBitset features)
    {
        testcase("test AMMClawback specific amount");
        using namespace jtx;

        // AMMClawback from MPT/IOU issued by different issuers
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(50000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(1000000000), USD(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'000000),
                USD(2000),
                IOUAmount{1414'213'562373095, -9}));

            // can not set tfClawTwoAssets because the assets are not issued by
            // the same issuer.
            env(amm::ammClawback(gw2, alice, BTC, USD, BTC(1000)),
                txflags(tfClawTwoAssets),
                ter(temINVALID_FLAG));

            auto aliceUSD = env.balance(alice, USD);
            auto aliceBTC = env.balance(alice, BTC);
            // gw clawback 1000 USD from alice
            env(amm::ammClawback(gw, alice, USD, BTC, USD(1000)));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                BTC(500'000000),
                USD(1000),
                IOUAmount{707'106'7811865475, -10}));
            // USD is clawed back,
            env.require(balance(alice, aliceUSD));
            // a proportional amount of BTC is returned to alice
            env.require(balance(alice, aliceBTC + BTC(500'000000)));
            aliceBTC = env.balance(alice, BTC);

            // gw2 clawback 250'000000 BTC from alice
            env(amm::ammClawback(gw2, alice, BTC, USD, BTC(250'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(250'000000),
                STAmount{USD, UINT64_C(499'9999999999999), -13},
                IOUAmount{353'553'3905932737, -10}));
            env.require(balance(alice, aliceUSD + USD(500)));
            env.require(balance(alice, aliceBTC));
            aliceUSD = env.balance(alice, USD);

            // gw2 clawback 500'000000 BTC which exceeds the balance,
            // this will clawback all and the amm will be deleted.
            env(amm::ammClawback(gw2, alice, BTC, USD, BTC(500'000000)));
            env.close();
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceUSD + USD(500)));
            env.require(balance(alice, aliceBTC));
        }

        // AMMClawback from MPT/XRP pool
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(1000000000), XRP(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'000000),
                XRP(2000),
                IOUAmount{1'414'213'562'373095, -6}));

            amm.deposit(bob, BTC(2'000'000000), XRP(4000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(3'000'000000),
                XRP(6000),
                IOUAmount{4'242'640'687'119285, -6}));

            auto aliceXRP = env.balance(alice, XRP);
            auto aliceBTC = env.balance(alice, BTC);
            auto bobXRP = env.balance(bob, XRP);
            auto bobBTC = env.balance(bob, BTC);

            // can not claw XRP
            env(amm::ammClawback(gw, alice, XRP, BTC, XRP(1000)),
                ter(temMALFORMED));
            // can not set tfClawTwoAssets
            env(amm::ammClawback(gw, alice, BTC, XRP, BTC(1000)),
                txflags(tfClawTwoAssets),
                ter(temINVALID_FLAG));

            // gw clawback 500 BTC from alice
            env(amm::ammClawback(gw, alice, BTC, XRP, BTC(500)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(2'999'999501),
                STAmount{XRP, UINT64_C(5'999'999001)},
                IOUAmount{4'242'639'980'012504, -6}));
            env.require(balance(alice, aliceXRP + drops(999)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(bob, bobXRP));
            env.require(balance(bob, bobBTC));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1'414'212'855'266314, -6}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{2'828'427'124'74619, -5}));
            aliceXRP = env.balance(alice, XRP);

            // gw clawback 1000'000000 BTC from bob
            env(amm::ammClawback(gw, bob, BTC, XRP, BTC(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'999'999501),
                STAmount{XRP, UINT64_C(3'999'999002)},
                IOUAmount{2828426418'110813, -6}));
            env.require(balance(alice, aliceXRP));
            env.require(balance(alice, aliceBTC));
            env.require(balance(bob, bobXRP + XRPAmount(1999999999)));
            env.require(balance(bob, bobBTC));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1'414'212'855'266314, -6}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1'414'213'562'844499, -6}));
            bobXRP = env.balance(bob, XRP);

            // gw clawback 1000'000000 BTC from alice, which exceeds her balance
            // will clawback all her balance
            env(amm::ammClawback(gw, alice, BTC, XRP, BTC(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'000001),
                XRPAmount(2'000'000002),
                IOUAmount{1'414'213'562'844499, -6}));
            env.require(balance(
                alice, aliceXRP + STAmount{XRP, UINT64_C(1'999'999000)}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(bob, bobXRP));
            env.require(balance(bob, bobBTC));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{1'414'213'562'844499, -6}));
            aliceXRP = env.balance(alice, XRP);

            // gw clawback from bob, which exceeds his balance
            env(amm::ammClawback(gw, bob, BTC, XRP, BTC(2'000'000000)));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceXRP));
            env.require(balance(alice, aliceBTC));
            env.require(balance(bob, bobXRP + XRPAmount(2000000002)));
            env.require(balance(bob, bobBTC));
        }

        // AMMClawback from MPT/MPT pool, different issuers
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, gw2, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            MPT ETH = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(2'000'000000), ETH(3'000'000000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(2'000'000000),
                ETH(3'000'000000),
                IOUAmount{2'449'489'742'783178, -6}));

            amm.deposit(bob, BTC(4'000'000000), ETH(6'000'000000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(6'000'000000),
                ETH(9'000'000000),
                IOUAmount{7'348'469'228'349534, -6}));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceETH = env.balance(alice, ETH);
            auto bobBTC = env.balance(bob, BTC);
            auto bobETH = env.balance(bob, ETH);

            // gw clawback BTC from alice
            env(amm::ammClawback(gw, alice, BTC, ETH, BTC(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(5'000'000000),
                ETH(7'500'000000),
                IOUAmount{6'123'724'356'957944, -6}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH + ETH(1'500'000000)));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{4'898'979'485'566356, -6}));
            aliceETH = env.balance(alice, ETH);

            // gw2 clawback ETH from bob
            env(amm::ammClawback(gw2, bob, ETH, BTC, ETH(3'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(3'000'000000),
                ETH(4'500'000000),
                IOUAmount{3'674'234'614'174766, -6}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC + BTC(2'000'000000)));
            env.require(balance(bob, bobETH));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{2'449'489'742'783178, -6}));
            bobBTC = env.balance(bob, BTC);

            // gw2 clawback ETH from alice, which exceeds her balance
            env(amm::ammClawback(gw2, alice, ETH, BTC, ETH(4'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(2'000'000001),
                ETH(3'000'000001),
                IOUAmount{2'449'489'742'783178, -6}));
            env.require(balance(alice, aliceBTC + BTC(999'999999)));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH));
            aliceBTC = env.balance(alice, BTC);

            // gw clawback BTC from bob, which exceeds his balance
            env(amm::ammClawback(gw, bob, BTC, ETH, BTC(3'000'000000)));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH + ETH(3'000'000001)));
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
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, gw2, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(50000)));
            env.trust(USD(200000), bob);
            env(pay(gw, bob, USD(60000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(2000000000), USD(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(2'000'000000), USD(2000), IOUAmount(2000000)));

            // gw clawback all BTC from alice
            amm.deposit(bob, BTC(1'000'000000), USD(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(3'000'000000), USD(3000), IOUAmount(3000000)));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceUSD = env.balance(alice, USD);
            auto bobBTC = env.balance(bob, BTC);
            auto bobUSD = env.balance(bob, USD);

            // gw2 clawback all BTC from alice
            env(amm::ammClawback(gw2, alice, BTC, USD, std::nullopt));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'000000), USD(1000), IOUAmount(1000000)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD + USD(2000)));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobUSD));
            aliceUSD = env.balance(alice, USD);

            // gw clawback all USD from bob
            env(amm::ammClawback(gw, bob, USD, BTC, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
            env.require(balance(bob, bobBTC + BTC(1'000'000000)));
            env.require(balance(bob, bobUSD));
        }

        // AMMClawback all from MPT/XRP pool
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(5000), XRP(10'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(5'000), XRP(10'000), IOUAmount{7'071'067'811865475, -9}));

            amm.deposit(bob, BTC(10'000), XRP(20'000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(15'000), XRP(30'000), IOUAmount{21'213'203'43559642, -8}));

            auto aliceXRP = env.balance(alice, XRP);
            auto aliceBTC = env.balance(alice, BTC);
            auto bobXRP = env.balance(bob, XRP);
            auto bobBTC = env.balance(bob, BTC);

            // gw clawback all BTC from alice
            env(amm::ammClawback(gw, alice, BTC, XRP, std::nullopt));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(10'000), XRP(20'000), IOUAmount{14'142'135'62373094, -8}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceXRP + XRP(10'000)));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobXRP));
            aliceXRP = env.balance(alice, XRP);

            // gw clawback all BTC from bob
            env(amm::ammClawback(gw, bob, BTC, XRP, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceXRP));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobXRP + XRP(20'000)));
        }

        // AMMClawback all from MPT/MPT pool, different issuers
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, gw2, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            MPT ETH = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(20'000), ETH(50'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(20'000), ETH(50'000), IOUAmount{31'622'77660168379, -11}));

            amm.deposit(bob, BTC(40'000), ETH(100'000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(60'000), ETH(150'000), IOUAmount{94'868'32980505137, -11}));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceETH = env.balance(alice, ETH);
            auto bobBTC = env.balance(bob, BTC);
            auto bobETH = env.balance(bob, ETH);

            // gw clawback all BTC from bob
            env(amm::ammClawback(gw, bob, BTC, ETH, std::nullopt));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(20'000), ETH(50'000), IOUAmount{31'622'77660168379, -11}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH + ETH(100'000)));
            bobETH = env.balance(bob, ETH);

            // gw2 clawback all ETH from alice
            env(amm::ammClawback(gw2, alice, ETH, BTC, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceBTC + BTC(20'000)));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH));
        }
    }

    void
    testAMMClawbackAmountSameIssuer(FeatureBitset features)
    {
        testcase(
            "test AMMClawback specific amount, assets have the same issuer");
        using namespace jtx;

        // AMMClawback from MPT/IOU issued by the same issuer
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(50000)));
            env.trust(USD(100000), bob);
            env(pay(gw, bob, USD(40000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(1'000'000000), USD(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'000000),
                USD(2000),
                IOUAmount{1414'213'562373095, -9}));

            amm.deposit(bob, BTC(500'000000), USD(1000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'500'000000),
                STAmount{USD, UINT64_C(2'999'999999999999), -12},
                IOUAmount{2'121'320'343559642, -9}));

            auto aliceUSD = env.balance(alice, USD);
            auto aliceBTC = env.balance(alice, BTC);
            auto bobUSD = env.balance(bob, USD);
            auto bobBTC = env.balance(bob, BTC);

            // gw clawback 500 USD from alice.
            env(amm::ammClawback(gw, alice, USD, BTC, USD(500)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1250'000001),
                USD(2500),
                IOUAmount{1'767'766'952966369, -9}));
            env.require(balance(alice, aliceUSD));
            env.require(balance(alice, aliceBTC + BTC(249'999999)));
            env.require(balance(bob, bobUSD));
            env.require(balance(bob, bobBTC));
            aliceBTC = env.balance(alice, BTC);
            // gw clawback 250'000000 BTC and 500 USD from bob
            // with tfClawTwoAssets
            env(amm::ammClawback(gw, bob, BTC, USD, BTC(250'000000)),
                txflags(tfClawTwoAssets));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'000002),
                STAmount{USD, UINT64_C(2000'0000004), -7},
                IOUAmount{1'414'213'562655938, -9}));
            env.require(balance(alice, aliceUSD));
            env.require(balance(alice, aliceBTC));
            env.require(balance(bob, bobUSD));
            env.require(balance(bob, bobBTC));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1'060'660'171779822, -9}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{353'553'390876116, -9}));

            // gw clawback USD from alice exceeding her balance
            env(amm::ammClawback(gw, alice, USD, BTC, USD(5'000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(250'000001),
                STAmount{USD, UINT64_C(500'0000004), -7},
                IOUAmount{353'553'390876116, -9}));
            env.require(balance(alice, aliceUSD));
            env.require(balance(alice, aliceBTC + BTC(750'000001)));
            env.require(balance(bob, bobUSD));
            env.require(balance(bob, bobBTC));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{353'553'390876116, -9}));
            aliceBTC = env.balance(alice, BTC);

            // gw clawback BTC from bob which exceeds his balance with
            // tfClawTwoAssets
            env(amm::ammClawback(gw, bob, BTC, USD, BTC(300'000000)),
                txflags(tfClawTwoAssets));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceUSD));
            env.require(balance(alice, aliceBTC));
            // USD is also clawed back from bob because of tfClawTwoAssets,
            // bob's USD balance will not change
            env.require(balance(bob, bobUSD));
            env.require(balance(bob, bobBTC));
        }

        // AMMClawback from MPT/MPT issued by the same issuer
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            MPT ETH = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(2'000'000000), ETH(3'000'000000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(2'000'000000),
                ETH(3'000'000000),
                IOUAmount{2'449'489'742'783178, -6}));

            amm.deposit(bob, BTC(4'000'000000), ETH(6'000'000000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(6'000'000000),
                ETH(9'000'000000),
                IOUAmount{7'348'469'228'349534, -6}));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceETH = env.balance(alice, ETH);
            auto bobBTC = env.balance(bob, BTC);
            auto bobETH = env.balance(bob, ETH);

            // gw clawback BTC from alice
            env(amm::ammClawback(gw, alice, BTC, ETH, BTC(1'000'000000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(5'000'000000),
                ETH(7'500'000000),
                IOUAmount{6'123'724'356'957944, -6}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH + ETH(1'500'000000)));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{4'898'979'485'566356, -6}));
            aliceETH = env.balance(alice, ETH);

            // gw clawback ETH and BTC from bob with tfClawTwoAssets
            env(amm::ammClawback(gw, bob, ETH, BTC, ETH(3'000'000000)),
                txflags(tfClawTwoAssets));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(3'000'000000),
                ETH(4'500'000000),
                IOUAmount{3'674'234'614'174766, -6}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1'224'744'871'391588, -6}));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{2'449'489'742'783178, -6}));

            // gw clawback BTC from alice, which exceeds her balance with
            // tfClawTwoAssets
            env(amm::ammClawback(gw, alice, BTC, ETH, BTC(3'000'000000)),
                txflags(tfClawTwoAssets));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(2'000'000001),
                ETH(3'000'000001),
                IOUAmount{2'449'489'742'783178, -6}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(
                amm.expectLPTokens(bob, IOUAmount{2'449'489'742'783178, -6}));

            // gw clawback ETH from bob, which is the same as his balance
            env(amm::ammClawback(gw, bob, ETH, BTC, ETH(3'000'000001)));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC + BTC(2'000'000001)));
            env.require(balance(bob, bobETH));
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
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(50000)));
            env.trust(USD(200000), bob);
            env(pay(gw, bob, USD(60000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(2'000'000000), USD(8'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(2'000'000000), USD(8'000), IOUAmount(4'000'000)));

            amm.deposit(bob, BTC(1'000'000000), USD(4'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(3'000'000000), USD(12'000), IOUAmount(6'000'000)));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceUSD = env.balance(alice, USD);
            auto bobBTC = env.balance(bob, BTC);
            auto bobUSD = env.balance(bob, USD);

            // gw clawback all BTC and USD from alice
            env(amm::ammClawback(gw, alice, BTC, USD, std::nullopt),
                txflags(tfClawTwoAssets));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'000000), USD(4'000), IOUAmount(2'000'000)));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(2'000'000)));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobUSD));

            // gw clawback all USD from bob
            env(amm::ammClawback(gw, bob, USD, BTC, std::nullopt));
            env.close();
            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
            env.require(balance(bob, bobBTC + BTC(1'000'000000)));
            env.require(balance(bob, bobUSD));
        }

        // AMMClawback all from MPT/MPT issued by the same issuer
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            MPT ETH = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, BTC(20'000), ETH(10'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(20'000), ETH(10'000), IOUAmount{14'142'13562373095, -11}));

            amm.deposit(bob, BTC(40'000), ETH(20'000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(60'000), ETH(30'000), IOUAmount{42'426'40687119285, -11}));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceETH = env.balance(alice, ETH);
            auto bobBTC = env.balance(bob, BTC);
            auto bobETH = env.balance(bob, ETH);

            // gw clawback all ETH from bob
            env(amm::ammClawback(gw, bob, ETH, BTC, std::nullopt));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(20'000), ETH(10'000), IOUAmount{14'142'13562373095, -11}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC + BTC(40'000)));
            env.require(balance(bob, bobETH));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{14'142'13562373095, -11}));
            BEAST_EXPECT(amm.expectLPTokens(bob, IOUAmount(0)));
            bobBTC = env.balance(bob, BTC);

            // gw clawback all ETH and BTC from alice with tfClawTwoAssets
            env(amm::ammClawback(gw, alice, ETH, BTC, std::nullopt),
                txflags(tfClawTwoAssets));
            env.close();

            // amm is empty and deleted
            BEAST_EXPECT(!amm.ammExists());
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(bob, bobBTC));
            env.require(balance(bob, bobETH));
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
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(1000000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), gw2);
            env(pay(gw, gw2, USD(5000)));
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(5000)));

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice, gw},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, gw, USD(1000), BTC(2000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                USD(1000), BTC(2000), IOUAmount{1414'213562373095, -12}));

            amm.deposit(gw2, USD(2000), BTC(4000));
            BEAST_EXPECT(amm.expectBalances(
                USD(3000), BTC(6000), IOUAmount{4242'640687119285, -12}));

            amm.deposit(alice, USD(3000), BTC(6000));
            BEAST_EXPECT(amm.expectBalances(
                USD(6000), BTC(12000), IOUAmount{8485'281374238570, -12}));

            BEAST_EXPECT(
                amm.expectLPTokens(gw, IOUAmount{1414'213562373095, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(gw2, IOUAmount{2828'427124746190, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{4242'640687119285, -12}));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceUSD = env.balance(alice, USD);
            auto gwBTC = env.balance(gw, BTC);
            auto gw2USD = env.balance(gw2, USD);

            // gw claws back 1000 USD from gw2.
            env(amm::ammClawback(gw, gw2, USD, BTC, USD(1000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                USD(5000), BTC(10000), IOUAmount{7071'067811865474, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(gw, IOUAmount{1414'213562373095, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(gw2, IOUAmount{1414'213562373094, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{4242'640687119285, -12}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
            env.require(balance(gw, gwBTC));
            env.require(balance(gw2, gw2USD));

            // gw2 claws back 1000 BTC from gw.
            env(amm::ammClawback(gw2, gw, BTC, USD, BTC(1000)),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                USD(4500), BTC(9001), IOUAmount{6363'961030678927, -12}));

            BEAST_EXPECT(
                amm.expectLPTokens(gw, IOUAmount{707'1067811865480, -13}));
            BEAST_EXPECT(
                amm.expectLPTokens(gw2, IOUAmount{1414'213562373094, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{4242'640687119285, -12}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
            env.require(balance(gw, gwBTC));
            env.require(balance(gw2, gw2USD));

            // gw2 claws back 4000 BTC from alice
            env(amm::ammClawback(gw2, alice, BTC, USD, BTC(4000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(2500'222197533607), -12},
                BTC(5001),
                IOUAmount{3535'84814069829, -11}));

            BEAST_EXPECT(
                amm.expectLPTokens(gw, IOUAmount{707'1067811865480, -13}));
            BEAST_EXPECT(
                amm.expectLPTokens(gw2, IOUAmount{1414'213562373094, -12}));
            BEAST_EXPECT(
                amm.expectLPTokens(alice, IOUAmount{1414'527797138648, -12}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(
                alice,
                aliceUSD + STAmount{USD, UINT64_C(1999'777802466393), -12}));
            env.require(balance(gw, gwBTC));
            env.require(balance(gw2, gw2USD));
        }

        // AMMClawback from MPT/MPT issued by each other
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env(fset(gw2, asfAllowTrustLineClawback));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {gw2, alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            MPT ETH = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {gw, alice},
                 .pay = 30'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, gw, BTC(10'000), ETH(50'000));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(10'000), ETH(50'000), IOUAmount{22'360'67977499789, -11}));

            amm.deposit(gw2, BTC(20'000), ETH(100'000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(30'000), ETH(150'000), IOUAmount{67'082'03932499367, -11}));

            amm.deposit(alice, BTC(40'000), ETH(200'000));
            BEAST_EXPECT(amm.expectBalances(
                BTC(70'000), ETH(350'000), IOUAmount{156'524'7584249852, -10}));

            auto aliceBTC = env.balance(alice, BTC);
            auto aliceETH = env.balance(alice, ETH);
            auto gw2BTC = env.balance(gw2, BTC);
            auto gwETH = env.balance(gw, ETH);

            // gw claws back 1000 BTC from gw2.
            env(amm::ammClawback(gw, gw2, BTC, ETH, BTC(1000)));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(69'001), ETH(345'001), IOUAmount{154'288'6904474855, -10}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(gw, gwETH));
            env.require(balance(gw2, gw2BTC));

            // gw2 claws back all ETH from gw
            env(amm::ammClawback(gw2, gw, ETH, BTC, std::nullopt));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(59'001), ETH(295'001), IOUAmount{131'928'0106724876, -10}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH));
            env.require(balance(gw, gwETH));
            env.require(balance(gw2, gw2BTC));

            // gw claws back all BTC from alice
            env(amm::ammClawback(gw, alice, BTC, ETH, std::nullopt));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                BTC(19'001), ETH(95'001), IOUAmount{42'485'29157249607, -11}));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceETH + ETH(200'000)));
            env.require(balance(gw, gwETH));
            env.require(balance(gw2, gw2BTC));
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
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1'000'000), gw, alice);

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            auto const USD = gw["USD"];
            env.trust(USD(1'000'000), alice);
            env(pay(gw, alice, USD(500'000)));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanClawback | tfMPTCanLock | MPTDEXFlags});
            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(10'000), BTC(10'000), IOUAmount(10'000)));
            env.close();

            auto aliceBTC = env.balance(alice, MPT(BTC));
            auto aliceUSD = env.balance(alice, USD);

            // globally locked and claw back 1000 BTC.
            // this should be successful
            BTC.set({.flags = tfMPTLock});
            env(amm::ammClawback(gw, alice, MPT(BTC), USD, BTC(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(9'000), BTC(9'000), IOUAmount(9'000)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD + USD(1'000)));
            aliceUSD = env.balance(alice, USD);

            // unlock and claw back 2000 BTC
            BTC.set({.flags = tfMPTUnlock});
            env(amm::ammClawback(gw, alice, MPT(BTC), USD, BTC(2'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(USD, UINT64_C(7'000'000000000001), -12),
                BTC(7'001),
                IOUAmount(7'000)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD + USD(2'000)));
            aliceUSD = env.balance(alice, USD);

            // globally freeze trustline and claw back 1000 USD.
            // this should be successful
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, USD, MPT(BTC), USD(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(USD, UINT64_C(6000'000000000002), -12),
                BTC(6'001),
                IOUAmount(6'000'000000000001, -12)));
            env.require(balance(alice, aliceBTC + BTC(1'000)));
            env.require(balance(alice, aliceUSD));
            aliceBTC = env.balance(alice, MPT(BTC));

            // globally unfreeze trustline and claw back 2000 USD
            // and 2000 BTC with tfClawTwoAssets
            env(fset(gw, asfGlobalFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, USD, MPT(BTC), USD(2'000)),
                txflags(tfClawTwoAssets));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(USD, UINT64_C(4'000'000000000002), -12),
                BTC(4'001),
                IOUAmount(4'000'000000000001, -12)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
        }

        // test AMMClawback when MPT individually locked or IOU individually
        // frozen
        {
            Env env{*this, features};
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1'000'000), gw, alice);

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            auto const USD = gw["USD"];
            env.trust(USD(1'000'000), alice);
            env(pay(gw, alice, USD(500'000)));

            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanClawback | tfMPTCanLock | MPTDEXFlags});
            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(10'000), BTC(10'000), IOUAmount(10'000)));
            env.close();

            auto aliceBTC = env.balance(alice, MPT(BTC));
            auto aliceUSD = env.balance(alice, USD);

            // individually locked and claw back 2000 BTC from alice
            BTC.set({.holder = alice, .flags = tfMPTLock});
            env(amm::ammClawback(gw, alice, MPT(BTC), USD, BTC(2'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(8'000), BTC(8'000), IOUAmount(8'000)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD + USD(2'000)));
            aliceUSD = env.balance(alice, USD);

            // individually freeze trustline and claw back 1000 USD from alice
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, USD, MPT(BTC), USD(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(7'000), BTC(7'000), IOUAmount(7'000)));
            env.require(balance(alice, aliceBTC + BTC(1'000)));
            env.require(balance(alice, aliceUSD));
            aliceBTC = env.balance(alice, MPT(BTC));

            // unlock MPT and claw back 3000 BTC from alice
            BTC.set({.holder = alice, .flags = tfMPTUnlock});
            env(amm::ammClawback(gw, alice, MPT(BTC), USD, BTC(3'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(4'000), BTC(4'000), IOUAmount(3'999'999999999999, -12)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD + USD(3'000)));
            aliceUSD = env.balance(alice, USD);

            // unlock trustline and claw back 1000 USD from alice
            env(trust(gw, alice["USD"](0), tfClearFreeze));
            env.close();
            env(amm::ammClawback(gw, alice, USD, MPT(BTC), USD(1'000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                STAmount(USD, UINT64_C(3'000'000000000001), -12),
                BTC(3'001),
                IOUAmount(3'000)));
            env.require(balance(alice, aliceBTC + BTC(999)));
            env.require(balance(alice, aliceUSD));
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
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1000000000), gw, alice);
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            // gw creates AMM pool of BTC/XRP.
            AMM amm(env, gw, XRP(100), BTC(400), ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(XRP(100), BTC(400), IOUAmount(200000)));
            amm.deposit(alice, BTC(400));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                XRP(100), BTC(800), IOUAmount{282842'712474619, -9}));

            auto aliceBTC = env.balance(alice, MPT(BTC));
            auto aliceXRP = env.balance(alice, XRP);

            // gw clawback 100 BTC from alice
            env(amm::ammClawback(gw, alice, MPT(BTC), XRP, BTC(100)));
            BEAST_EXPECT(amm.expectBalances(
                XRPAmount(87500001),
                BTC(701),
                IOUAmount{247'487'3734152917, -10}));

            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceXRP + XRPAmount(12'499999)));
        }

        // MPT/IOU
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1000000000), gw, alice);
            env.close();

            // gw sets asfAllowTrustLineClawback.
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.require(flags(gw, asfAllowTrustLineClawback));

            // gw issues 1000 USD to Alice.
            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(1000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            // gw creates AMM pool of BTC/USD.
            AMM amm(env, gw, USD(100), BTC(400), ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(USD(100), BTC(400), IOUAmount(200)));
            amm.deposit(alice, BTC(400));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                USD(100), BTC(800), IOUAmount{282'842712474619, -12}));

            auto aliceBTC = env.balance(alice, MPT(BTC));
            auto aliceUSD = env.balance(alice, USD);

            // gw clawback 100 BTC from alice
            env(amm::ammClawback(gw, alice, MPT(BTC), USD, BTC(100)));
            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(87'50000000000003), -14},
                BTC(701),
                IOUAmount{247'4873734152917, -13}));

            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD + USD(12.5)));
            aliceUSD = env.balance(alice, USD);

            // gw clawback 30 USD from alice with tfClawTwoAssets, which exceeds
            // her balance
            env(amm::ammClawback(gw, alice, USD, MPT(BTC), USD(30)),
                txflags(tfClawTwoAssets));
            BEAST_EXPECT(amm.expectBalances(
                STAmount{USD, UINT64_C(70'71067811865476), -14},
                BTC(567),
                IOUAmount(200)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
            BEAST_EXPECT(amm.expectLPTokens(alice, IOUAmount(0)));
            BEAST_EXPECT(amm.expectLPTokens(gw, IOUAmount(200)));
        }

        // MPT/MPT
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(1000000000), gw, alice);
            env.close();

            MPT USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            // gw creates AMM pool of BTC/USD.
            AMM amm(env, gw, USD(100), BTC(400), ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(
                amm.expectBalances(USD(100), BTC(400), IOUAmount(200)));
            amm.deposit(alice, BTC(400));
            env.close();
            BEAST_EXPECT(amm.expectBalances(
                USD(100), BTC(800), IOUAmount{282'842712474619, -12}));

            auto aliceBTC = env.balance(alice, MPT(BTC));
            auto aliceUSD = env.balance(alice, USD);

            // gw clawback 100 BTC from alice
            env(amm::ammClawback(gw, alice, MPT(BTC), USD, BTC(100)));
            BEAST_EXPECT(amm.expectBalances(
                USD(88), BTC(701), IOUAmount{247'4873734152917, -13}));

            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD + USD(12)));
            aliceUSD = env.balance(alice, USD);

            // gw clawback 30 USD from alice with tfClawTwoAssets, which exceeds
            // her balance
            env(amm::ammClawback(gw, alice, USD, MPT(BTC), USD(30)),
                txflags(tfClawTwoAssets));
            BEAST_EXPECT(amm.expectBalances(USD(72), BTC(567), IOUAmount(200)));
            env.require(balance(alice, aliceBTC));
            env.require(balance(alice, aliceUSD));
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
            Account gw{"gateway"}, alice{"alice"}, bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(50000)));
            env.trust(USD(100000), bob);
            env(pay(gw, bob, USD(40000)));
            env.close();

            MPT EUR = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, USD(2), EUR(1));
            amm.deposit(alice, IOUAmount{1'576123487565916, -15});
            amm.deposit(bob, IOUAmount{1'000});
            amm.withdraw(alice, IOUAmount{1'576123487565916, -15});
            amm.withdrawAll(bob);

            auto const lpToken =
                getAccountLines(
                    env, alice, amm.lptIssue())[jss::lines][0u][jss::balance]
                    .asString();
            auto const lpTokenBalance =
                amm.ammRpcInfo()[jss::amm][jss::lp_token][jss::value]
                    .asString();
            BEAST_EXPECT(
                lpToken == "1.414213562374011" &&
                lpTokenBalance == "1.414213562374");

            auto res =
                isOnlyLiquidityProvider(*env.current(), amm.lptIssue(), alice);
            BEAST_EXPECT(res && res.value());

            if (features[fixAMMv1_3] && features[fixAMMClawbackRounding])
            {
                env(amm::ammClawback(gw, alice, USD, EUR, std::nullopt));
                BEAST_EXPECT(!amm.ammExists());
            }
            else
            {
                env(amm::ammClawback(gw, alice, USD, EUR, std::nullopt),
                    ter(tecINTERNAL));
                BEAST_EXPECT(amm.ammExists());
            }
        }

        // MPT/MPT
        {
            Env env(*this, features, std::make_unique<CaptureLogs>(&logs));
            Account gw{"gateway"}, alice{"alice"}, bob{"bob"};
            env.fund(XRP(100000), gw, alice, bob);
            env.close();

            MPT USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            MPT EUR = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 40'000'000000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, USD(2), EUR(1));
            amm.deposit(alice, IOUAmount{1'576123487565916, -15});
            amm.deposit(bob, IOUAmount{1'000});
            amm.withdraw(alice, IOUAmount{1'576123487565916, -15});
            amm.withdrawAll(bob);

            auto const lpToken =
                getAccountLines(
                    env, alice, amm.lptIssue())[jss::lines][0u][jss::balance]
                    .asString();
            auto const lpTokenBalance =
                amm.ammRpcInfo()[jss::amm][jss::lp_token][jss::value]
                    .asString();
            BEAST_EXPECT(
                lpToken == "1.414213562374011" &&
                lpTokenBalance == "1.414213562374");

            auto res =
                isOnlyLiquidityProvider(*env.current(), amm.lptIssue(), alice);
            BEAST_EXPECT(res && res.value());

            if (features[fixAMMv1_3] && features[fixAMMClawbackRounding])
            {
                env(amm::ammClawback(gw, alice, USD, EUR, std::nullopt));
                BEAST_EXPECT(!amm.ammExists());
            }
            else
            {
                env(amm::ammClawback(gw, alice, USD, EUR, std::nullopt),
                    ter(tecINTERNAL));
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
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(1000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice}, .pay = 40'000});

            AMM amm(env, alice, USD(200), BTC(100));
            // Asset BTC is not clawable without tfMPTCanClawback.
            env(amm::ammClawback(gw, alice, BTC, USD, std::nullopt),
                ter(tecNO_PERMISSION));

            // Although USD is clawable with asfAllowTrustLineClawback.
            // When tfClawTwoAssets is set, we will claw Asser2 as well.
            // But Asset2 is not clawable. tfMPTCanClawback was not set for BTC.
            env(amm::ammClawback(gw, alice, USD, BTC, std::nullopt),
                txflags(tfClawTwoAssets),
                ter(tecNO_PERMISSION));

            // Can only claw the other asset
            env(amm::ammClawback(gw, alice, USD, BTC, std::nullopt));
        }

        // IOU/MPT, IOU not clawable
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, alice);
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(1000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            // Asset USD is not clawable without asfAllowTrustLineClawback.
            AMM amm(env, alice, USD(200), BTC(100));
            env(amm::ammClawback(gw, alice, USD, BTC, std::nullopt),
                ter(tecNO_PERMISSION));

            // Although BTC is clawable with tfMPTCanClawback.
            // When tfClawTwoAssets is set, we will claw Asser2 as well.
            // But Asset2 is not clawable. asfAllowTrustLineClawback was not set
            // by the issuer.
            env(amm::ammClawback(gw, alice, BTC, USD, std::nullopt),
                txflags(tfClawTwoAssets),
                ter(tecNO_PERMISSION));

            // Can only claw the other asset
            env(amm::ammClawback(gw, alice, BTC, USD, std::nullopt));
        }

        // IOU/MPT both clawable
        {
            Env env(*this, features);
            Account gw{"gateway"};
            Account gw2{"gateway2"};
            Account alice{"alice"};
            env.fund(XRP(100000), gw, gw2, alice);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            auto const USD = gw["USD"];
            env.trust(USD(100000), alice);
            env(pay(gw, alice, USD(1000)));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw2,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanClawback | tfMPTCanTransfer});

            AMM amm(env, alice, USD(200), BTC(100));

            // the account trying to claw MPT is not its issuer
            // will return temMALFORMED in preflight.
            env(amm::ammClawback(gw, alice, BTC, USD, std::nullopt),
                ter(temMALFORMED));
        }

        // only issuer can claw. IOU/MPT mix
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                Account gw("gateway"), alice("alice"), bob("bob");
                env.fund(XRP(30'000), alice, bob, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = bob,
                     .holders = {alice},
                     .limit = 1'000'000});
                env(pay(gw, alice, USD(50000)));
                env(pay(bob, alice, BTC(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10100));
                // BTC's issuer is bob, alice can not clawback
                env(amm::ammClawback(gw, alice, BTC, USD, std::nullopt),
                    ter(temMALFORMED));
            };
            testHelper2TokensMix(test);
        }

        // set tfClawTwoAssets, but the two assets are from different issuer.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                Account gw("gateway"), alice("alice"), bob("bob");
                env.fund(XRP(30'000), alice, bob, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = bob,
                     .holders = {alice},
                     .limit = 1'000'000});
                env(pay(gw, alice, USD(50000)));
                env(pay(bob, alice, BTC(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10100));
                // BTC's issuer is bob. But with tfClawTwoAssets, we will claw
                // both. It will fail because the other asset USD's issuer is
                // gw.
                env(amm::ammClawback(bob, alice, BTC, USD, std::nullopt),
                    txflags(tfClawTwoAssets),
                    ter(temINVALID_FLAG));
            };
            testHelper2TokensMix(test);
        }
    }

    void
    run() override
    {
        FeatureBitset const all{
            jtx::testable_amendments() | fixAMMClawbackRounding};

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
        testLastHolderLPTokenBalance(all - fixAMMClawbackRounding);
        testClawAssetCheck(all);
    }
};

BEAST_DEFINE_TESTSUITE(AMMClawbackMPT, app, ripple);

}  // namespace test
}  // namespace ripple