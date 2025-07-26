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

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/tx/detail/AMMBid.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {
namespace test {

/**
 * Basic tests of AMM functionality involving MPT assets, excluding those that
 * use offers. Tests incorporating offers are in `AMMExtended_test`.
 */
struct AMMMPT_test : public jtx::AMMTest
{
private:
    void
    testInstanceCreate()
    {
        testcase("Instance Create");

        using namespace jtx;

        // XRP to MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'000'000}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // IOU to MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(20'000), MPT(ammAlice[1])(20'000), IOUAmount{20'000}));
            },
            {{USD(20'000), AMMMPT(20'000)}});

        // MPT to MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(20'000),
                    MPT(ammAlice[1])(20'000),
                    IOUAmount{20'000}));
            },
            {{AMMMPT(20'000), AMMMPT(20'000)}});

        // IOU to MPT + transfer fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .transferFee = 1'500,
                 .pay = 30'000});
            // no transfer fee on create
            AMM ammAlice(env, alice, USD(20'000), BTC(20'000));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(20'000), BTC(20'000), IOUAmount{20'000, 0}));
            BEAST_EXPECT(expectLine(env, alice, USD(0)));
            // alice initially had 30'000
            BEAST_EXPECT(expectMPT(env, alice, BTC(10'000)));
        }

        // Require authorization is set, account is authorized
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env(trust(alice, gw["USD"](30'000), 0));
            env(trust(gw, alice["USD"](0), tfSetfAuth));
            env(pay(gw, alice, USD(10'000)));
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTRequireAuth | MPTDEXFlags,
                 .authHolder = true});
            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));
        }

        // Cleared global freeze
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            USD.set({.flags = tfMPTLock});
            AMM ammAliceFail(
                env, alice, XRP(10'000), USD(10'000), ter(tecFROZEN));
            USD.set({.flags = tfMPTUnlock});
            AMM ammAlice(env, alice, XRP(10'000), USD(10'000));
        }
    }

    void
    testInvalidInstance()
    {
        testcase("Invalid Instance");

        using namespace jtx;

        // Can't have both tokens the same MPT
        {
            Env env{*this};
            env.fund(XRP(30'000), alice, gw);
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .transferFee = 1'500,
                 .pay = 40'000});
            AMM ammAlice(
                env, alice, BTC(20'000), BTC(10'000), ter(temBAD_AMM_TOKENS));
        }

        // MPT did not set lsfMPTCanTransfer
        {
            Env env{*this};
            env.fund(XRP(30'000), alice, gw);
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanLock});
            AMM ammAlice(
                env, alice, BTC(20'000), XRP(10'000), ter(tecNO_PERMISSION));
        }

        // Can't have zero or negative amounts
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000)}, Fund::All);
            env(rate(gw, 1.25));
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .transferFee = 1'500,
                 .pay = 30'000});

            AMM ammAlice(env, alice, XRP(0), BTC(10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice.ammExists());
            AMM ammAlice1(env, alice, XRP(10'000), BTC(0), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice1.ammExists());
            AMM ammAlice2(env, alice, USD(0), BTC(10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice2.ammExists());
            AMM ammAlice3(env, alice, USD(10'000), BTC(0), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice3.ammExists());
            AMM ammAlice4(
                env, alice, XRP(-10'0000), BTC(10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice4.ammExists());
            AMM ammAlice5(
                env, alice, XRP(10'000), BTC(-10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice5.ammExists());
            AMM ammAlice6(
                env, alice, USD(-10'000), BTC(10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice6.ammExists());
            AMM ammAlice7(
                env, alice, USD(10'000), BTC(-10'000), ter(temBAD_AMOUNT));
            BEAST_EXPECT(!ammAlice7.ammExists());
        }

        // Bad MPT
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000)}, Fund::All);

            AMM ammAlice(
                env, alice, XRP(10'000), MPT(badMPT())(100), ter(temBAD_MPT));

            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Insufficient MPT balance
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(30'000)}, Fund::All);
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .transferFee = 1'500,
                 .pay = 30'000});
            AMM ammAlice(
                env, alice, XRP(10'000), BTC(40'000), ter(tecUNFUNDED_AMM));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // Invalid trading fee
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000)}, Fund::All);
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .transferFee = 1'500,
                 .pay = 30'000});
            AMM ammAlice(
                env,
                alice,
                USD(10'000),
                BTC(10'000),
                false,
                65'001,
                10,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(temBAD_FEE));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // AMM already exists XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Account bob1("bob1");
                env.fund(XRP(30'000), bob);
                env.close();
                AMM ammBob(
                    env,
                    bob,
                    XRP(1'000),
                    MPT(ammAlice[1])(1'000),
                    ter(tecDUPLICATE));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // AMM already exists IOU/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(XRP(30'000), bob);
                env.close();
                env.trust(USD(10000), bob);
                env(pay(gw, bob, USD(100)));

                AMM ammBob(
                    env,
                    bob,
                    USD(1'000),
                    MPT(ammAlice[1])(1'000),
                    ter(tecDUPLICATE));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // AMM already exists MPT/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                AMM ammCarol(
                    env,
                    carol,
                    MPT(ammAlice[0])(1'000),
                    MPT(ammAlice[1])(2'000),
                    ter(tecDUPLICATE));
            },
            {{AMMMPT(20'000), AMMMPT(10'000)}});

        // Require authorization is set
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            env(fset(gw, asfRequireAuth));
            env(trust(alice, gw["USD"](30'000), 0));
            env(trust(gw, alice["USD"](0), tfSetfAuth));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .flags = tfMPTRequireAuth});
            AMM ammAlice(env, alice, USD(10'000), BTC(10'000), ter(tecNO_AUTH));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // MPT globally locked
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000)}, Fund::All);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            BTC.set({.flags = tfMPTLock});
            AMM ammAlice(env, alice, USD(10'000), BTC(10'000), ter(tecFROZEN));
            BEAST_EXPECT(!ammAlice.ammExists());
        }

        // MPT individually locked
        {
            Env env{*this};
            fund(env, gw, {alice, bob}, {USD(20'000)}, Fund::All);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            BTC.set({.holder = alice, .flags = tfMPTLock});

            // alice's token is locked
            AMM ammAlice(env, alice, USD(10'000), BTC(10'000), ter(tecFROZEN));
            BEAST_EXPECT(!ammAlice.ammExists());

            // bob can create
            AMM ammBob(env, bob, USD(10'000), BTC(10'000));
            BEAST_EXPECT(ammBob.ammExists());
        }
    }

    void
    testInvalidDeposit(FeatureBitset features)
    {
        testcase("Invalid Deposit");

        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // LPTokenOut can not be zero
                ammAlice.deposit(
                    alice,
                    0,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // LPTokenOut can not be negative
                ammAlice.deposit(
                    alice,
                    IOUAmount{-1},
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // LPTokenOut can not be MPT
                {
                    Json::Value jv = Json::objectValue;
                    jv[jss::Account] = alice.human();
                    jv[jss::TransactionType] = jss::AMMDeposit;
                    jv[jss::Asset] =
                        STIssue(sfAsset, XRP).getJson(JsonOptions::none);
                    jv[jss::Asset2] = STIssue(sfAsset, MPT(ammAlice[1]))
                                          .getJson(JsonOptions::none);
                    jv[jss::LPTokenOut] = MPT(ammAlice[1])(100).value().getJson(
                        JsonOptions::none);
                    jv[jss::Flags] = tfLPToken;
                    env(jv, ter(telENV_RPC_FAILED));
                }

                // Provided LPTokenOut does not match AMM pool's LPToken
                // asset
                {
                    Json::Value jv = Json::objectValue;
                    jv[jss::Account] = alice.human();
                    jv[jss::TransactionType] = jss::AMMDeposit;
                    jv[jss::Asset] =
                        STIssue(sfAsset, XRP).getJson(JsonOptions::none);
                    jv[jss::Asset2] = STIssue(sfAsset, MPT(ammAlice[1]))
                                          .getJson(JsonOptions::none);
                    jv[jss::LPTokenOut] =
                        USD(100).value().getJson(JsonOptions::none);
                    jv[jss::Flags] = tfLPToken;
                    env(jv, ter(temBAD_AMM_TOKENS));
                }

                // Invalid trading fee
                ammAlice.deposit(
                    carol,
                    std::nullopt,
                    XRP(200),
                    MPT(ammAlice[1])(200),
                    std::nullopt,
                    tfTwoAssetIfEmpty,
                    std::nullopt,
                    std::nullopt,
                    10'000,
                    ter(temBAD_FEE));

                // Invalid tokens
                {
                    auto const mpt1 = MPTIssue{MPTID(0xabc)};
                    auto const mpt2 = MPTIssue{MPTID(0xdef)};
                    ammAlice.deposit(
                        alice,
                        1'000,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        {{mpt1, mpt2}},
                        std::nullopt,
                        std::nullopt,
                        ter(terNO_AMM));
                }

                // invalid MPT
                ammAlice.deposit(
                    alice,
                    badMPT(),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_MPT));
                ammAlice.deposit(
                    alice,
                    XRP(100),
                    badMPT(),
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_MPT));

                MPTTester BTC(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol},
                     .flags = tfMPTCanLock | MPTDEXFlags,
                     .authHolder = true});

                // Depositing mismatched token, invalid Asset1In.issue
                ammAlice.deposit(
                    alice,
                    BTC(100),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // Depositing mismatched token, invalid Asset2In.issue
                ammAlice.deposit(
                    alice,
                    XRP(100),
                    BTC(100),
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // Assets can not be the same
                ammAlice.deposit(
                    alice,
                    MPT(ammAlice[1])(100),
                    MPT(ammAlice[1])(200),
                    std::nullopt,
                    tfTwoAsset,
                    ter(temBAD_AMM_TOKENS));

                // Invalid amount value
                ammAlice.deposit(
                    alice,
                    MPT(ammAlice[1])(0),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
                ammAlice.deposit(
                    alice,
                    MPT(ammAlice[1])(-1'000),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));

                // Invalid Account
                {
                    Account bad("bad");
                    env.memoize(bad);
                    ammAlice.deposit(
                        bad,
                        std::nullopt,
                        MPT(ammAlice[1])(1'000),
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        seq(1),
                        std::nullopt,
                        ter(terNO_ACCOUNT));
                }

                // Invalid AMM
                ammAlice.deposit(
                    alice,
                    1'000,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    {{MPT(BTC), MPT(ammAlice[1])}},
                    std::nullopt,
                    std::nullopt,
                    ter(terNO_AMM));

                // Single deposit: 100000 tokens worth of MPT
                // Amount to deposit exceeds Max
                ammAlice.deposit(
                    carol,
                    100'000,
                    MPT(ammAlice[1])(200),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED));

                // Single deposit: 100000 tokens worth of XRP
                // Amount to deposit exceeds Max
                ammAlice.deposit(
                    carol,
                    100'000,
                    XRP(200),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED));

                // Deposit amount is invalid
                ammAlice.deposit(
                    alice,
                    MPT(ammAlice[1])(0),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 1, -1},
                    std::nullopt,
                    ter(tecUNFUNDED_AMM));
                // Calculated amount is 0
                ammAlice.deposit(
                    alice,
                    MPT(ammAlice[1])(0),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 2'000, -6},
                    std::nullopt,
                    ter(tecAMM_FAILED));

                // Tiny deposit
                ammAlice.deposit(
                    carol,
                    IOUAmount{1, -4},
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));

                // Deposit non-empty AMM
                ammAlice.deposit(
                    carol,
                    XRP(100),
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    tfTwoAssetIfEmpty,
                    ter(tecAMM_NOT_EMPTY));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Tiny deposit
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const enabledv1_3 =
                    env.current()->rules().enabled(fixAMMv1_3);
                auto const err =
                    !enabledv1_3 ? ter(temBAD_AMOUNT) : ter(tesSUCCESS);
                // Pre-amendment XRP deposit side is rounded to 0
                // and deposit fails.
                ammAlice.deposit(
                    carol, IOUAmount{1, -1}, std::nullopt, std::nullopt, err);
            },
            {{XRP(10'000), AMMMPT(10'000)}},
            0,
            std::nullopt,
            {features, features - fixAMMv1_3});

        // Invalid AMM
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.withdrawAll(alice);
                ammAlice.deposit(
                    alice, 10'000, std::nullopt, std::nullopt, ter(terNO_AMM));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit MPT with eprice
        // the calculated amount to deposit is negative.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 1, -1},
                    tfLimitLPToken,
                    ter(tecAMM_FAILED));

                // although we should use lptoken unit for eprice,
                // we don't check the currency any more, we just use
                // the value
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    STAmount{USD, 1, -1},
                    tfLimitLPToken,
                    ter(tecAMM_FAILED));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Globally locked MPT
        {
            Env env{*this};
            fund(env, gw, {alice, carol}, {USD(20'000)}, Fund::All);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));
            BTC.set({.flags = tfMPTLock});

            ammAlice.deposit(
                carol,
                BTC(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            ammAlice.deposit(
                carol, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));

            ammAlice.deposit(
                carol,
                USD(100),
                BTC(100),
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
        }

        // Individually lock MPT or freeze IOU (AMM) with IOU/MPT AMM
        {
            Env env{*this, features};
            fund(env, gw, {alice, carol}, {USD(20'000)}, Fund::All);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));

            // Carol's mpt is locked
            BTC.set({.holder = carol, .flags = tfMPTLock});

            // Carol can not deposit locked mpt
            ammAlice.deposit(
                carol,
                BTC(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            ammAlice.deposit(
                carol, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));

            if (!features[featureAMMClawback])
            {
                ammAlice.deposit(
                    carol,
                    USD(100),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecLOCKED));
            }
            else
            {
                // Carol can not deposit non-forzen token either
                ammAlice.deposit(
                    carol,
                    USD(100),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecFROZEN));
            }

            // Alice can deposit because she's not individually locked
            ammAlice.deposit(
                alice, BTC(100), std::nullopt, std::nullopt, std::nullopt);
            ammAlice.deposit(alice, 1'000, std::nullopt, std::nullopt);
            ammAlice.deposit(
                alice, USD(100), std::nullopt, std::nullopt, std::nullopt);

            // Unlock
            BTC.set({.holder = carol, .flags = tfMPTUnlock});
            // Carol can deposit after unlock
            ammAlice.deposit(
                carol, BTC(100), std::nullopt, std::nullopt, std::nullopt);
            ammAlice.deposit(carol, 1'000, std::nullopt, std::nullopt);

            // Individually frozen AMM
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            env.close();

            // Can deposit non-frozen token
            ammAlice.deposit(
                carol, BTC(100), std::nullopt, std::nullopt, std::nullopt);

            // Cannot deposit frozen token
            ammAlice.deposit(
                carol, 1'000'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            // unfreeze IOU
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfClearFreeze));
            env.close();
            // Can deposit
            ammAlice.deposit(carol, 1'000, std::nullopt, std::nullopt);

            // Individually lock AMM
            BTC.set({.holder = ammAlice.ammAccount(), .flags = tfMPTLock});

            // Can deposit non-frozen token
            ammAlice.deposit(
                carol, USD(100), std::nullopt, std::nullopt, std::nullopt);

            // Can not deposit locked token
            ammAlice.deposit(
                carol, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                BTC(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            // Unlock AMM MPT
            BTC.set({.holder = ammAlice.ammAccount(), .flags = tfMPTUnlock});
            // can deposit
            ammAlice.deposit(carol, 1'000, std::nullopt, std::nullopt);
            ammAlice.deposit(
                carol, BTC(100), std::nullopt, std::nullopt, std::nullopt);
        }

        // Individually lock MPT (AMM) account with MPT/MPT AMM
        {
            Env env{*this};
            env.fund(XRP(10'000), gw, alice, carol);
            env.close();
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 40'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));

            // Carol's BTC is locked
            BTC.set({.holder = carol, .flags = tfMPTLock});
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            ammAlice.deposit(
                carol,
                BTC(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            // Unlock carol's BTC
            BTC.set({.holder = carol, .flags = tfMPTUnlock});

            // Can deposit
            ammAlice.deposit(
                carol, USD(100), std::nullopt, std::nullopt, std::nullopt);
            ammAlice.deposit(
                carol, BTC(100), std::nullopt, std::nullopt, std::nullopt);

            // Individually lock MPT BTC (AMM) account
            BTC.set({.holder = ammAlice.ammAccount(), .flags = tfMPTLock});

            // Can deposit non-locked token USD
            ammAlice.deposit(
                carol, USD(100), std::nullopt, std::nullopt, std::nullopt);

            // Can not deposit locked token BTC
            ammAlice.deposit(
                carol, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                BTC(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            // Unlock AMM MPT BTC
            BTC.set({.holder = ammAlice.ammAccount(), .flags = tfMPTUnlock});
            // Can deposit BTC
            ammAlice.deposit(carol, 1'000, std::nullopt, std::nullopt);
            ammAlice.deposit(
                carol, BTC(100), std::nullopt, std::nullopt, std::nullopt);

            // Individually Lock MPT USD (AMM) account
            USD.set({.holder = ammAlice.ammAccount(), .flags = tfMPTLock});

            // Can deposit non-locked token BTC
            ammAlice.deposit(
                carol, BTC(100), std::nullopt, std::nullopt, std::nullopt);

            // Can not deposit locked token USD
            ammAlice.deposit(
                carol, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.deposit(
                carol,
                USD(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));

            // Unlock AMM MPT USD
            USD.set({.holder = ammAlice.ammAccount(), .flags = tfMPTUnlock});
            // Can deposit USD
            ammAlice.deposit(carol, 1'000, std::nullopt, std::nullopt);
            ammAlice.deposit(
                carol, USD(100), std::nullopt, std::nullopt, std::nullopt);
        }

        // Deposit unauthorized token
        {
            Env env{*this, features};
            Account gw("gateway"), alice{"alice"}, carol{"carol"};
            env.fund(XRP(30'000), alice, carol, gw);
            env.close();

            MPTTester BTC(env, gw, {.holders = {alice, carol}, .fund = false});
            BTC.create(
                {.maxAmt = 1'000'000,
                 .authorize = {{alice}},
                 .pay = {{{alice}, 10'000}},
                 .flags = tfMPTRequireAuth | MPTDEXFlags,
                 .authHolder = true});

            AMM amm(env, alice, XRP(10'000), BTC(10'000));
            env.close();

            if (!features[featureAMMClawback])
                amm.deposit(
                    carol, XRP(10), std::nullopt, std::nullopt, std::nullopt);
            else
                amm.deposit(
                    carol,
                    XRP(10),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecNO_AUTH));
        }

        // Insufficient XRP balance
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(XRP(1'000), bob);
                env.close();
                ammAlice.deposit(bob, XRP(10));
                ammAlice.deposit(
                    bob,
                    XRP(1'000),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecUNFUNDED_AMM));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Insufficient MPT balance
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(450'000),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecUNFUNDED_AMM));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Insufficient IOU balance
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                fund(env, gw, {bob}, {USD(1'000)}, Fund::Acct);
                ammAlice.deposit(
                    bob,
                    USD(1'001),
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecUNFUNDED_AMM));
            },
            {{USD(1000), AMMMPT(1000)}});

        // Insufficient MPT balance by tokens
        {
            Env env{*this};
            env.fund(XRP(30'000), alice, bob, gw);
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .transferFee = 1'500,
                 .pay = 1000});
            AMM ammAlice(env, alice, XRP(20'000), BTC(1000));
            ammAlice.deposit(
                bob,
                10'000'000,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecUNFUNDED_AMM));
        }

        // Insufficient reserve, XRP/MPT
        {
            Env env(*this);
            auto const starting_xrp =
                reserve(env, 4) + env.current()->fees().base * 4;
            env.fund(XRP(10'000), gw);
            env.fund(XRP(10'000), alice);
            env.fund(starting_xrp, carol);

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 1'500,
                 .pay = 40'000});

            env(offer(carol, XRP(100), BTC(101)));
            AMM ammAlice(env, alice, XRP(1000), BTC(1000));
            ammAlice.deposit(
                carol,
                XRP(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));

            env(offer(carol, XRP(100), BTC(102)));
            ammAlice.deposit(
                carol,
                BTC(100),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                ter(tecINSUF_RESERVE_LINE));
        }

        // Invalid min
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // min tokens can't be <= zero
                ammAlice.deposit(
                    carol, 0, XRP(100), tfSingleAsset, ter(temBAD_AMM_TOKENS));
                ammAlice.deposit(
                    carol, -1, XRP(100), tfSingleAsset, ter(temBAD_AMM_TOKENS));
                ammAlice.deposit(
                    carol,
                    0,
                    XRP(100),
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    tfTwoAsset,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // min amounts can't be <= zero
                ammAlice.deposit(
                    carol,
                    1'000,
                    XRP(0),
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    tfTwoAsset,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
                ammAlice.deposit(
                    carol,
                    1'000,
                    XRP(100),
                    MPT(ammAlice[1])(-1),
                    std::nullopt,
                    tfTwoAsset,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
                ammAlice.deposit(
                    carol,
                    1'000,
                    XRP(100),
                    badMPT(),
                    std::nullopt,
                    tfTwoAsset,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_MPT));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Min deposit
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Equal deposit by tokens
                ammAlice.deposit(
                    carol,
                    1'000'000,
                    XRP(1'000),
                    MPT(ammAlice[1])(1'001),
                    std::nullopt,
                    tfLPToken,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED));
                ammAlice.deposit(
                    carol,
                    1'000'000,
                    XRP(1'001),
                    MPT(ammAlice[1])(1'000),
                    std::nullopt,
                    tfLPToken,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED));
                // Equal deposit by asset
                ammAlice.deposit(
                    carol,
                    100'001,
                    XRP(100),
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    tfTwoAsset,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED));
                // Single deposit by asset
                ammAlice.deposit(
                    carol,
                    488'090,
                    XRP(1'000),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED));
            },
            {{XRP(1000), AMMMPT(1000)}});
    }

    void
    testDeposit()
    {
        testcase("Deposit");

        using namespace jtx;

        // Equal deposit: 1000000 tokens. XRP/MPT AMM.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, 1'000'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{11'000'000, 0}));

                // carol deposited 1000 XRP and pays the transaction fee
                env.require(
                    balance(carol, carolXRP - XRP(1000) - drops(baseFee)));
                // carol deposited 1000 MPT
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(1000)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // single deposit MPT with eprice
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 1, -1},
                    tfLimitLPToken);

                // although we should use lptoken unit for eprice,
                // we don't check the currency any more, we just use
                // the value
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    STAmount{USD, 1, -1},
                    tfLimitLPToken);
            },
            {{USD(10'000'000), AMMMPT(10'000)}});

        // Equal deposit: 1000000 tokens. IOU/MPT combination
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto carolBTC = env.balance(carol, BTC);
                auto carolUSD = env.balance(carol, USD);

                ammAlice.deposit(carol, 1'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(11'000), USD(11'000), IOUAmount(11'000)));

                env.require(balance(carol, carolBTC - BTC(1000)));
                env.require(balance(carol, carolUSD - USD(1000)));
            };
            testHelper2TokensMix(test);
        }

        // Deposit 100MPT/100XRP. XRP/MPT AMM.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, MPT(ammAlice[1])(100), XRP(100));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100),
                    MPT(ammAlice[1])(10'100),
                    IOUAmount{10'100'000, 0}));

                env.require(
                    balance(carol, carolXRP - XRP(100) - drops(baseFee)));
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(100)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Deposit MPT/IOU combination
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto carolBTC = env.balance(carol, BTC);
                auto carolUSD = env.balance(carol, USD);
                ammAlice.deposit(carol, BTC(100), USD(100));

                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(10'100), USD(10'100), IOUAmount(10'100)));

                env.require(balance(carol, carolBTC - BTC(100)));
                env.require(balance(carol, carolUSD - USD(100)));
            };
            testHelper2TokensMix(test);
        }

        // Equal limit deposit.
        // Try to deposit 200MPT/100XRP. Is truncated to 100MPT/100XRP.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, MPT(ammAlice[1])(200), XRP(100));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100),
                    MPT(ammAlice[1])(10'100),
                    IOUAmount{10'100'000, 0}));

                env.require(
                    balance(carol, carolXRP - XRP(100) - drops(baseFee)));
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(100)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal limit deposit. MPT/IOU combination.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto carolBTC = env.balance(carol, BTC);
                auto carolUSD = env.balance(carol, USD);
                ammAlice.deposit(carol, BTC(200), USD(100));
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(10'100), USD(10'100), IOUAmount(10'100)));

                env.require(balance(carol, carolBTC - BTC(100)));
                env.require(balance(carol, carolUSD - USD(100)));
            };
            testHelper2TokensMix(test);
        }

        //  Single deposit: 1000 MPT into MPT/XRP
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, MPT(ammAlice[1])(1000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'488'088'48170151, -8}));

                env.require(balance(carol, carolXRP - drops(baseFee)));
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(1000)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        //  Single deposit: 1000 XRP into MPT/XRP
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, XRP(1000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'488'088'48170151, -8}));

                env.require(
                    balance(carol, carolXRP - XRP(1000) - drops(baseFee)));
                env.require(balance(carol, carolMPT));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit: 1000 MPT0 into MPT/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto carolMPT0 = env.balance(carol, MPT(ammAlice[0]));
                auto carolMPT1 = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, MPT(ammAlice[0])(1000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(11'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'488'08848170151, -11}));

                env.require(balance(carol, carolMPT0 - MPT(ammAlice[0])(1000)));
                env.require(balance(carol, carolMPT1));
            },
            {{AMMMPT(10'000), AMMMPT(10'000)}});

        // Single deposit: 1000 MPT into MPT/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto carolMPT = env.balance(carol, MPT(ammAlice[0]));
                auto carolUSD = env.balance(carol, USD);

                ammAlice.deposit(carol, MPT(ammAlice[0])(1000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(11'000),
                    USD(10'000),
                    IOUAmount{10'488'08848170151, -11}));

                env.require(balance(carol, carolMPT - MPT(ammAlice[0])(1000)));
                env.require(balance(carol, carolUSD));
            },
            {{AMMMPT(10'000), USD(10'000)}});

        // Single deposit: 1000 IOU into MPT/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto carolMPT = env.balance(carol, MPT(ammAlice[0]));
                auto carolUSD = env.balance(carol, USD);

                ammAlice.deposit(carol, USD(1000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000),
                    STAmount{USD, UINT64_C(10999'99999999999), -11},
                    IOUAmount{10'488'08848170151, -11}));

                env.require(balance(carol, carolMPT));
                env.require(balance(
                    carol,
                    carolUSD - STAmount{USD, UINT64_C(999'99999999999), -11}));
            },
            {{AMMMPT(10'000), USD(10'000)}});

        // Single deposit: 100000 tokens worth of MPT into XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, 100'000, MPT(ammAlice[1])(205));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'201),
                    IOUAmount{10'100'000, 0}));

                env.require(balance(carol, carolXRP - drops(baseFee)));
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(201)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit: 100000 tokens worth of XRP into XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(carol, 100'000, XRP(205));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'201),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'100'000, 0}));

                env.require(
                    balance(carol, carolXRP - XRP(201) - drops(baseFee)));
                env.require(balance(carol, carolMPT));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit: 100 tokens worth of MPT/IOU into pool of MPT/IOU
        // combination
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto carolBTC = env.balance(carol, BTC);
                auto carolUSD = env.balance(carol, USD);

                ammAlice.deposit(carol, 100, USD(205));
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(10'000), USD(10'201), IOUAmount{10'100, 0}));

                env.require(balance(carol, carolBTC));
                env.require(balance(carol, carolUSD - USD(201)));
            };
            testHelper2TokensMix(test);
        }

        // Single deposit with EP not exceeding specified:
        // 100 MPT with EP not to exceed 0.1 (AssetIn/TokensOut)
        // for XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 1, -1});

                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10100),
                    IOUAmount{10'049'875'62112089, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit with EP not exceeding specified:
        // 100 MPT with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 2004, -6});

                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'081),
                    IOUAmount{10'039'920'31840891, -8}));

                env.require(balance(carol, carolXRP - drops(baseFee)));
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(81)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit with EP not exceeding specified:
        // 0 MPT with EP not to exceed 0.002004 (AssetIn/TokensOut)
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(0),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 2004, -6});

                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'081),
                    IOUAmount{10'039'920'31840891, -8}));

                env.require(balance(carol, carolXRP - drops(baseFee)));
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(81)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit with EP not exceeding specified:
        // 100 MPT with EP not to exceed 0.1 (AssetIn/TokensOut)
        // for IOU/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 1, -1});

                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'000'000'000),
                    MPT(ammAlice[1])(10100),
                    IOUAmount{10'049'875'62112089, -8}));
            },
            {{USD(10'000'000'000), AMMMPT(10'000)}});

        // Single deposit with EP not exceeding specified:
        // 100 IOU with EP not to exceed 0.1 (AssetIn/TokensOut)
        // for IOU/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol, USD(100), std::nullopt, STAmount{USD, 1, -1});

                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[1])(10'000'000'000),
                    USD(10100),
                    IOUAmount{10'049'875'62112089, -8}));
            },
            {{USD(10'000), AMMMPT(10'000'000'000)}});

        // Single deposit with EP not exceeding specified:
        // 100 IOU with EP not to exceed 0.1 (AssetIn/TokensOut)
        // for MPT/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    MPT(ammAlice[0])(100),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 1, -1});

                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[1])(10'000'000'000),
                    MPT(ammAlice[0])(10100),
                    IOUAmount{10'049'875'62112089, -8}));
            },
            {{AMMMPT(10'000), AMMMPT(10'000'000'000)}});

        // MPT/MPT with transfer fee
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 25'000,
                 .pay = 400'000});

            MPT USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 25'000,
                 .pay = 400'000});

            AMM ammAlice(env, alice, USD(200'000), BTC(5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(200'000), BTC(5), IOUAmount{1000, 0}));

            ammAlice.deposit(carol, 100, std::nullopt, std::nullopt);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(220'000), BTC(6), IOUAmount{1100, 0}));
        }

        // IOU/MPT with transfer fee
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, bob, carol);
            env.close();

            env(rate(gw, 1.25));
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 400'000});

            auto const USD = gw["USD"];
            env.trust(USD(1000000), alice);
            env(pay(gw, alice, USD(1000000)));
            env.trust(USD(1000000), bob);
            env(pay(gw, bob, USD(1000000)));
            env.trust(USD(1000000), carol);
            env(pay(gw, carol, USD(1000000)));
            env.close();

            // IOU/MPT
            AMM ammAlice(env, alice, USD(200'000), BTC(5));
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(200'000), BTC(5), IOUAmount{1000, 0}));
            ammAlice.deposit(carol, 100, std::nullopt, std::nullopt);
            BEAST_EXPECT(ammAlice.expectBalances(
                USD(220'000), BTC(6), IOUAmount{1100, 0}));

            MPT ETH = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 400'000});

            // MPT/IOU
            AMM ammBob(env, bob, ETH(20'000), USD(0.5));
            BEAST_EXPECT(ammBob.expectBalances(
                ETH(20'000), USD(0.5), IOUAmount{100, 0}));
            ammBob.deposit(carol, 10);
            BEAST_EXPECT(ammBob.expectBalances(
                ETH(22'000), USD(0.55), IOUAmount{110, 0}));
        }

        // Tiny deposits for IOU/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // tiny amount causes MPT to deposit rounded to 0
                ammAlice.deposit(
                    carol, IOUAmount{1, -3}, std::nullopt, std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(10'000'001), -3},
                    MPT(ammAlice[1])(10'001),
                    IOUAmount{10'000'001, -3}));

                ammAlice.deposit(carol, IOUAmount{1});
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(10'001'001), -3},
                    MPT(ammAlice[1])(10'003),
                    IOUAmount{10'001'001, -3}));
            },
            {{USD(10'000), AMMMPT(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, STAmount{USD, 1, -10});
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(10'000'00000000008), -11},
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{1'000'000'000000004, -11}));

                ammAlice.deposit(carol, MPT(ammAlice[1])(1));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(10'000'00000000008), -11},
                    MPT(ammAlice[1])(10'001),
                    IOUAmount{10'000'49998750066, -11}));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Tiny deposits for XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, XRPAmount{1});
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{10'000'000'001},
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{1'000'000'000049999, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, MPT(ammAlice[1])(1));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'001),
                    IOUAmount{10'000'499'98750062, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Tiny deposits for MPT/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, MPT(ammAlice[1])(1));

                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000),
                    MPT(ammAlice[1])(10'001),
                    IOUAmount{1'000'049'998750062, -11}));
            },
            {{AMMMPT(10'000), AMMMPT(10'000)}});

        // MPT Issuer create/deposit
        {
            Env env(*this);
            env.fund(XRP(30'000), gw);
            env.close();

            MPT BTC = MPTTester({.env = env, .issuer = gw, .holders = {}});

            AMM ammGw(env, gw, XRP(10'000), BTC(10'000'000'000));
            BEAST_EXPECT(ammGw.expectBalances(
                XRP(10'000), BTC(10'000'000'000), IOUAmount{10'000'000'000}));

            ammGw.deposit(gw, 1'000'000);
            BEAST_EXPECT(ammGw.expectBalances(
                XRP(10'001), BTC(10'001000000), IOUAmount{10'001000000}));

            ammGw.deposit(gw, BTC(1'000000000));
            BEAST_EXPECT(ammGw.expectBalances(
                XRP(10'001),
                BTC(11'001000000),
                IOUAmount{1048'908'961731188, -5}));
        }

        // Issuer deposit in MPT/MPT pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(gw, 1'000'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(1'010'000),
                    MPT(ammAlice[1])(1'010'000),
                    IOUAmount{1'010'000}));

                ammAlice.deposit(gw, MPT(ammAlice[0])(1000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(1'010'999),
                    MPT(ammAlice[1])(1'010'000),
                    IOUAmount{1'010'499'376546071, -9}));
            },
            {{AMMMPT(10'000), AMMMPT(10'000)}});

        // Issuer deposit in MPT/XRP pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(gw, 1'000'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{11'000'000}));
                ammAlice.deposit(gw, MPT(ammAlice[1])(1'000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(12'000),
                    IOUAmount{11'489'125'29307605, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal deposit by tokens MPT/XRP
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    1'000'000,
                    XRP(1'000),
                    MPT(ammAlice[1])(1'000),
                    std::nullopt,
                    tfLPToken,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{11'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal deposit by tokens MPT/IOU combination
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto carolBTC = env.balance(carol, BTC);
                auto carolUSD = env.balance(carol, USD);

                ammAlice.deposit(
                    carol,
                    1'000,
                    USD(1'000),
                    BTC(1'000),
                    std::nullopt,
                    tfLPToken,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(11'000), BTC(11'000), IOUAmount{11'000}));

                env.require(balance(carol, carolBTC - BTC(1000)));
                env.require(balance(carol, carolUSD - USD(1000)));
            };
            testHelper2TokensMix(test);
        }

        // Equal deposit by asset XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    1'000'000,
                    XRP(1'000),
                    MPT(ammAlice[1])(1'000),
                    std::nullopt,
                    tfTwoAsset,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{11'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal deposit by asset IOU/MPT combination
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();
                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto carolBTC = env.balance(carol, BTC);
                auto carolUSD = env.balance(carol, USD);

                ammAlice.deposit(
                    carol,
                    1'000,
                    USD(1'000),
                    BTC(1'000),
                    std::nullopt,
                    tfTwoAsset,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(11'000), BTC(11'000), IOUAmount{11'000, 0}));

                env.require(balance(carol, carolBTC - BTC(1000)));
                env.require(balance(carol, carolUSD - USD(1000)));
            };
            testHelper2TokensMix(test);
        }

        // Single deposit XRP by asset MPT/XRP
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    488'088,
                    XRP(1'000),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'488'088'48170151, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit MPT by asset MPT/XRP
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    488'088,
                    MPT(ammAlice[1])(1'000),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'488'088'48170151, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit IOU by asset MPT/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    488,
                    USD(1'000),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(10'999'99999999999), -11},
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'488'08848170151, -11}));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Single deposit MPT by asset MPT/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    488,
                    MPT(ammAlice[1])(1'000),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'488'088'48170151, -11}));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Single deposit MPT by asset MPT/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(
                    carol,
                    488,
                    MPT(ammAlice[1])(1'000),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    std::nullopt,
                    std::nullopt);
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'488'088'48170151, -11}));
            },
            {{AMMMPT(10'000), AMMMPT(10'000)}});
    }

    void
    testInvalidWithdraw()
    {
        testcase("Invalid AMMWithdraw");

        using namespace jtx;
        auto const all = testable_amendments();

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .asset1Out = XRP(100),
                    .err = ter(tecAMM_BALANCE),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(99), AMMMPT(99)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .asset1Out = MPT(ammAlice[1])(100),
                    .err = ter(tecAMM_BALANCE),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(99), AMMMPT(99)}});

        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice, bob);
            env.close();
            // alice is authorized to hold gw MPT, bob is not authorized
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTRequireAuth | MPTDEXFlags,
                 .authHolder = true});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            WithdrawArg args{
                .account = bob,
                .asset1Out = BTC(100),
                .err = ter(tecNO_AUTH),
            };
            ammAlice.withdraw(args);
        }

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                MPTTester BTC(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol},
                     .pay = 2'000,
                     .flags = tfMPTCanLock | MPTDEXFlags});

                // Invalid tokens
                ammAlice.withdraw(
                    alice,
                    0,
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));
                ammAlice.withdraw(
                    alice,
                    IOUAmount{-1},
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // Mismatched token, invalid Asset1Out issue
                ammAlice.withdraw(
                    alice,
                    BTC(100),
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));
                ammAlice.withdraw(
                    alice,
                    MPT(badMPT())(100),
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_MPT));

                // Mismatched token, invalid Asset2Out issue
                ammAlice.withdraw(
                    alice,
                    XRP(100),
                    BTC(100),
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // Mismatched token, Asset1Out.issue == Asset2Out.issue
                ammAlice.withdraw(
                    alice,
                    MPT(ammAlice[1])(100),
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    ter(temBAD_AMM_TOKENS));

                // Invalid amount value
                ammAlice.withdraw(
                    alice,
                    MPT(ammAlice[1])(0),
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
                ammAlice.withdraw(
                    alice,
                    MPT(ammAlice[1])(-100),
                    std::nullopt,
                    std::nullopt,
                    ter(temBAD_AMOUNT));
                ammAlice.withdraw(
                    alice,
                    MPT(ammAlice[1])(10),
                    std::nullopt,
                    IOUAmount{-1},
                    ter(temBAD_AMOUNT));

                // Invalid amount/token value, withdraw all tokens from one side
                // of the pool.
                ammAlice.withdraw(
                    alice,
                    MPT(ammAlice[1])(10'000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_BALANCE));
                ammAlice.withdraw(
                    alice,
                    XRP(10'000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_BALANCE));
                ammAlice.withdraw(
                    alice,
                    std::nullopt,
                    MPT(ammAlice[1])(0),
                    std::nullopt,
                    std::nullopt,
                    tfOneAssetWithdrawAll,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_BALANCE));

                // Bad MPT
                ammAlice.withdraw(
                    alice,
                    XRP(100),
                    MPT(badMPT())(100),
                    std::nullopt,
                    ter(temBAD_MPT));

                // Invalid Account
                Account bad("bad");
                env.memoize(bad);
                ammAlice.withdraw(
                    bad,
                    1'000'000,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    seq(1),
                    ter(terNO_ACCOUNT));

                // Invalid AMM
                ammAlice.withdraw(
                    alice,
                    1'000,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    {{MPT(ammAlice[1]), GBP}},
                    std::nullopt,
                    ter(terNO_AMM));

                // Carol is not a Liquidity Provider
                ammAlice.withdraw(
                    carol,
                    10'000,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_BALANCE));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Withdraw entire one side of the pool.
                // Pre-fixAMMv1_3:
                // Equal withdraw but due to MPT precision limit,
                // this results in full withdraw of MPT pool only,
                // while leaving a tiny amount in USD pool.
                // Post-fixAMMv1_3:
                // Most of the pool is withdrawn with remaining tiny amounts
                auto err = env.enabled(fixAMMv1_3) ? ter(tesSUCCESS)
                                                   : ter(tecAMM_BALANCE);
                ammAlice.withdraw(
                    alice,
                    IOUAmount{9'999'999'9999, -4},
                    std::nullopt,
                    std::nullopt,
                    err);
                if (env.enabled(fixAMMv1_3))
                    BEAST_EXPECT(ammAlice.expectBalances(
                        MPT(ammAlice[0])(1),
                        STAmount{USD, 1, -7},
                        IOUAmount{1, -4}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}},
            0,
            std::nullopt,
            {all, all - fixAMMv1_3});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Similar to above with even smaller remaining amount
                // Pre-fixAMMv1_3: results in full withdraw of MPT pool only,
                // returning tecAMM_BALANCE. Post-fixAMMv1_3: most of the pool
                // is withdrawn with remaining tiny amounts
                auto err = env.enabled(fixAMMv1_3) ? ter(tesSUCCESS)
                                                   : ter(tecAMM_BALANCE);
                ammAlice.withdraw(
                    alice,
                    IOUAmount{9'999'999'999999999, -9},
                    std::nullopt,
                    std::nullopt,
                    err);
                if (env.enabled(fixAMMv1_3))
                    BEAST_EXPECT(ammAlice.expectBalances(
                        MPT(ammAlice[0])(1),
                        STAmount{USD, 1, -11},
                        IOUAmount{1, -8}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}},
            0,
            std::nullopt,
            {all, all - fixAMMv1_3});

        // Invalid AMM
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.withdrawAll(alice);
                ammAlice.withdraw(
                    alice, 10'000, std::nullopt, std::nullopt, ter(terNO_AMM));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Globally locked MPT
        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            env.close();
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags,
                 .authHolder = true});

            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            BTC.set({.flags = tfMPTLock});

            ammAlice.withdraw(
                alice,
                MPT(ammAlice[1])(100),
                std::nullopt,
                std::nullopt,
                ter(tecFROZEN));
            ammAlice.withdraw(
                alice, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));

            // can single withdraw the other asset
            ammAlice.withdraw({.account = alice, .asset1Out = XRP(100)});
        }

        // Individually frozen (AMM) account with MPT/MPT AMM
        {
            Env env{*this};
            env.fund(XRP(10'000), gw, alice);
            env.close();
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 40'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));

            // Alice's BTC is locked
            BTC.set({.holder = alice, .flags = tfMPTLock});
            ammAlice.withdraw(
                alice, 1000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, BTC(100), std::nullopt, std::nullopt, ter(tecFROZEN));

            // can withdraw the other asset
            ammAlice.withdraw(alice, USD(100), std::nullopt, std::nullopt);

            // Unlock and then alice can withdraw
            BTC.set({.holder = alice, .flags = tfMPTUnlock});
            ammAlice.withdraw(alice, 1000, std::nullopt, std::nullopt);
            ammAlice.withdraw(alice, BTC(100), std::nullopt, std::nullopt);
            ammAlice.withdraw(alice, USD(100), std::nullopt, std::nullopt);
        }

        // Individually lock MPT or freeze IOU (AMM)
        {
            Env env{*this};
            fund(env, gw, {alice}, {USD(20'000)}, Fund::All);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, USD(10'000), BTC(10'000));

            // Alice's BTC is locked
            BTC.set({.holder = alice, .flags = tfMPTLock});

            ammAlice.withdraw(
                alice, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, BTC(100), std::nullopt, std::nullopt, ter(tecFROZEN));
            // can still single withdraw the unlocked other asset
            ammAlice.withdraw(alice, USD(100), std::nullopt, std::nullopt);

            // Unlock alice's BTC
            BTC.set({.holder = alice, .flags = tfMPTUnlock});

            // Now alice can withdraw
            ammAlice.withdraw(alice, USD(100), std::nullopt, std::nullopt);
            ammAlice.withdraw(alice, 1'000, std::nullopt, std::nullopt);
            ammAlice.withdraw(alice, BTC(100), std::nullopt, std::nullopt);

            // Individually lock MPT BTC (AMM) account
            BTC.set({.holder = ammAlice.ammAccount(), .flags = tfMPTLock});

            // Can withdraw non-frozen token USD
            ammAlice.withdraw(alice, USD(100), std::nullopt, std::nullopt);

            // Can not withdraw locked token BTC
            ammAlice.withdraw(
                alice, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, BTC(100), std::nullopt, std::nullopt, ter(tecFROZEN));

            // Unlock AMM MPT
            BTC.set({.holder = ammAlice.ammAccount(), .flags = tfMPTUnlock});
            // Can withdraw
            ammAlice.withdraw(alice, 1'000, std::nullopt, std::nullopt);
            ammAlice.withdraw(alice, BTC(100), std::nullopt, std::nullopt);

            // Individually frozen AMM
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfSetFreeze));
            env.close();

            // Can withdraw non-locked token BTC
            ammAlice.withdraw(alice, BTC(100), std::nullopt, std::nullopt);

            // Can not withdraw frozen token USD
            ammAlice.withdraw(
                alice, 1'000, std::nullopt, std::nullopt, ter(tecFROZEN));
            ammAlice.withdraw(
                alice, USD(100), std::nullopt, std::nullopt, ter(tecFROZEN));

            // Unfreeze
            env(trust(
                gw,
                STAmount{Issue{gw["USD"].currency, ammAlice.ammAccount()}, 0},
                tfClearFreeze));
            env.close();

            // Can withdraw
            ammAlice.withdraw(alice, 1'000, std::nullopt, std::nullopt);
            ammAlice.withdraw(alice, USD(100), std::nullopt, std::nullopt);
        }

        // Carol withdraws more than she owns
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // Single deposit of 100000 worth of tokens
                // which is 10% of the pool. Carol is LP now.
                ammAlice.deposit(carol, 1'000'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{11'000'000, 0}));

                ammAlice.withdraw(
                    carol,
                    2'000'000,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{11'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are 0.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000);
                auto const err = env.enabled(fixAMMv1_3)
                    ? ter(tecAMM_INVALID_TOKENS)
                    : ter(tecAMM_FAILED);
                ammAlice.withdraw(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    IOUAmount{500, 0},
                    err);
            },
            {{XRP(10'000), AMMMPT(10'000)}},
            0,
            std::nullopt,
            {all, all - fixAMMv1_3});

        // Withdraw with EPrice limit. Fails to withdraw, calculated tokens
        // to withdraw are greater than the LP shares.
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, 1'000'000);
                ammAlice.withdraw(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    IOUAmount{600, 0},
                    ter(tecAMM_INVALID_TOKENS));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Withdraw with EPrice limit. Fails to withdraw, amount1
        // to withdraw is less than 1700 MPT.
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, 1'000'000);
                ammAlice.withdraw(
                    carol,
                    MPT(ammAlice[1])(1'700),
                    std::nullopt,
                    IOUAmount{520, 0},
                    ter(tecAMM_FAILED));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Deposit/Withdraw the same amount with the trading fee
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                ammAlice.withdraw(
                    carol,
                    MPT(ammAlice[1])(1'000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
            },
            {{XRP(10'000), AMMMPT(10'000)}},
            1'000);
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, XRP(1'000));
                ammAlice.withdraw(
                    carol,
                    XRP(1'000),
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
            },
            {{XRP(10'000), AMMMPT(10'000)}},
            1'000);

        // Deposit/Withdraw the same amount fails due to the tokens adjustment
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, STAmount{USD, 1, -6});
                ammAlice.withdraw(
                    carol,
                    STAmount{USD, 1, -6},
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});

        // Withdraw close to one side of the pool. Account's LP tokens
        // are rounded to all LP tokens.
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(
                    alice,
                    STAmount{
                        MPT(ammAlice[1]), UINT64_C(9'999'999999999999), -12},
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_BALANCE));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Tiny withdraw
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // XRP amount to withdraw is 0
                ammAlice.withdraw(
                    alice,
                    IOUAmount{1, -5},
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_FAILED));
                // Calculated tokens to withdraw are 0
                ammAlice.withdraw(
                    alice,
                    std::nullopt,
                    STAmount{USD, 1, -11},
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
                ammAlice.deposit(carol, STAmount{USD, 1, -10});
                ammAlice.withdraw(
                    carol,
                    std::nullopt,
                    STAmount{USD, 1, -9},
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
                ammAlice.withdraw(
                    carol,
                    std::nullopt,
                    MPT(ammAlice[0])(1),
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
    }

    void
    testWithdraw()
    {
        testcase("Withdraw");

        using namespace jtx;

        // Equal withdrawal by Carol: 1'000'000 of tokens, 10% of the current
        // pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // XRP/MPT
                XRPAmount const baseFee{env.current()->fees().base};
                auto carolXRP = env.balance(carol, XRP);
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));

                // Single deposit of 1'000'000 worth of tokens,
                // which is 10% of the pool. Carol is LP now.
                ammAlice.deposit(carol, 1'000'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{11'000'000, 0}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount{1'000'000, 0}));
                env.require(balance(carol, carolMPT - MPT(ammAlice[1])(1'000)));
                env.require(
                    balance(carol, carolXRP - XRP(1'000) - drops(baseFee)));

                // Carol withdraws all tokens
                ammAlice.withdraw(carol, 1'000'000);
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
                env.require(balance(carol, carolMPT));
                env.require(balance(carol, carolXRP - drops(2 * baseFee)));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal withdrawal by tokens 1000000, 10%
        // of the current pool, XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // XRP/MPT
                ammAlice.withdraw(alice, 1'000'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(9'000),
                    MPT(ammAlice[1])(9'000),
                    IOUAmount{9'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal withdrawal by tokens, 10% of the current pool, IOU/MPT
        // combination
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, gw);
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
                     .issuer = gw,
                     .holders = {alice},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env.close();
                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto aliceBTC = env.balance(alice, BTC);
                auto aliceUSD = env.balance(alice, USD);
                ammAlice.withdraw(alice, 1'000);
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(9'000), USD(9'000), IOUAmount(9'000)));
                env.require(balance(alice, aliceBTC + BTC(1000)));
                env.require(balance(alice, aliceUSD + USD(1000)));
            };
            testHelper2TokensMix(test);
        }

        // Equal withdrawal with a limit. Withdraw XRP200.
        // If proportional withdraw of MPT is less than 100
        // then withdraw that amount, otherwise withdraw MPT100
        // and proportionally withdraw XRP. It's the latter
        // in this case - XRP100/MPT100.
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // XRP/MPT
                ammAlice.withdraw(alice, XRP(200), MPT(ammAlice[1])(100));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(9'900),
                    MPT(ammAlice[1])(9'900),
                    IOUAmount{9'900'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal withdrawal with a limit. XRP100/MPT200 truncated to
        // XRP100/MPT100
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // XRP/MPT
                ammAlice.withdraw(alice, XRP(100), MPT(ammAlice[1])(200));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(9'900),
                    MPT(ammAlice[1])(9'900),
                    IOUAmount{9'900'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal withdrawal with a limit. IOU/MPT combination.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, gw);
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
                     .issuer = gw,
                     .holders = {alice},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env.close();
                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto aliceBTC = env.balance(alice, BTC);
                auto aliceUSD = env.balance(alice, USD);
                ammAlice.withdraw(alice, BTC(200), USD(100));
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(9'900), USD(9'900), IOUAmount(9'900)));
                env.require(balance(alice, aliceBTC + BTC(100)));
                env.require(balance(alice, aliceUSD + USD(100)));
            };
            testHelper2TokensMix(test);
        }

        // Single withdrawal by amount
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // single withdraw XRP from XRP/MPT
                ammAlice.withdraw(alice, XRP(1'000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(9000'000001),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{9'486'832'98050514, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // single withdraw MPT from XRP/MPT
                ammAlice.withdraw(alice, MPT(ammAlice[1])(1'000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10000),
                    MPT(ammAlice[1])(9001),
                    IOUAmount{9'486'832'98050514, -8}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // single withdraw IOU from IOU/MPT
                ammAlice.withdraw(alice, USD(1'000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(9000'000000000004), -12},
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{9486'83298050514, -11}));
            },
            {{USD(10'000), AMMMPT(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // single withdraw MPT from IOU/MPT
                ammAlice.withdraw(alice, MPT(ammAlice[1])(1'000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'000),
                    MPT(ammAlice[1])(9001),
                    IOUAmount{9486'83298050514, -11}));
            },
            {{USD(10'000), AMMMPT(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // single withdraw MPT from MPT/MPT
                ammAlice.withdraw(alice, MPT(ammAlice[0])(1'000));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(9001),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{9486'83298050514, -11}));
            },
            {{AMMMPT(10'000), AMMMPT(10'000)}});

        // Single withdrawal MPT by tokens 10000. XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, 10'000, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(9981),
                    IOUAmount{9'990'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single withdrawal XRP by tokens 10000. XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, 10'000, XRP(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(9980010000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{9'990'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single withdrawal by tokens 10000. MPT/IOU combination.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, gw);
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
                     .issuer = gw,
                     .holders = {alice},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env.close();
                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto aliceBTC = env.balance(alice, BTC);
                auto aliceUSD = env.balance(alice, USD);
                ammAlice.withdraw(alice, 1000, BTC(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'000), BTC(8100), IOUAmount{9000, 0}));
                env.require(balance(alice, aliceBTC + BTC(1900)));
                env.require(balance(alice, aliceUSD));
            };
            testHelper2TokensMix(test);
        }

        // Withdraw all tokens.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(trust(carol, STAmount{ammAlice.lptIssue(), 10'000}));
                // Can SetTrust only for AMM LP tokens
                env(trust(
                        carol,
                        STAmount{
                            Issue{EUR.currency, ammAlice.ammAccount()},
                            10'000}),
                    ter(tecNO_PERMISSION));
                env.close();
                ammAlice.withdrawAll(alice);
                BEAST_EXPECT(!ammAlice.ammExists());

                BEAST_EXPECT(!env.le(keylet::ownerDir(ammAlice.ammAccount())));

                // Can create AMM for the XRP/MPT pair
                AMM ammCarol(env, carol, XRP(10'000), MPT(ammAlice[1])(10'000));
                BEAST_EXPECT(ammCarol.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit 1000MPT, withdraw all tokens in MPT from XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'001),
                    IOUAmount{10'000'000, 0}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit 1000MPT, withdraw all tokens in XRP from XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                ammAlice.withdrawAll(carol, XRP(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(9'090'909'091),
                    MPT(ammAlice[1])(11000),
                    IOUAmount{10'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit 1000MPT, withdraw all tokens in MPT from USD/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // USD/MPT
                ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'000),
                    MPT(ammAlice[1])(10'001),
                    IOUAmount{10'000, 0}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Single deposit 1000USD, withdraw all tokens in USD from USD/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // USD/MPT
                ammAlice.deposit(carol, USD(1'000));
                ammAlice.withdrawAll(carol, USD(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'000, 0}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Single deposit 1000MPT, withdraw all tokens in MPT from MPT/MPT
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // MPT/MPT
                ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000),
                    MPT(ammAlice[1])(10'001),
                    IOUAmount{10'000, 0}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount(beast::Zero())));
            },
            {{AMMMPT(10'000), AMMMPT(10'000)}});

        // Single deposit 1000MPT, withdraw all tokens in MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10001),
                    IOUAmount{10'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit 1000MPT, withdraw all tokens in USD
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                ammAlice.withdrawAll(carol, USD(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(9'090'9090909091), -10},
                    MPT(ammAlice[1])(11000),
                    IOUAmount{10'000, 0}));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Single deposit 1000USD, withdraw all tokens in MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, USD(1'000));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(10'999'99999999999), -11},
                    MPT(ammAlice[1])(9091),
                    IOUAmount{10'000, 0}));
            },
            {{USD(10'000), AMMMPT(10'000)}});

        // Single deposit/withdraw by the same account
        testAMM(
            [&](AMM& ammAlice, Env&) {
                auto lpTokens = ammAlice.deposit(carol, USD(1'000));
                ammAlice.withdraw(carol, lpTokens, USD(0));
                lpTokens = ammAlice.deposit(carol, STAmount(USD, 1, -6));
                ammAlice.withdraw(carol, lpTokens, USD(0));
                lpTokens = ammAlice.deposit(carol, MPT(ammAlice[0])(1));
                ammAlice.withdraw(carol, lpTokens, MPT(ammAlice[0])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000'000'001),
                    USD(10'000),
                    ammAlice.tokens()));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                auto const& BTC = MPT(ammAlice[1]);
                auto lpTokens = ammAlice.deposit(carol, BTC(1'000));
                ammAlice.withdraw(carol, lpTokens, BTC(0));
                lpTokens = ammAlice.deposit(carol, BTC(1));
                ammAlice.withdraw(carol, lpTokens, BTC(0));
                lpTokens = ammAlice.deposit(carol, BTC(1));
                ammAlice.withdraw(carol, lpTokens, BTC(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(10'003), XRP(10'000), ammAlice.tokens()));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Single deposit by different accounts and then withdraw
        // in reverse.
        testAMM(
            [&](AMM& ammAlice, Env&) {
                auto const carolTokens =
                    ammAlice.deposit(carol, MPT(ammAlice[1])(1'000));
                auto const aliceTokens =
                    ammAlice.deposit(alice, MPT(ammAlice[1])(1'000));
                ammAlice.withdraw(alice, aliceTokens, MPT(ammAlice[1])(0));
                ammAlice.withdraw(carol, carolTokens, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000), MPT(ammAlice[1])(10'001), ammAlice.tokens()));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
                BEAST_EXPECT(ammAlice.expectLPTokens(alice, ammAlice.tokens()));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal deposit 10%, withdraw all tokens. XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, 1'000'000);
                ammAlice.withdrawAll(carol);
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
        // Equal deposit 10%, withdraw all tokens. IOU/MPT combination.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, carol, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 1'000'000});
                env(pay(gw, alice, BTC(50000)));
                env(pay(gw, carol, BTC(50000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();
                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                auto carolBTC = env.balance(carol, BTC);
                auto carolUSD = env.balance(carol, USD);
                ammAlice.deposit(carol, 1'000);
                ammAlice.withdrawAll(carol);
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'000), BTC(10'000), IOUAmount{10'000, 0}));
                env.require(balance(carol, carolBTC));
                env.require(balance(carol, carolUSD));
            };
            testHelper2TokensMix(test);
        }

        // Equal deposit 10%, withdraw all tokens in MPT from XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, 1'000'000);
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    STAmount{
                        MPT(ammAlice[1]), UINT64_C(9'090'909090909092), -12},
                    IOUAmount{10'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal deposit 10%, withdraw all tokens in XRP from XRP/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, 1'000'000);
                ammAlice.withdrawAll(carol, XRP(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount(9'090'909'091),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'000'000, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Equal deposit 10%, withdraw all tokens in USD from USD/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, 1'000);
                ammAlice.withdrawAll(carol, USD(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    STAmount{USD, UINT64_C(9'090'909090909092), -12},
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'000}));
            },
            {{USD(10'000), AMMMPT(10'000)}});
        // Equal deposit 10%, withdraw all tokens in MPT from MPT/MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.deposit(carol, 1'000);
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(11'000),
                    MPT(ammAlice[1])(9'091),
                    IOUAmount{10'000}));
            },
            {{AMMMPT(10'000), AMMMPT(10'000)}});

        auto const all = testable_amendments();

        // Withdraw with EPrice limit.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000'000'000);
                ammAlice.withdraw(
                    carol,
                    MPT(ammAlice[1])(100'000000),
                    std::nullopt,
                    IOUAmount{520, 0});
                if (!env.enabled(fixAMMv1_1) && !env.enabled(fixAMMv1_3))
                {
                    BEAST_EXPECT(
                        ammAlice.expectBalances(
                            XRP(11'000'000000),
                            MPT(ammAlice[1])(9372781065),
                            IOUAmount{10'153'846'15384616, -2}) &&
                        ammAlice.expectLPTokens(
                            carol, IOUAmount{153'846'15384616, -2}));
                }
                else if (env.enabled(fixAMMv1_1) && !env.enabled(fixAMMv1_3))
                {
                    BEAST_EXPECT(
                        ammAlice.expectBalances(
                            XRP(11'000'000000),
                            MPT(ammAlice[1])(9372781065),
                            IOUAmount{10'153'846'15384616, -2}) &&
                        ammAlice.expectLPTokens(
                            carol, IOUAmount{153'846'15384616, -2}));
                }
                else if (env.enabled(fixAMMv1_3))
                {
                    BEAST_EXPECT(
                        ammAlice.expectBalances(
                            XRP(11'000'000000),
                            MPT(ammAlice[1])(9372781066),
                            IOUAmount{10'153'846'15384616, -2}) &&
                        ammAlice.expectLPTokens(
                            carol, IOUAmount{153'846'15384616, -2}));
                }
                ammAlice.withdrawAll(carol);
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
            },
            {{XRP(10'000'000'000), AMMMPT(10'000'000'000)}},
            0,
            std::nullopt,
            {all, all - fixAMMv1_3, all - fixAMMv1_1 - fixAMMv1_3});

        // Withdraw with EPrice limit. AssetOut is 0.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000'000'000);
                ammAlice.withdraw(
                    carol,
                    MPT(ammAlice[1])(0),
                    std::nullopt,
                    IOUAmount{520, 0});
                if (!env.enabled(fixAMMv1_1) && !env.enabled(fixAMMv1_3))
                {
                    BEAST_EXPECT(
                        ammAlice.expectBalances(
                            XRP(11'000'000000),
                            MPT(ammAlice[1])(9372781065),
                            IOUAmount{10'153'846'15384616, -2}) &&
                        ammAlice.expectLPTokens(
                            carol, IOUAmount{153'846'15384616, -2}));
                }
                else if (env.enabled(fixAMMv1_1) && !env.enabled(fixAMMv1_3))
                {
                    BEAST_EXPECT(
                        ammAlice.expectBalances(
                            XRP(11'000'000000),
                            MPT(ammAlice[1])(9372781065),
                            IOUAmount{10'153'846'15384616, -2}) &&
                        ammAlice.expectLPTokens(
                            carol, IOUAmount{153'846'15384616, -2}));
                }
                else if (env.enabled(fixAMMv1_3))
                {
                    BEAST_EXPECT(
                        ammAlice.expectBalances(
                            XRP(11'000'000000),
                            MPT(ammAlice[1])(9372781066),
                            IOUAmount{10'153'846'15384616, -2}) &&
                        ammAlice.expectLPTokens(
                            carol, IOUAmount{153'846'15384616, -2}));
                }
                ammAlice.withdrawAll(carol);
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
            },
            {{XRP(10'000'000'000), AMMMPT(10'000'000'000)}},
            0,
            std::nullopt,
            {all, all - fixAMMv1_3, all - fixAMMv1_1 - fixAMMv1_3});

        // IOU/MPT combination + transfer fee
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
                     .limit = 1'000'000,
                     .transferFee = 25'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000'000,
                     .transferFee = 25'000});
                env(pay(gw, alice, BTC(10000)));
                env(pay(gw, bob, BTC(10000)));
                env(pay(gw, carol, BTC(10000)));
                env(pay(gw, alice, USD(10000)));
                env(pay(gw, bob, USD(10000)));
                env(pay(gw, carol, USD(10000)));
                env.close();
                // no transfer fee on create
                AMM ammAlice(env, alice, BTC(2'000), USD(5));
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(2'000), USD(5), IOUAmount{100, 0}));
                env.require(balance(alice, BTC(8000)));
                env.require(balance(alice, USD(9995)));

                // no transfer fee on deposit
                ammAlice.deposit(carol, 100);
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(4000), USD(10), IOUAmount{200, 0}));
                env.require(balance(carol, BTC(8000)));
                env.require(balance(carol, USD(9995)));

                // no transfer fee on withdraw
                ammAlice.withdraw(carol, 100);
                BEAST_EXPECT(ammAlice.expectBalances(
                    BTC(2'000), USD(5), IOUAmount{100, 0}));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0, 0}));
                env.require(balance(carol, BTC(10000)));
                env.require(balance(carol, USD(10000)));
            };
            testHelper2TokensMix(test);
        }

        // Tiny withdraw
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // By tokens
                ammAlice.withdraw(alice, IOUAmount{1, -3});
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(9'999'999'999),
                    STAmount{USD, UINT64_C(9'999'999999), -6},
                    IOUAmount{9'999'999'999, -3}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // Single withdraw MPT from MPT/IOU
                ammAlice.withdraw(alice, std::nullopt, MPT(ammAlice[0])(1));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10000'000000),
                    USD(10'000),
                    IOUAmount{9'999'999'9995, -4}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // Single withdraw IOU from MPT/IOU
                ammAlice.withdraw(alice, std::nullopt, STAmount{USD, 1, -10});
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000'000'000),
                    STAmount{USD, UINT64_C(9'999'9999999999), -10},
                    IOUAmount{9'999'999'99999995, -8}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                // Single withdraw XRP from MPT/XRP
                ammAlice.withdraw(alice, std::nullopt, XRPAmount(1));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[1])(10'000),
                    XRP(10'000),
                    IOUAmount{9999999'9995, -4}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Withdraw close to entire pool
        // Equal by tokens
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, IOUAmount{9'999'999'999, -3});
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(1),
                    STAmount{USD, 1, -6},
                    IOUAmount{1, -3}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        // USD by tokens
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, IOUAmount{9'999'999}, USD(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000'000'000),
                    STAmount{USD, 1, -10},
                    IOUAmount{1}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        // MPT by tokens
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(
                    alice, IOUAmount{9'999'900}, MPT(ammAlice[0])(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(1), USD(10'000), IOUAmount{100}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        // XRP by tokens
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, IOUAmount{9'999'900}, XRP(0));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[1])(10000), XRPAmount(1), IOUAmount{100}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
        // USD
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(
                    alice, STAmount{USD, UINT64_C(9'999'99999999999), -11});
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000'000'000),
                    STAmount{USD, 1, -11},
                    IOUAmount{316227765, -9}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        // XRP
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, XRPAmount{9'999'999'999});
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[1])(10000), XRPAmount(1), IOUAmount{100}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
        // MPT
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, MPT(ammAlice[0])(9'999'999'999));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(1), USD(10'000), IOUAmount{100}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});
        testAMM(
            [&](AMM& ammAlice, Env&) {
                ammAlice.withdraw(alice, MPT(ammAlice[1])(9'999));
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[1])(1), XRP(10'000), IOUAmount{100000}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
    }

    void
    testInvalidFeeVote()
    {
        testcase("Invalid Fee Vote");
        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Invalid Account
                Account bad("bad");
                env.memoize(bad);
                ammAlice.vote(
                    bad,
                    1'000,
                    std::nullopt,
                    seq(1),
                    std::nullopt,
                    ter(terNO_ACCOUNT));

                // Invalid AMM
                ammAlice.vote(
                    alice,
                    1'000,
                    std::nullopt,
                    std::nullopt,
                    {{MPT(ammAlice[1]), GBP}},
                    ter(terNO_AMM));

                // Account is not LP
                ammAlice.vote(
                    carol,
                    1'000,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(tecAMM_INVALID_TOKENS));

                // Invalid asset pair
                ammAlice.vote(
                    alice,
                    1'000,
                    std::nullopt,
                    std::nullopt,
                    {{MPT(ammAlice[1]), MPT(ammAlice[1])}},
                    ter(temBAD_AMM_TOKENS));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Invalid AMM
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.withdrawAll(alice);
                ammAlice.vote(
                    alice,
                    1'000,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    ter(terNO_AMM));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
    }

    void
    testFeeVote()
    {
        testcase("Fee Vote");
        using namespace jtx;

        // One vote sets fee to 1%.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{0}));
                ammAlice.vote({}, 1'000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
                // Discounted fee is 1/10 of trading fee.
                BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, IOUAmount{0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        auto vote = [&](AMM& ammAlice,
                        Env& env,
                        int i,
                        std::uint32_t tokens = 10'000'000,
                        std::vector<Account>* accounts = nullptr) {
            Account a(std::to_string(i));
            ammAlice.deposit(a, tokens);
            ammAlice.vote(a, 50 * (i + 1));
            if (accounts)
                accounts->push_back(std::move(a));
        };

        {
            // Eight votes fill all voting slots, set fee 0.175%.
            // New vote, same account, sets fee 0.225%
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            std::vector<Account> holders = {alice};
            for (int i = 0; i <= 7; ++i)
            {
                Account a(std::to_string(i));
                holders.push_back(a);
                env.fund(XRP(30'000), a);
            }
            env.close();
            // create MPT and pay 30'000 to all the accounts
            MPTTester BTC(
                {.env = env, .issuer = gw, .holders = holders, .pay = 30'000});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            Account const a("0");
            ammAlice.vote(a, 450);
            BEAST_EXPECT(ammAlice.expectTradingFee(225));
        }

        {
            // Eight votes fill all voting slots, set fee 0.175%.
            // New vote, new account, higher vote weight, set higher fee 0.244%
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            std::vector<Account> holders = {alice};
            for (int i = 0; i < 8; ++i)
            {
                Account a(std::to_string(i));
                holders.push_back(a);
                env.fund(XRP(30'000), a);
            }
            env.close();
            MPTTester BTC(
                {.env = env, .issuer = gw, .holders = holders, .pay = 30'000});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            vote(ammAlice, env, 7, 20'000'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(244));
        }

        {
            // Eight votes fill all voting slots, set fee 0.219%.
            // New vote, new account, higher vote weight, set smaller fee 0.206%
            Env env{*this};
            env.fund(XRP(30'000), gw, alice);
            std::vector<Account> holders = {alice};
            for (int i = 0; i < 8; ++i)
            {
                Account a(std::to_string(i));
                holders.push_back(a);
                env.fund(XRP(30'000), a);
            }
            env.close();
            MPTTester BTC(
                {.env = env, .issuer = gw, .holders = holders, .pay = 30'000});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            for (int i = 7; i > 0; --i)
                vote(ammAlice, env, i);
            BEAST_EXPECT(ammAlice.expectTradingFee(219));
            vote(ammAlice, env, 0, 20'000'000);
            BEAST_EXPECT(ammAlice.expectTradingFee(206));
        }

        {
            // Eight votes fill all voting slots. The accounts then withdraw all
            // tokens. An account sets a new fee and the previous slots are
            // deleted.
            Env env{*this};
            env.fund(XRP(30'000), gw, alice, carol);
            std::vector<Account> holders = {alice, carol};
            for (int i = 0; i < 7; ++i)
            {
                Account a(std::to_string(i));
                holders.push_back(a);
                env.fund(XRP(30'000), a);
            }
            env.close();
            MPTTester BTC(
                {.env = env, .issuer = gw, .holders = holders, .pay = 30'000});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            std::vector<Account> accounts;
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 10'000'000, &accounts);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            for (int i = 0; i < 7; ++i)
                ammAlice.withdrawAll(accounts[i]);
            ammAlice.deposit(carol, 10'000'000);
            ammAlice.vote(carol, 1'000);
            // The initial LP set the fee to 1000. Carol gets 50% voting
            // power, and the new fee is 500.
            BEAST_EXPECT(ammAlice.expectTradingFee(500));
        }
        {
            // Eight votes fill all voting slots. The accounts then withdraw
            // some tokens. The new vote doesn't get the voting power but the
            // slots are refreshed and the fee is updated.
            Env env{*this};
            env.fund(XRP(30'000), gw, alice, carol);
            std::vector<Account> holders = {alice, carol};
            for (int i = 0; i < 7; ++i)
            {
                Account a(std::to_string(i));
                holders.push_back(a);
                env.fund(XRP(30'000), a);
            }
            env.close();
            MPTTester BTC(
                {.env = env, .issuer = gw, .holders = holders, .pay = 30'000});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            std::vector<Account> accounts;
            for (int i = 0; i < 7; ++i)
                vote(ammAlice, env, i, 10'000'000, &accounts);
            BEAST_EXPECT(ammAlice.expectTradingFee(175));
            for (int i = 0; i < 7; ++i)
                ammAlice.withdraw(accounts[i], 9'000'000);
            ammAlice.deposit(carol, 1'000);
            // The vote is not added to the slots
            ammAlice.vote(carol, 1'000);
            auto const info = ammAlice.ammRpcInfo()[jss::amm][jss::vote_slots];
            for (std::uint16_t i = 0; i < info.size(); ++i)
                BEAST_EXPECT(info[i][jss::account] != carol.human());
            // But the slots are refreshed and the fee is changed
            BEAST_EXPECT(ammAlice.expectTradingFee(82));
        }
    }

    void
    testInvalidBid()
    {
        testcase("Invalid Bid");
        using namespace jtx;
        using namespace std::chrono;

        // burn all the LPTokens through a AMMBid transaction
        {
            Env env(*this);
            env.fund(XRP(2'000), gw, alice);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 2'000,
                 .flags = MPTDEXFlags});
            AMM amm(env, gw, XRP(1'000), BTC(1'000), false, 1'000);

            // auction slot is owned by the creator of the AMM i.e. gw
            BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{0}));

            // gw attempts to burn all her LPTokens through a bid transaction
            // this transaction fails because AMMBid transaction can not burn
            // all the outstanding LPTokens
            env(amm.bid({
                    .account = gw,
                    .bidMin = 1'000'000,
                }),
                ter(tecAMM_INVALID_TOKENS));
        }

        // burn all the LPTokens through a AMMBid transaction
        {
            Env env(*this);
            env.fund(XRP(2'000), gw, alice);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 2'000,
                 .flags = MPTDEXFlags});
            AMM amm(env, gw, XRP(1'000), BTC(1'000), false, 1'000);

            // auction slot is owned by the creator of the AMM i.e. gw
            BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{0}));

            // gw burns all but one of its LPTokens through a bid transaction
            // this transaction suceeds because the bid price is less than
            // the total outstanding LPToken balance
            env(amm.bid({
                    .account = gw,
                    .bidMin = STAmount{amm.lptIssue(), UINT64_C(999'999)},
                }),
                ter(tesSUCCESS))
                .close();

            // gw must own the auction slot
            BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{999'999}));

            // 999'999 tokens are burned, only 1 LPToken is owned by gw
            BEAST_EXPECT(
                amm.expectBalances(XRP(1'000), BTC(1'000), IOUAmount{1}));

            // gw owns only 1 LPToken in its balance
            BEAST_EXPECT(Number{amm.getLPTokensBalance(gw)} == 1);

            // gw attempts to burn the last of its LPTokens in an AMMBid
            // transaction. This transaction fails because it would burn all
            // the remaining LPTokens
            env(amm.bid({
                    .account = gw,
                    .bidMin = 1,
                }),
                ter(tecAMM_INVALID_TOKENS));
        }

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000);
                // Invlaid Min/Max combination
                env(ammAlice.bid({
                        .account = carol,
                        .bidMin = 200,
                        .bidMax = 100,
                    }),
                    ter(tecAMM_INVALID_TOKENS));

                // Invalid Account
                Account bad("bad");
                env.memoize(bad);
                env(ammAlice.bid({
                        .account = bad,
                        .bidMax = 100,
                    }),
                    seq(1),
                    ter(terNO_ACCOUNT));

                // Account is not LP
                Account const dan("dan");
                env.fund(XRP(1'000), dan);
                env(ammAlice.bid({
                        .account = dan,
                        .bidMin = 100,
                    }),
                    ter(tecAMM_INVALID_TOKENS));
                env(ammAlice.bid({
                        .account = dan,
                    }),
                    ter(tecAMM_INVALID_TOKENS));

                // Auth account is invalid.
                env(ammAlice.bid({
                        .account = carol,
                        .bidMin = 100,
                        .authAccounts = {bob},
                    }),
                    ter(terNO_ACCOUNT));

                // Invalid Assets
                env(ammAlice.bid({
                        .account = alice,
                        .bidMax = 100,
                        .assets = {{MPT(ammAlice[1]), GBP}},
                    }),
                    ter(terNO_AMM));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Invalid AMM
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.withdrawAll(alice);
                env(ammAlice.bid({
                        .account = alice,
                        .bidMax = 100,
                    }),
                    ter(terNO_AMM));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Bid price exceeds LP owned tokens
        {
            Env env(*this);
            fund(
                env,
                gw,
                {alice, bob, carol},
                XRP(1'000),
                {USD(30'000)},
                Fund::All);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 30'000'000'000,
                 .flags = MPTDEXFlags});

            AMM ammAlice(env, alice, BTC(10'000'000'000), USD(10'000));
            ammAlice.deposit(carol, 1'000'000);
            ammAlice.deposit(bob, 10);
            env(ammAlice.bid({
                    .account = carol,
                    .bidMin = 1'000'001,
                }),
                ter(tecAMM_INVALID_TOKENS));
            env(ammAlice.bid({
                    .account = carol,
                    .bidMax = 1'000'001,
                }),
                ter(tecAMM_INVALID_TOKENS));
            env(ammAlice.bid({
                .account = carol,
                .bidMin = 1'000,
            }));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{1'000}));
            // Slot purchase price is more than 1000 but bob only has 10 tokens
            env(ammAlice.bid({
                    .account = bob,
                }),
                ter(tecAMM_INVALID_TOKENS));
        }

        // Bid all tokens, still own the slot
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice, bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 1'000,
                 .flags = MPTDEXFlags});
            AMM amm(env, gw, XRP(10), BTC(1'000));
            auto const lpIssue = amm.lptIssue();
            env.trust(STAmount{lpIssue, 100}, alice);
            env.trust(STAmount{lpIssue, 50}, bob);
            env(pay(gw, alice, STAmount{lpIssue, 100}));
            env(pay(gw, bob, STAmount{lpIssue, 50}));
            env(amm.bid({.account = alice, .bidMin = 100}));
            // Alice doesn't have any more tokens, but
            // she still owns the slot.
            env(amm.bid({
                    .account = bob,
                    .bidMax = 50,
                }),
                ter(tecAMM_FAILED));
        }
    }

    void
    testBid(FeatureBitset features)
    {
        testcase("Bid");
        using namespace jtx;
        using namespace std::chrono;

        // Auction slot initially is owned by AMM creator, who pays 0 price.

        // Bid 110 tokens. Pay bidMin.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000);
                env(ammAlice.bid({.account = carol, .bidMin = 110}));
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));
                // 110 tokens are burned.
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'999'890, 0}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Bid with min/max when the pay price is less than min.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000);
                // Bid exactly 110. Pay 110 because the pay price is < 110.
                env(ammAlice.bid(
                    {.account = carol, .bidMin = 110, .bidMax = 110}));
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'999'890}));
                // Bid exactly 180-200. Pay 180 because the pay price is < 180.
                env(ammAlice.bid(
                    {.account = alice, .bidMin = 180, .bidMax = 200}));
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{180}));
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(11'000),
                    MPT(ammAlice[1])(11'000),
                    IOUAmount{10'999'814'5, -1}));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Start bid at bidMin 110.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol, bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 30'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));

            ammAlice.deposit(carol, 1'000'000);
            // Bid, pay bidMin.
            env(ammAlice.bid({.account = carol, .bidMin = 110}));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));

            ammAlice.deposit(bob, 1'000'000);
            // Bid, pay the computed price.
            env(ammAlice.bid({.account = bob}));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount(1155, -1)));

            // Bid bidMax fails because the computed price is higher.
            env(ammAlice.bid({
                    .account = carol,
                    .bidMax = 120,
                }),
                ter(tecAMM_FAILED));
            // Bid MaxSlotPrice succeeds - pay computed price
            env(ammAlice.bid({.account = carol, .bidMax = 600}));
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 0, IOUAmount{121'275, -3}));

            // Bid Min/MaxSlotPrice fails because the computed price is not
            // in range
            env(ammAlice.bid({
                    .account = carol,
                    .bidMin = 10,
                    .bidMax = 100,
                }),
                ter(tecAMM_FAILED));
            // Bid Min/MaxSlotPrice succeeds - pay computed price
            env(ammAlice.bid({.account = carol, .bidMin = 100, .bidMax = 600}));
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 0, IOUAmount{127'33875, -5}));
        }

        // Slot states.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol, bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .pay = 30'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));

            ammAlice.deposit(carol, 1'000'000);
            ammAlice.deposit(bob, 1'000'000);
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(12'000'000001),
                BTC(12'001),
                IOUAmount{12'000'000, 0}));

            // Initial state. Pay bidMin.
            env(ammAlice.bid({.account = carol, .bidMin = 110})).close();
            BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{110}));

            // 1st Interval after close, price for 0th interval.
            env(ammAlice.bid({.account = bob}));
            env.close(seconds(AUCTION_SLOT_INTERVAL_DURATION + 1));
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 1, IOUAmount{1'155, -1}));

            // 10th Interval after close, price for 1st interval.
            env(ammAlice.bid({.account = carol}));
            env.close(seconds(10 * AUCTION_SLOT_INTERVAL_DURATION + 1));
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, 10, IOUAmount{121'275, -3}));

            // 20th Interval (expired) after close, price for 10th interval.
            env(ammAlice.bid({.account = bob}));
            env.close(seconds(
                AUCTION_SLOT_TIME_INTERVALS * AUCTION_SLOT_INTERVAL_DURATION +
                1));
            BEAST_EXPECT(ammAlice.expectAuctionSlot(
                0, std::nullopt, IOUAmount{127'33875, -5}));

            // 0 Interval.
            env(ammAlice.bid({.account = carol, .bidMin = 110})).close();
            BEAST_EXPECT(
                ammAlice.expectAuctionSlot(0, std::nullopt, IOUAmount{110}));
            // ~321.09 tokens burnt on bidding fees.
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(12'000'000001),
                BTC(12'001),
                IOUAmount{11'999'678'91, -2}));
        }

        // Pool's fee 1%. Bid bidMin.
        // Auction slot owner and auth account trade at discounted fee -
        // 1/10 of the trading fee.
        // Other accounts trade at 1% fee.
        {
            Env env(*this);
            Account const dan("dan");
            Account const ed("ed");
            env.fund(XRP(2'000), gw, alice, bob, carol, dan, ed);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan, ed},
                 .pay = 30'000'000'000});
            fund(env, gw, {alice, carol}, {USD(30'000)}, Fund::TokenOnly);
            fund(env, gw, {bob, dan, ed}, {USD(20'000)}, Fund::TokenOnly);

            AMM ammAlice(
                env,
                alice,
                BTC(10'000'000'000),
                USD(10'000),
                CreateArg{.tfee = 1'000});
            ammAlice.deposit(bob, 1'000'000);
            ammAlice.deposit(ed, 1'000'000);
            ammAlice.deposit(carol, 500'000);
            ammAlice.deposit(dan, 500'000);
            auto ammTokens = ammAlice.getLPTokensBalance();
            env(ammAlice.bid({
                .account = carol,
                .bidMin = 120,
                .authAccounts = {bob, ed},
            }));
            auto const slotPrice = IOUAmount{5'200};
            ammTokens -= slotPrice;
            BEAST_EXPECT(ammAlice.expectAuctionSlot(100, 0, slotPrice));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(13'000'000'003), USD(13'000), ammTokens));
            // Discounted trade
            for (int i = 0; i < 10; ++i)
            {
                auto tokens = ammAlice.deposit(carol, USD(100));
                ammAlice.withdraw(carol, tokens, USD(0));
                tokens = ammAlice.deposit(bob, USD(100));
                ammAlice.withdraw(bob, tokens, USD(0));
                tokens = ammAlice.deposit(ed, USD(100));
                ammAlice.withdraw(ed, tokens, USD(0));
            }
            // carol, bob, and ed pay ~0.99USD in fees.
            BEAST_EXPECT(
                env.balance(carol, USD) ==
                STAmount(USD, UINT64_C(29'499'00572620544), -11));
            BEAST_EXPECT(
                env.balance(bob, USD) ==
                STAmount(USD, UINT64_C(18'999'00572616194), -11));
            BEAST_EXPECT(
                env.balance(ed, USD) ==
                STAmount(USD, UINT64_C(18'999'0057261184), -10));
            // USD pool is slightly higher because of the fees.
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(13'000'000'003),
                STAmount(USD, UINT64_C(13'002'98282151422), -11),
                ammTokens));

            ammTokens = ammAlice.getLPTokensBalance();
            // Trade with the fee
            for (int i = 0; i < 10; ++i)
            {
                auto const tokens = ammAlice.deposit(dan, USD(100));
                ammAlice.withdraw(dan, tokens, USD(0));
            }
            // dan pays ~9.94USD, which is ~10 times more in fees than
            // carol, bob, ed. the discounted fee is 10 times less
            // than the trading fee.

            BEAST_EXPECT(
                env.balance(dan, USD) ==
                STAmount(USD, UINT64_C(19'490'05672274398), -11));
            // USD pool gains more in dan's fees.
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(13'000'000'003),
                STAmount{USD, UINT64_C(13'012'92609877024), -11},
                ammTokens));
            // Discounted fee payment
            ammAlice.deposit(carol, USD(100));
            ammTokens = ammAlice.getLPTokensBalance();
            BEAST_EXPECT(ammAlice.expectBalances(
                MPT(ammAlice[0])(13'000'000'003),
                STAmount{USD, UINT64_C(13'112'92609877024), -11},
                ammTokens));
            env(pay(carol, bob, USD(100)),
                path(~USD),
                sendmax(BTC(110'000'000)));
            env.close();
            // carol pays 100000 drops in fees
            // 99900668MPT swapped in for 100USD
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(13'100'000'671),
                STAmount{USD, UINT64_C(13'012'92609877024), -11},
                ammTokens));

            // Payment with the trading fee
            env(pay(alice, carol, BTC(100'000'000)),
                path(~MPT(ammAlice[0])),
                sendmax(USD(110)));
            env.close();
            // alice pays ~1.011USD in fees, which is ~10 times more
            // than carol's fee
            // 100.099431529USD swapped in for 100MPT

            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(13'000'000'671),
                STAmount{USD, UINT64_C(13'114'03663044937), -11},
                ammTokens));

            // Auction slot expired, no discounted fee
            env.close(seconds(TOTAL_TIME_SLOT_SECS + 1));
            // clock is parent's based
            env.close();

            BEAST_EXPECT(
                env.balance(carol, USD) ==
                STAmount(USD, UINT64_C(29'399'00572620544), -11));
            ammTokens = ammAlice.getLPTokensBalance();
            for (int i = 0; i < 10; ++i)
            {
                auto const tokens = ammAlice.deposit(carol, USD(100));
                ammAlice.withdraw(carol, tokens, USD(0));
            }
            // carol pays ~9.94USD in fees, which is ~10 times more in
            // trading fees vs discounted fee.

            BEAST_EXPECT(
                env.balance(carol, USD) ==
                STAmount(USD, UINT64_C(29'389'06197177129), -11));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(13'000'000'671),
                STAmount{USD, UINT64_C(13'123'98038488352), -11},
                ammTokens));

            env(pay(carol, bob, USD(100)),
                path(~USD),
                sendmax(BTC(110'000'000)));
            env.close();
            // carol pays ~1.008MPT in trading fee, which is
            // ~10 times more than the discounted fee.
            // 99.815876MPT is swapped in for 100USD
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(13'100'824'793),
                STAmount{USD, UINT64_C(13'023'98038488352), -11},
                ammTokens));
        }

        // Bid tiny amount
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // Bid a tiny amount
                auto const tiny =
                    Number{STAmount::cMinValue, STAmount::cMinOffset};
                env(ammAlice.bid(
                    {.account = alice, .bidMin = IOUAmount{tiny}}));
                // Auction slot purchase price is equal to the tiny amount
                // since the minSlotPrice is 0 with no trading fee.
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{tiny}));
                // The purchase price is too small to affect the total tokens
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000'000'000),
                    USD(10'000),
                    ammAlice.tokens()));
                // Bid the tiny amount
                env(ammAlice.bid({
                    .account = alice,
                    .bidMin =
                        IOUAmount{STAmount::cMinValue, STAmount::cMinOffset},
                }));
                // Pay slightly higher price
                BEAST_EXPECT(ammAlice.expectAuctionSlot(
                    0, 0, IOUAmount{tiny * Number{105, -2}}));
                // The purchase price is still too small to affect the total
                // tokens
                BEAST_EXPECT(ammAlice.expectBalances(
                    MPT(ammAlice[0])(10'000'000'000),
                    USD(10'000),
                    ammAlice.tokens()));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});

        // Reset auth account
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(ammAlice.bid({
                    .account = alice,
                    .bidMin = IOUAmount{100},
                    .authAccounts = {carol},
                }));
                BEAST_EXPECT(ammAlice.expectAuctionSlot({carol}));
                env(ammAlice.bid({.account = alice, .bidMin = IOUAmount{100}}));
                BEAST_EXPECT(ammAlice.expectAuctionSlot({}));
                Account bob("bob");
                Account dan("dan");
                fund(env, {bob, dan}, XRP(1'000));
                env(ammAlice.bid({
                    .account = alice,
                    .bidMin = IOUAmount{100},
                    .authAccounts = {bob, dan},
                }));
                BEAST_EXPECT(ammAlice.expectAuctionSlot({bob, dan}));
            },
            {{AMMMPT(10'000'000'000), USD(10'000)}});

        // Bid all tokens, still own the slot and trade at a discount
        {
            Env env(*this);
            env.fund(XRP(2'000), gw, alice, bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 2'000'000'000,
                 .flags = MPTDEXFlags});
            fund(env, gw, {alice, bob}, {USD(2'000)}, Fund::TokenOnly);
            AMM amm(env, gw, BTC(1'000'000'000), USD(1'010), false, 1'000);
            auto const lpIssue = amm.lptIssue();
            env.trust(STAmount{lpIssue, 500}, alice);
            env.trust(STAmount{lpIssue, 50}, bob);
            env(pay(gw, alice, STAmount{lpIssue, 500}));
            env(pay(gw, bob, STAmount{lpIssue, 50}));
            // Alice doesn't have anymore lp tokens
            env(amm.bid({.account = alice, .bidMin = 500}));
            BEAST_EXPECT(amm.expectAuctionSlot(100, 0, IOUAmount{500}));
            BEAST_EXPECT(expectLine(env, alice, STAmount{lpIssue, 0}));
            // But trades with the discounted fee since she still owns the slot.
            // Alice pays ~10011 MPT in fees
            env(pay(alice, bob, USD(10)), path(~USD), sendmax(BTC(11'000'000)));
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'010'010'011),
                USD(1'000),
                IOUAmount{1'004'487'562112089, -9}));

            // Bob pays the full fee ~0.1USD
            env(pay(bob, alice, BTC(10'000'000)),
                path(~MPT(BTC)),
                sendmax(USD(11)));

            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000'010'011),
                STAmount{USD, UINT64_C(1'010'100908980811), -12},
                IOUAmount{1'004'487'562112089, -9}));
        }

        // preflight tests
        {
            Env env(*this, features);
            env.fund(XRP(2'000), gw, alice, bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .pay = 2'000,
                 .flags = MPTDEXFlags});
            AMM amm(env, gw, XRP(1'000), BTC(1'010), false, 1'000);
            Json::Value tx = amm.bid({.account = alice, .bidMin = 500});

            {
                auto jtx = env.jt(tx, seq(1), fee(10));
                env.app().config().features.erase(featureMPTokensV2);
                PreflightContext pfctx(
                    env.app(),
                    *jtx.stx,
                    env.current()->rules(),
                    tapNONE,
                    env.journal);
                auto pf = AMMBid::preflight(pfctx);
                BEAST_EXPECT(pf == temDISABLED);
                env.app().config().features.insert(featureMPTokensV2);
            }

            {
                auto jtx = env.jt(tx, seq(1), fee(10));
                jtx.jv["Asset2"]["currency"] = "XRP";
                jtx.jv["Asset2"].removeMember("mpt_issuance_id");
                jtx.stx = env.ust(jtx);
                PreflightContext pfctx(
                    env.app(),
                    *jtx.stx,
                    env.current()->rules(),
                    tapNONE,
                    env.journal);
                auto pf = AMMBid::preflight(pfctx);
                BEAST_EXPECT(pf == temBAD_AMM_TOKENS);
            }
        }
    }

    void
    testClawback()
    {
        testcase("Clawback");
        using namespace jtx;

        Env env(*this);
        env.fund(XRP(2'000), gw, alice);
        MPT BTC = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .transferFee = 1'500,
             .pay = 40'000});

        // alice creates AMM
        AMM amm(env, alice, XRP(1'000), BTC(1'000));

        // gw owns MPTIssuance, not allowed to set asfAllowTrustLineClawback
        env(fset(gw, asfAllowTrustLineClawback), ter(tecOWNERS));
    }

    void
    testClawbackFromAMMAccount(FeatureBitset features)
    {
        testcase("test clawback from AMM account");
        using namespace jtx;

        Env env(*this, features);
        env.fund(XRP(1'000), gw);
        env(fset(gw, asfAllowTrustLineClawback));
        fund(env, gw, {alice}, XRP(1'000), {USD(1'000)}, Fund::Acct);

        MPTTester BTC(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .pay = 30'000,
             .flags = tfMPTCanLock | MPTDEXFlags});

        // to clawback from AMM account, must use AMMClawback instead of
        // Clawback
        auto const err = features[featureSingleAssetVault] ? tecPSEUDO_ACCOUNT
                                                           : tecAMM_ACCOUNT;
        AMM amm(env, gw, XRP(100), BTC(100));
        auto amount = amountFromString(amm.lptIssue(), "10");
        env(claw(gw, amount), ter(err));

        AMM amm1(env, alice, USD(100), BTC(200));
        auto amount1 = amountFromString(amm1.lptIssue(), "10");
        env(claw(gw, amount1), ter(err));
    }

    void
    testInvalidAMMPayment()
    {
        testcase("Invalid AMM Payment");
        using namespace jtx;
        using namespace std::chrono;
        using namespace std::literals::chrono_literals;

        // Can't pay into AMM account.
        // Can't pay out since there is no keys
        for (auto const& acct : {gw, alice})
        {
            {
                Env env(*this);
                fund(env, gw, {alice, carol}, XRP(1'000));
                MPTTester BTC(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol},
                     .pay = 100,
                     .flags = MPTDEXFlags});
                // XRP balance is below reserve
                AMM ammAlice(env, acct, XRP(10), BTC(10));
                // Pay below reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                    ter(tecNO_PERMISSION));
                // Pay above reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(300)),
                    ter(tecNO_PERMISSION));
                // Pay MPT
                env(pay(carol, ammAlice.ammAccount(), BTC(10)),
                    ter(tecNO_PERMISSION));
            }

            {
                Env env(*this);
                fund(env, gw, {alice, carol}, XRP(10'000'000));
                MPTTester BTC(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol},
                     .pay = 20'000,
                     .flags = MPTDEXFlags});
                // XRP balance is above reserve
                AMM ammAlice(env, acct, XRP(1'000'000), BTC(10'000));
                // Pay below reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(10)),
                    ter(tecNO_PERMISSION));
                // Pay above reserve
                env(pay(carol, ammAlice.ammAccount(), XRP(1'000'000)),
                    ter(tecNO_PERMISSION));
                // Pay MPT
                env(pay(carol, ammAlice.ammAccount(), BTC(1'000)),
                    ter(tecNO_PERMISSION));
            }
        }

        // Can't pay into AMM with escrow.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(escrow::create(
                        carol, ammAlice.ammAccount(), MPT(ammAlice[1])(1)),
                    escrow::condition(escrow::cb1),
                    escrow::finish_time(env.now() + 1s),
                    escrow::cancel_time(env.now() + 2s),
                    fee(1'500),
                    ter(tecNO_PERMISSION));

                env(escrow::create(carol, ammAlice.ammAccount(), XRP(1)),
                    escrow::condition(escrow::cb1),
                    escrow::finish_time(env.now() + 1s),
                    escrow::cancel_time(env.now() + 2s),
                    fee(1'500),
                    ter(tecNO_PERMISSION));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Can't pay into AMM with paychan.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const pk = carol.pk();
                auto const settleDelay = 10s;
                NetClock::time_point const cancelAfter =
                    env.current()->info().parentCloseTime + 20s;
                env(create(
                        carol,
                        ammAlice.ammAccount(),
                        MPT(ammAlice[1])(1'000),
                        settleDelay,
                        pk,
                        cancelAfter),
                    ter(telENV_RPC_FAILED));

                env(create(
                        carol,
                        ammAlice.ammAccount(),
                        XRP(1'000),
                        settleDelay,
                        pk,
                        cancelAfter),
                    ter(tecNO_PERMISSION));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Can't pay into AMM with checks.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env(check::create(
                        env.master.id(), ammAlice.ammAccount(), XRP(100)),
                    ter(tecNO_PERMISSION));

                env(check::create(
                        env.master.id(),
                        ammAlice.ammAccount(),
                        MPT(ammAlice[1])(100)),
                    ter(tecNO_PERMISSION));
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const& BTC = MPT(ammAlice[1]);
                // Can't consume whole pool
                env(pay(alice, carol, USD(100)),
                    path(~USD),
                    sendmax(BTC(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, BTC(100'000'000)),
                    path(~BTC),
                    sendmax(USD(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                // Overflow
                env(pay(alice,
                        carol,
                        STAmount{USD, UINT64_C(99'999999999), -9}),
                    path(~USD),
                    sendmax(BTC(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice,
                        carol,
                        STAmount{USD, UINT64_C(999'99999999), -8}),
                    path(~USD),
                    sendmax(BTC(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, BTC(99'999'999)),
                    path(~BTC),
                    sendmax(USD(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                // Sender doesn't have enough funds
                env(pay(alice, carol, USD(99.99)),
                    path(~USD),
                    sendmax(BTC(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
                env(pay(alice, carol, BTC(99'990'000)),
                    path(~BTC),
                    sendmax(USD(1'000'000'000)),
                    ter(tecPATH_PARTIAL));
            },
            {{USD(100), AMMMPT(100'000'000)}});

        // Globally locked MPT.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            BTC.set({.flags = tfMPTLock});

            env(pay(alice, carol, BTC(1)),
                path(~static_cast<MPT>(BTC)),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(XRP(10)),
                ter(tecLOCKED));
            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(BTC(10)),
                ter(tecLOCKED));
        }

        // Individually locked MPT destination account.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            BTC.set({.holder = carol, .flags = tfMPTLock});

            env(pay(alice, carol, BTC(1)),
                path(~static_cast<MPT>(BTC)),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(XRP(10)),
                ter(tecLOCKED));
        }

        // Individually locked MPT source account
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            BTC.set({.holder = alice, .flags = tfMPTLock});

            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(BTC(10)),
                ter(tecLOCKED));
        }

        // lock on both sides
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, ETH(10'000), BTC(10'000));
            BTC.set({.holder = carol, .flags = tfMPTLock});
            BTC.set({.holder = alice, .flags = tfMPTLock});
            ETH.set({.holder = carol, .flags = tfMPTLock});
            ETH.set({.holder = alice, .flags = tfMPTLock});

            env(pay(alice, carol, ETH(1)),
                path(~MPT(ETH)),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(BTC(10)),
                ter(tecLOCKED));

            env(pay(alice, carol, BTC(1)),
                path(~MPT(BTC)),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(ETH(10)),
                ter(tecLOCKED));
        }

        // Individually locked AMM MPT
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});

            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));
            BTC.set({.holder = ammAlice.ammAccount(), .flags = tfMPTLock});

            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                txflags(tfPartialPayment | tfNoRippleDirect),
                sendmax(BTC(10)),
                ter(tecPATH_DRY));
        }
    }

    void
    testBasicPaymentEngine()
    {
        testcase("Basic Payment");
        using namespace jtx;

        // Payment 100MPT for 100XRP.
        // Force one path with tfNoRippleDirect.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));
                env.fund(XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, MPT(ammAlice[1])(100)),
                    path(~MPT(ammAlice[1])),
                    sendmax(XRP(100)),
                    txflags(tfNoRippleDirect));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100), MPT(ammAlice[1])(10'000), ammAlice.tokens()));
                // Initial balance + 100
                env.require(balance(carol, carolMPT + MPT(ammAlice[1])(100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30'000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10'000), AMMMPT(10'100)}});

        // Payment 100IOU/MPT for 100IOU/MPT. Test IOU/MPT mix.
        // Force one path with tfNoRippleDirect.
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
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, bob, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10100));
                auto carolBTC = env.balance(carol, BTC);
                auto bobUSD = env.balance(bob, USD);
                env(pay(bob, carol, BTC(100)),
                    path(~BTC),
                    sendmax(USD(100)),
                    txflags(tfNoRippleDirect | tfPartialPayment));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'100), BTC(10'000), ammAlice.tokens()));
                env.require(balance(carol, carolBTC + BTC(100)));
                env.require(balance(bob, bobUSD - USD(100)));
            };
            testHelper2TokensMix(test);
        }

        // Payment 100MPT for 100XRP, use default path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));
                env.fund(XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, MPT(ammAlice[1])(100)), sendmax(XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100), MPT(ammAlice[1])(10'000), ammAlice.tokens()));
                // Initial balance + 100
                env.require(balance(carol, carolMPT + MPT(ammAlice[1])(100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30'000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10'000), AMMMPT(10'100)}});

        // Payment 100IOU/MPT for 100IOU/MPT using default path.
        // Test IOU/MPT mix.
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
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, bob, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10100));
                auto carolBTC = env.balance(carol, BTC);
                auto bobUSD = env.balance(bob, USD);
                env(pay(bob, carol, BTC(100)), sendmax(USD(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'100), BTC(10'000), ammAlice.tokens()));
                env.require(balance(carol, carolBTC + BTC(100)));
                env.require(balance(bob, bobUSD - USD(100)));
            };
            testHelper2TokensMix(test);
        }

        // This payment is identical to above. While it has
        // both default path and path, activeStrands has one path.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));
                env.fund(XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, MPT(ammAlice[1])(100)),
                    path(~MPT(ammAlice[1])),
                    sendmax(XRP(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'100), MPT(ammAlice[1])(10'000), ammAlice.tokens()));
                // Initial balance + 100
                env.require(balance(carol, carolMPT + MPT(ammAlice[1])(100)));
                // Initial balance 30,000 - 100(sendmax) - 10(tx fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30'000) - XRP(100) - txfee(env, 1)));
            },
            {{XRP(10'000), AMMMPT(10'100)}});

        // Test MPT/IOU combination for the case above.
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
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, bob, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10100));
                auto carolBTC = env.balance(carol, BTC);
                auto bobUSD = env.balance(bob, USD);
                env(pay(bob, carol, BTC(100)), path(~BTC), sendmax(USD(100)));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'100), BTC(10'000), ammAlice.tokens()));
                env.require(balance(carol, carolBTC + BTC(100)));
                env.require(balance(bob, bobUSD - USD(100)));
            };
            testHelper2TokensMix(test);
        }

        // Payment with limitQuality set.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto carolMPT = env.balance(carol, MPT(ammAlice[1]));
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                // Pays 10MPT for 10XRP. A larger payment of ~99.11MPT/100XRP
                // would have been sent has it not been for limitQuality.
                env(pay(bob, carol, MPT(ammAlice[1])(100)),
                    path(~MPT(ammAlice[1])),
                    sendmax(XRP(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'010), MPT(ammAlice[1])(10'000), ammAlice.tokens()));
                // Initial balance + 10(limited by limitQuality)
                env.require(balance(carol, carolMPT + MPT(ammAlice[1])(10)));
                // Initial balance 30,000 - 10(limited by limitQuality) - 10(tx
                // fee)
                BEAST_EXPECT(expectLedgerEntryRoot(
                    env, bob, XRP(30'000) - XRP(10) - txfee(env, 1)));

                // Fails because of limitQuality. Would have sent
                // ~98.91MPT/110XRP has it not been for limitQuality.
                env(pay(bob, carol, MPT(ammAlice[1])(100)),
                    path(~MPT(ammAlice[1])),
                    sendmax(XRP(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality),
                    ter(tecPATH_DRY));
                env.close();
            },
            {{XRP(10'000), AMMMPT(10'010)}});

        // Payment with limitQuality set. MPT/IOU combination.
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
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, bob, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10010));
                auto carolBTC = env.balance(carol, BTC);
                auto bobUSD = env.balance(bob, USD);
                env(pay(bob, carol, BTC(100)),
                    path(~BTC),
                    sendmax(USD(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'010), BTC(10'000), ammAlice.tokens()));
                env.require(balance(carol, carolBTC + BTC(10)));
                env.require(balance(bob, bobUSD - USD(10)));

                env(pay(bob, carol, BTC(100)),
                    path(~BTC),
                    sendmax(USD(100)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality),
                    ter(tecPATH_DRY));
                env.close();
            };
            testHelper2TokensMix(test);
        }

        // Payment with limitQuality and transfer fee set.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, bob, carol);
            env.close();
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 10'000,
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            auto ammAlice =
                AMM(env, alice, XRP(10'000), BTC(10'010'000'000'000'000));
            env.close();
            auto carolMPT = env.balance(carol, MPT(BTC));
            // Pays 10'000'000'000'000MPT for 10XRP. A larger payment of
            // ~99'110'000'000'000MPT/100XRP would have been sent has it not
            // been for limitQuality and the transfer fee.
            env(pay(bob, carol, BTC(100'000'000'000'000)),
                path(~MPT(BTC)),
                sendmax(XRP(110)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'010), BTC(10'000'000'000'000'000), ammAlice.tokens()));
            // 10'000'000'000'000MPT - 10% transfer fee
            env.require(balance(carol, carolMPT + BTC(9'090'909'090'909)));
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(30'000) - XRP(10) - txfee(env, 1)));
        }

        // Payment with limitQuality and transfer fee set. MPT/IOU combination.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(30'000), alice, bob, carol, gw);
                env.close();

                auto const USD = issue1({
                    .env = env,
                    .token = "USD",
                    .issuer = gw,
                    .holders = {alice, bob, carol},
                    .limit = 1'000'000
                    //.transferFee = 10'000
                });
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 300'000'000'000'000'000,
                     .transferFee = 10'000});

                env(pay(gw, alice, BTC(30'000'000'000'000'000)));
                env(pay(gw, bob, BTC(30'000'000'000'000'000)));
                env(pay(gw, carol, BTC(30'000'000'000'000'000)));
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, bob, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice =
                    AMM(env, alice, USD(10'000), BTC(10'010'000'000'000'000));
                auto carolBTC = env.balance(carol, BTC);
                auto bobUSD = env.balance(bob, USD);
                env(pay(bob, carol, BTC(100'000'000'000'000)),
                    path(~BTC),
                    sendmax(USD(110)),
                    txflags(
                        tfNoRippleDirect | tfPartialPayment | tfLimitQuality));
                env.close();
                BEAST_EXPECT(ammAlice.expectBalances(
                    USD(10'010),
                    BTC(10'000'000'000'000'000),
                    ammAlice.tokens()));
                env.require(balance(carol, carolBTC + BTC(9'090'909'090'909)));
                env.require(balance(bob, bobUSD - USD(10)));
            };
            testHelper2TokensMix(test);
        }

        // Fail when partial payment is not set.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                env.fund(jtx::XRP(30'000), bob);
                env.close();
                env(pay(bob, carol, MPT(ammAlice[1])(100)),
                    path(~MPT(ammAlice[1])),
                    sendmax(XRP(100)),
                    txflags(tfNoRippleDirect),
                    ter(tecPATH_PARTIAL));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
        // Fail when partial payment is not set. MPT/IOU combination.
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
                env(pay(gw, alice, USD(50000)));
                env(pay(gw, bob, USD(50000)));
                env(pay(gw, carol, USD(50000)));
                env.close();

                auto ammAlice = AMM(env, alice, USD(10000), BTC(10000));
                env(pay(bob, carol, BTC(100)),
                    path(~BTC),
                    sendmax(USD(100)),
                    txflags(tfNoRippleDirect),
                    ter(tecPATH_PARTIAL));
            };
            testHelper2TokensMix(test);
        }

        // Non-default path (with AMM) has a better quality than default path.
        // The max possible liquidity is taken out of non-default
        // path ~29.9e14XRP/29.9e14ETH, ~29.9e14ETH/~29.99e14btc. The rest
        // is taken from the offer.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 3'000'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 3'000'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            env.fund(XRP(1'000), bob);
            env.close();
            auto ammETH_XRP =
                AMM(env, alice, XRP(10'000), ETH(1'000'000'000'000'000'000));
            auto ammBTC_ETH =
                AMM(env,
                    alice,
                    ETH(1'000'000'000'000'000'000),
                    BTC(1'000'000'000'000'000'000));
            env(offer(alice, XRP(101), BTC(10'000'000'000'000'000)),
                txflags(tfPassive));
            env.close();
            env(pay(bob, carol, BTC(10'000'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammETH_XRP.expectBalances(
                XRPAmount(10'030'082'730),
                ETH(9'970'00749812546800),
                ammETH_XRP.tokens()));

            BEAST_EXPECT(ammBTC_ETH.expectBalances(
                BTC(9'970'09727766217162),
                ETH(10'029'99250187453200),
                ammBTC_ETH.tokens()));

            Amounts const expectedAmounts =
                Amounts{XRPAmount(30'201'749), BTC(29'90272233782838)};

            BEAST_EXPECT(expectOffers(env, alice, 1, {{expectedAmounts}}));

            // Initial (30,000 + 100)e14
            env.require(balance(carol, BTC(3'010'000'000'000'000'000)));
            // Initial 1,000 - 30082730(AMM pool) - 70798251(offer) - 10(tx fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1'000) - XRPAmount{30'082'730} - XRPAmount{70'798'251} -
                    txfee(env, 1)));
        }

        // Default path (with AMM) has a better quality than a
        // non-default path.
        // The max possible liquidity is taken out of default
        // path ~49XRP/49e14BTC. The rest is taken from the offer.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 3'000'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .pay = 1'000'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            auto ammAlice =
                AMM(env, alice, XRP(10'000), BTC(1'000'000'000'000'000'000));
            env.fund(XRP(1'000), bob);
            env.close();
            env(offer(alice, XRP(101), ETH(10'000'000'000'000'000)),
                txflags(tfPassive));
            env.close();
            env(offer(
                    alice,
                    ETH(10'000'000'000'000'000),
                    BTC(10'000'000'000'000'000)),
                txflags(tfPassive));
            env.close();
            env(pay(bob, carol, BTC(10'000'000'000'000'000)),
                path(~MPT(ETH), ~MPT(BTC)),
                sendmax(XRP(102)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount(10'050'238'637),
                BTC(9'950'01249687578000),
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                2,
                {{Amounts{XRPAmount(50'487'378), ETH(49'98750312422000)},
                  Amounts{ETH(49'98750312422000), BTC(49'98750312422000)}}}));
            // Initial (30,000 + 100)e14
            env.require(balance(carol, BTC(30'100'00000000000000)));
            // Initial 1,000 - 50238637(AMM pool) - 50512622(offer) - 10(tx
            // fee)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                bob,
                XRP(1'000) - XRPAmount{50'238'637} - XRPAmount{50'512'622} -
                    txfee(env, 1)));
        }

        // Default path with AMM and Order Book offer. AMM is consumed first,
        // remaining amount is consumed by the offer.
        {
            Env env(*this);
            fund(env, gw, {alice, bob, carol}, XRP(30'000));
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'100'000'000'000'000));
            env(offer(bob, XRP(100), MPT(ammAlice[1])(100'000'000'000'000)),
                txflags(tfPassive));
            env.close();
            env(pay(alice, carol, MPT(ammAlice[1])(200'000'000'000'000)),
                sendmax(XRP(200)),
                txflags(tfPartialPayment));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100),
                MPT(ammAlice[1])(10'000'000000000010),
                ammAlice.tokens()));
            env.require(balance(carol, MPT(ammAlice[1])(30'199'999999999990)));

            // Initial 30,000 - 10000(AMM pool LP) - 100(AMMoffer) -
            // - 100(offer) - 10(tx fee) - 10(tx fee of MPTTester init as
            // holder) - one reserve
            BEAST_EXPECT(expectLedgerEntryRoot(
                env,
                alice,
                XRP(30'000) - XRP(10'000) - XRP(100) - XRP(100) -
                    ammCrtFee(env) - 2 * txfee(env, 1)));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        // Default path with AMM and Order Book offer.
        // Order Book offer is consumed first.
        // Remaining amount is consumed by AMM.
        {
            Env env(*this);
            fund(env, gw, {alice, bob, carol}, XRP(20'000));
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 2'000,
                 .flags = MPTDEXFlags});
            env(offer(bob, XRP(50), BTC(150)), txflags(tfPassive));
            AMM ammAlice(env, alice, XRP(1'000), BTC(1'050));
            env(pay(alice, carol, BTC(200)),
                sendmax(XRP(200)),
                txflags(tfPartialPayment));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(1'050), BTC(1'000), ammAlice.tokens()));
            env.require(balance(carol, BTC(2'200)));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        // Offer crossing XRP/MPT
        {
            Env env(*this);
            fund(env, gw, {alice, bob, carol}, XRP(30'000));
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 30'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'100));
            env(offer(bob, MPT(ammAlice[1])(100), XRP(100)));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'100), MPT(ammAlice[1])(10'000), ammAlice.tokens()));
            // Initial 30,000 + 100
            env.require(balance(bob, MPT(ammAlice[1])(30'100)));
            // Initial 30,000 - 100(offer) - 10(tx fee) - 1(tx fee for MPTTester
            // holder)
            BEAST_EXPECT(expectLedgerEntryRoot(
                env, bob, XRP(30'000) - XRP(100) - 2 * txfee(env, 1)));
            BEAST_EXPECT(expectOffers(env, bob, 0));
        }

        // Offer crossing MPT/MPT and transfer rate
        // Single path AMM offer
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 25'000,
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 25'000,
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(
                env,
                alice,
                BTC(1'000'000'000'000'000),
                ETH(1'100'000'000'000'000));
            // This offer succeeds to cross pre- and post-amendment
            // because the strand's out amount is small enough to match
            // limitQuality value and limitOut() function in StrandFlow
            // doesn't require an adjustment to out value.
            env(offer(
                carol, ETH(100'000'000'000'000), BTC(100'000'000'000'000)));
            env.close();
            // No transfer fee
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(1'100'000'000'000'000),
                ETH(1'000'000'000'000'000),
                ammAlice.tokens()));
            // Initial 30,000'000'000'000'000 - 100'000'000'000'000(offer)-25 %
            // transfer fee
            env.require(balance(carol, BTC(29'875'000'000'000'000)));
            // Initial 30,000'000'000'000'000 + 100'000'000'000'000(offer)
            env.require(balance(carol, ETH(30'100'000'000'000'000)));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Single-path AMM offer
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, bob, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 100,
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM amm(env, alice, XRP(1'000), BTC(500'000'000'000'000));
            env(offer(carol, XRP(100), BTC(55'000'000'000'000)));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                XRPAmount(909'090'909), BTC(550'000000055000), amm.tokens()));
            // Offer ~91XRP/49.99e12BTC
            BEAST_EXPECT(expectOffers(
                env,
                carol,
                1,
                {{Amounts{XRPAmount{9'090'909}, BTC(4'999999950000)}}}));
            // Carol pays 0.1% fee on 50'000000055000BTC = 50'000000055BTC
            env.require(balance(carol, BTC(29'949'949'999'944'945)));
        }

        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .transferFee = 100,
                 .pay = 3'000'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM amm(env, alice, XRP(1'000), BTC(50'000'000'000'000'000));
            env(offer(carol, XRP(10), BTC(5'500'000'000'000'000)));
            env.close();

            BEAST_EXPECT(amm.expectBalances(
                XRP(990), BTC(505'05050505050510), amm.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Multi-path AMM offer
        {
            Env env(*this);
            Account const ed("ed");
            env.fund(XRP(30'000), gw, alice, bob, carol, ed);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 20'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = 25'000,
                 .pay = 20'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(
                env,
                alice,
                BTC(10'000'000'000'000'000),
                ETH(11'000'000'000'000'000));

            env(offer(bob, BTC(100'000'000'000'000), XRP(10)),
                txflags(tfPassive));
            env(offer(ed, XRP(10), ETH(100'000'000'000'000)),
                txflags(tfPassive));
            env.close();
            env(offer(
                carol, ETH(1'000'000'000'000'000), BTC(1'000'000'000'000'000)));
            env.close();

            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(1'060'6848287928310),
                ETH(1'037'0658372213403),
                ammAlice.tokens()));
            // Consumed offer ~72.93e13ETH/72.93e13BTC
            BEAST_EXPECT(expectOffers(
                env,
                carol,
                1,
                {Amounts{ETH(27'0658372213403), BTC(27'0658372213403)}}));
            BEAST_EXPECT(expectOffers(env, bob, 0));
            BEAST_EXPECT(expectOffers(env, ed, 0));

            env.require(balance(carol, BTC(19'116'439'640'089'610)));
            env.require(balance(carol, ETH(20'729'341'627'786'597)));
            env.require(balance(bob, BTC(20'100'000'000'000'000)));
            env.require(balance(ed, ETH(19'875'000'000'000'000)));
        }

        // Payment and transfer fee
        // Scenario:
        // Bob sends 125BTC to pay 80EUR to Carol
        // Payment execution:
        // bob's 125BTC/1.25 = 100BTC
        // 100BTC/100EUR AMM offer
        // 100EUR/1.25 = 80EUR paid to carol
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, bob, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 30'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .pay = 30'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, BTC(1'000), ETH(1'100));
            env(rate(gw, 1.25));
            env.close();
            env(pay(bob, carol, ETH(100)),
                path(~MPT(ETH)),
                sendmax(BTC(125)),
                txflags(tfPartialPayment));
            env.close();
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(1'100), ETH(1'000), ammAlice.tokens()));
            env.require(balance(bob, BTC(29'875)));
            env.require(balance(carol, ETH(30'080)));
        }

        // Payment and transfer fee, multiple steps
        // Scenario:
        // Dan's offer 200CAN/200GBP
        // AMM 1000GBP/10125ETH
        // Ed's offer 200ETH/BTC
        // Bob sends 195.3125CAN to pay 100BTC to Carol
        // Payment execution:
        // bob's 195.3125CAN/1.25 = 156.25CAN -> dan's offer
        // 156.25CAN/156.25GBP 156.25GBP/1.25 = 125GBP -> AMM's offer
        // 125GBP/125ETH 125ETH/1.25 = 100ETH -> ed's offer
        // 100ETH/100BTC 100BTC/1.25 = 80BTC paid to carol
        {
            Env env(*this);
            Account const dan("dan");
            Account const ed("ed");
            env.fund(XRP(30'000), gw, alice, bob, carol, dan, ed);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan, ed},
                 .transferFee = 25'000,
                 .pay = 30'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan, ed},
                 .transferFee = 25'000,
                 .pay = 30'000,
                 .flags = MPTDEXFlags});
            MPTTester CAN(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan, ed},
                 .transferFee = 25'000,
                 .pay = 2'000'000,
                 .flags = MPTDEXFlags});
            MPTTester GBP(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, dan, ed},
                 .transferFee = 25'000,
                 .pay = 3'000'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, GBP(1'000'000), ETH(10'125));
            env(pay(gw, bob, CAN(1'953'125)));
            env.close();
            env(offer(dan, CAN(2'000'000), GBP(20'000)));
            env(offer(ed, ETH(200), BTC(200)));
            env.close();
            env(pay(bob, carol, BTC(100)),
                path(~MPT(GBP), ~MPT(ETH), ~MPT(BTC)),
                sendmax(CAN(1'953'125)),
                txflags(tfPartialPayment));
            env.close();
            env.require(balance(bob, CAN(2'000'000)));
            env.require(balance(dan, CAN(3'562'500)));
            env.require(balance(dan, GBP(2'984'375)));

            BEAST_EXPECT(ammAlice.expectBalances(
                GBP(1'012'500), ETH(10'000), ammAlice.tokens()));
            env.require(balance(ed, ETH(30'100)));
            env.require(balance(ed, BTC(29'900)));
            env.require(balance(carol, BTC(30'080)));
        }

        // Pay amounts close to one side of the pool
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const& BTC = MPT(ammAlice[1]);
                env(pay(alice, carol, BTC(9999)),
                    path(~BTC),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, BTC(10'000)),
                    path(~BTC),
                    sendmax(XRP(1)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, XRP(100)),
                    path(~XRP),
                    sendmax(BTC(100)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
                env(pay(alice, carol, STAmount{xrpIssue(), 99'999'900}),
                    path(~XRP),
                    sendmax(BTC(100)),
                    txflags(tfPartialPayment),
                    ter(tesSUCCESS));
            },
            {{XRP(100), AMMMPT(10'000)}});

        // Multiple paths/steps
        {
            Env env(*this);
            env.fund(XRP(100'000), gw, alice);
            env.fund(XRP(1'000), bob, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 500'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 500'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 500'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester EUR(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 500'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM xrp_eur(env, alice, XRP(10'100), EUR(100'000'000'000'000'000));
            AMM eur_btc(
                env,
                alice,
                EUR(100'000'000'000'000'000),
                BTC(102'000'000'000'000'000));
            AMM btc_usd(
                env,
                alice,
                BTC(101'000'000'000'000'000),
                USD(100'000'000'000'000'000));
            AMM xrp_usd(env, alice, XRP(10'150), USD(102'000'000'000'000'000));
            AMM xrp_eth(env, alice, XRP(10'000), ETH(101'000'000'000'000'000));
            AMM eth_eur(
                env,
                alice,
                ETH(109'000'000'000'000'000),
                EUR(110'000'000'000'000'000));
            AMM eur_usd(
                env,
                alice,
                EUR(101'000'000'000'000'000),
                USD(100'000'000'000'000'000));
            env(pay(bob, carol, USD(1'000'000'000'000'000)),
                path(~MPT(EUR), ~MPT(BTC), ~MPT(USD)),
                path(~MPT(USD)),
                path(~MPT(ETH), ~MPT(EUR), ~MPT(USD)),
                sendmax(XRP(200)));

            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10'026'208'900),
                ETH(10'073'6577924446075),
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                ETH(10'926'3422075553925),
                EUR(10'973'5423207872004),
                eth_eur.tokens()));
            BEAST_EXPECT(eur_usd.expectBalances(
                EUR(10'126'4576792127996),
                USD(9'973'9315171205700),
                eur_usd.tokens()));
            // XRP-USD path
            // This path provides ~73.9e12USD/74.1XRP
            BEAST_EXPECT(xrp_usd.expectBalances(
                XRPAmount(10'224'106'246),
                USD(10'126'0684828794300),
                xrp_usd.tokens()));

            // XRP-EUR-BTC-USD
            // This path doesn't provide any liquidity due to how
            // offers are generated in multi-path.
            // Analytical solution shows a different distribution:
            // XRP-EUR-BTC-USD 11.6e12USD/11.64XRP,
            // XRP-USD 60.7e12USD/60.8XRP,
            // XRP-ETH-EUR-USD 27.6e12USD/27.6XRP
            BEAST_EXPECT(xrp_eur.expectBalances(
                XRP(10'100), EUR(100'000'000'000'000'000), xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                EUR(100'000'000'000'000'000),
                BTC(102'000'000'000'000'000),
                eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                BTC(101'000'000'000'000'000),
                USD(100'000'000'000'000'000),
                btc_usd.tokens()));
            env.require(balance(carol, USD(501'000'000'000'000'000)));
        }

        // Dependent AMM
        {
            Env env(*this);
            env.fund(XRP(40'000), gw, alice);
            env.fund(XRP(1'000), bob, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 50'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 50'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 50'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester EUR(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 50'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM xrp_eur(env, alice, XRP(10'100), EUR(10'000'000'000'000'000));
            AMM eur_btc(
                env,
                alice,
                EUR(10'000'000'000'000'000),
                BTC(10'200'000'000'000'000));
            AMM btc_usd(
                env,
                alice,
                BTC(10'100'000'000'000'000),
                USD(10'000'000'000'000'000));
            AMM xrp_eth(env, alice, XRP(10'000), ETH(10'100'000'000'000'000));
            AMM eth_eur(
                env,
                alice,
                ETH(10'900'000'000'000'000),
                EUR(11'000'000'000'000'000));
            env(pay(bob, carol, USD(100'000'000'000'000)),
                path(~MPT(EUR), ~MPT(BTC), ~MPT(USD)),
                path(~MPT(ETH), ~MPT(EUR), ~MPT(BTC), ~MPT(USD)),
                sendmax(XRP(200)));

            BEAST_EXPECT(xrp_eur.expectBalances(
                XRPAmount(10'118'738'472),
                EUR(9'981'544436337922),
                xrp_eur.tokens()));
            BEAST_EXPECT(eur_btc.expectBalances(
                EUR(10'101'160967851879),
                BTC(10'097'914269680591),
                eur_btc.tokens()));
            BEAST_EXPECT(btc_usd.expectBalances(
                BTC(10'202'085730319409),
                USD(9'900'000'000'000'000),
                btc_usd.tokens()));
            BEAST_EXPECT(xrp_eth.expectBalances(
                XRPAmount(10'082'446'397),
                ETH(10'017'410727779966),
                xrp_eth.tokens()));
            BEAST_EXPECT(eth_eur.expectBalances(
                ETH(10'982'589272220034),
                EUR(10'917'294595810199),
                eth_eur.tokens()));
            env.require(balance(carol, USD(50'100'000'000'000'000)));
        }

        // AMM offers limit
        // Consuming 30 CLOB offers, results in hitting 30 AMM offers limit.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            env.fund(XRP(1'000), bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 400'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000'000'000'000'000));

            for (int i = 0; i < 30; ++i)
                env(offer(
                    alice,
                    ETH(1'000'000'000'000 + 10'000'000'000 * i),
                    XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, ETH(140'000'000'000'000), XRP(100)));
            env(pay(bob, carol, BTC(100'000'000'000'000)),
                path(~XRP, ~MPT(BTC)),
                sendmax(ETH(400'000'000'000'000)),
                txflags(tfPartialPayment | tfNoRippleDirect));

            BEAST_EXPECT(ammAlice.expectBalances(
                XRP(10'030), BTC(9'970'089730807827), ammAlice.tokens()));

            env.require(balance(carol, BTC(30'029'910269192173)));
            BEAST_EXPECT(expectOffers(
                env, alice, 1, {{{ETH(140'000'000'000'000), XRP(100)}}}));
        }

        // This payment is fulfilled
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            env.fund(XRP(1'000), bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 400'000'000'000'000,
                 .flags = MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000'000'000'000'000));

            for (int i = 0; i < 29; ++i)
                env(offer(
                    alice,
                    ETH(1'000'000'000'000 + 10'000'000'000 * i),
                    XRP(1)));
            // This is worse quality offer than 30 offers above.
            // It will not be consumed because of AMM offers limit.
            env(offer(alice, ETH(140'000'000'000'000), XRP(100)));
            env(pay(bob, carol, BTC(100'000'000'000'000)),
                path(~XRP, ~MPT(BTC)),
                sendmax(ETH(400'000'000'000'000)),
                txflags(tfPartialPayment | tfNoRippleDirect));
            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'101'010'102},
                BTC(9'900'000'000'000'000),
                ammAlice.tokens()));

            env.require(balance(carol, BTC(30'100'000'000'000'000)));
            BEAST_EXPECT(expectOffers(
                env,
                alice,
                1,
                {{{ETH(39'185857200000), XRPAmount{27'989'898}}}}));
        }

        // Offer crossing with AMM and another offer.
        // AMM has a better quality and is consumed first.
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            env.fund(XRP(1'000), bob);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 30'000'000'000'000'000,
                 .flags = MPTDEXFlags});

            env(offer(bob, XRP(100), BTC(100'001'000'000'000)));
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'100'000'000'000'000));
            env(offer(carol, BTC(100'000'000'000'000), XRP(100)));

            BEAST_EXPECT(ammAlice.expectBalances(
                XRPAmount{10'049'825'372},
                BTC(10'049'925870493030),
                ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(
                env,
                bob,
                1,
                {{{XRPAmount{50'074'628}, BTC(50'075129506970)}}}));

            env.require(balance(carol, BTC(30'100'000'000'000'000)));
        }

        // Individually locked MPT destination account
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));

            BTC.set({.holder = carol, .flags = tfMPTLock});

            env(pay(alice, carol, XRP(1)),
                path(~XRP),
                sendmax(BTC(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(tesSUCCESS));
        }

        // Individually locked MPT source account
        {
            Env env(*this);
            env.fund(XRP(30'000), gw, alice, carol);
            MPTTester BTC(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 30'000,
                 .flags = tfMPTCanLock | MPTDEXFlags});
            AMM ammAlice(env, alice, XRP(10'000), BTC(10'000));

            BTC.set({.holder = alice, .flags = tfMPTLock});

            env(pay(alice, carol, BTC(1)),
                path(~MPT(BTC)),
                sendmax(XRP(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(tesSUCCESS));
        }
    }

    void
    testAMMTokens()
    {
        testcase("AMM Tokens");
        using namespace jtx;

        // Offer crossing with AMM LPTokens and XRP.
        // AMM LPTokens come from MPT/XRP pool.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const token1 = ammAlice.lptIssue();
                auto priceXRP = ammAssetOut(
                    STAmount{XRPAmount{10'000'000'000}},
                    STAmount{token1, 10'000'000},
                    STAmount{token1, 5'000'000},
                    0);
                // Carol places an order to buy LPTokens
                env(offer(carol, STAmount{token1, 5'000'000}, priceXRP));
                // Alice places an order to sell LPTokens
                env(offer(alice, priceXRP, STAmount{token1, 5'000'000}));
                // Pool's LPTokens balance doesn't change
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRP(10'000),
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{10'000'000}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount{5'000'000}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(alice, IOUAmount{5'000'000}));
                // Carol votes
                ammAlice.vote(carol, 1'000);
                BEAST_EXPECT(ammAlice.expectTradingFee(500));
                ammAlice.vote(carol, 0);
                BEAST_EXPECT(ammAlice.expectTradingFee(0));
                // Carol bids
                env(ammAlice.bid({.account = carol, .bidMin = 100}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(carol, IOUAmount{4'999'900}));
                BEAST_EXPECT(ammAlice.expectAuctionSlot(0, 0, IOUAmount{100}));
                BEAST_EXPECT(accountBalance(env, carol) == "22499999950");
                priceXRP = ammAssetOut(
                    STAmount{XRPAmount{10'000'000'000}},
                    STAmount{token1, 9'999'900},
                    STAmount{token1, 4'999'900},
                    0);
                // Carol withdraws
                ammAlice.withdrawAll(carol, XRP(0));
                BEAST_EXPECT(accountBalance(env, carol) == "29999949939");
                BEAST_EXPECT(ammAlice.expectBalances(
                    XRPAmount{10'000'000'000} - priceXRP,
                    MPT(ammAlice[1])(10'000),
                    IOUAmount{5'000'000}));
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(alice, IOUAmount{5'000'000}));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
            },
            {{XRP(10000), AMMMPT(10000)}});

        // Offer crossing with two AMM LPTokens.
        // token1 comes from MPT/XRP pool.
        // token2 comes from XRP/IOU pool.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000);
                fund(env, gw, {alice, carol}, {EUR(10'000)}, Fund::TokenOnly);
                AMM ammAlice1(env, alice, XRP(10'000), EUR(10'000));
                ammAlice1.deposit(carol, 1'000'000);
                auto const token1 = ammAlice.lptIssue();
                auto const token2 = ammAlice1.lptIssue();
                env(offer(alice, STAmount{token1, 100}, STAmount{token2, 100}),
                    txflags(tfPassive));
                env.close();
                BEAST_EXPECT(expectOffers(env, alice, 1));
                env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
                env.close();
                BEAST_EXPECT(
                    expectLine(env, alice, STAmount{token1, 10'000'100}) &&
                    expectLine(env, alice, STAmount{token2, 9'999'900}));
                BEAST_EXPECT(
                    expectLine(env, carol, STAmount{token2, 1'000'100}) &&
                    expectLine(env, carol, STAmount{token1, 999'900}));
                BEAST_EXPECT(
                    expectOffers(env, alice, 0) && expectOffers(env, carol, 0));
            },
            {{XRP(10000), AMMMPT(10000)}});

        // LPs pay LPTokens directly. Must trust set because the trust line
        // is checked for the limit, which is 0 in the AMM auto-created
        // trust line.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const token1 = ammAlice.lptIssue();
                env.trust(STAmount{token1, 2'000'000}, carol);
                env.close();
                ammAlice.deposit(carol, 1'000'000);
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(alice, IOUAmount{10'000'000, 0}) &&
                    ammAlice.expectLPTokens(carol, IOUAmount{1'000'000, 0}));
                // Pool balance doesn't change, only tokens moved from
                // one line to another.
                env(pay(alice, carol, STAmount{token1, 100}));
                env.close();
                BEAST_EXPECT(
                    // Alice initial token1 10,000,000 - 100
                    ammAlice.expectLPTokens(alice, IOUAmount{9'999'900, 0}) &&
                    // Carol initial token1 1,000,000 + 100
                    ammAlice.expectLPTokens(carol, IOUAmount{1'000'100, 0}));

                env.trust(STAmount{token1, 20'000'000}, alice);
                env.close();
                env(pay(carol, alice, STAmount{token1, 100}));
                env.close();
                // Back to the original balance
                BEAST_EXPECT(
                    ammAlice.expectLPTokens(alice, IOUAmount{10'000'000, 0}) &&
                    ammAlice.expectLPTokens(carol, IOUAmount{1'000'000, 0}));
            },
            {{XRP(10000), AMMMPT(10000)}});
    }

    void
    testAmendment()
    {
        testcase("Amendment");
        using namespace jtx;
        FeatureBitset const feature{testable_amendments() - featureMPTokensV2};
        Env env{*this, feature};

        env.fund(XRP(30'000), gw, alice);
        env.close();
        MPT BTC = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .pay = 10'000,
             .flags = tfMPTCanClawback | tfMPTCanTransfer});

        AMM amm(env, alice, XRP(1'000), BTC(1'000), ter(temDISABLED));

        env(amm.bid({.bidMax = 1000}), ter(temMALFORMED));
        env(amm.bid({}), ter(temDISABLED));
        amm.vote(VoteArg{.tfee = 100, .err = ter(temDISABLED)});
        amm.withdraw(WithdrawArg{.tokens = 100, .err = ter(temMALFORMED)});
        amm.withdraw(WithdrawArg{.err = ter(temDISABLED)});
        amm.deposit(DepositArg{.asset1In = USD(100), .err = ter(temDISABLED)});
        amm.ammDelete(alice, ter(temDISABLED));
    }

    void
    testAMMAndCLOB(FeatureBitset features)
    {
        testcase("AMMAndCLOB, offer quality change");
        using namespace jtx;
        auto const gw = Account("gw");
        auto const LP1 = Account("LP1");
        auto const LP2 = Account("LP2");

        auto prep = [&](auto const& offerCb, auto const& expectCb) {
            Env env(*this, features);
            env.fund(XRP(30'000'000'000), gw);
            env.fund(XRP(10'000), LP1);
            env.fund(XRP(10'000), LP2);
            MPTTester TST(
                {.env = env,
                 .issuer = gw,
                 .holders = {LP1, LP2},
                 .flags = MPTDEXFlags});

            env(offer(gw, XRP(11'500'000'000), TST(1'000'000'000'000'000)));

            env(offer(LP1, TST(25'000'000), XRPAmount(287'500'000)));

            // Either AMM or CLOB offer
            offerCb(env, TST);

            env(offer(LP2, TST(25'000'000), XRPAmount(287'500'000)));

            expectCb(env, TST);
        };

        // If we replace AMM with an equivalent CLOB offer, which AMM generates
        // when it is consumed, then the result must be equivalent, too.
        STAmount lp2TSTBalance;
        std::string lp2TakerGets;
        std::string lp2TakerPays;
        // Execute with AMM first
        prep(
            [&](Env& env, MPTTester TST) {
                AMM amm(env, LP1, TST(25'000'000), XRP(250));
            },
            [&](Env& env, MPTTester TST) {
                lp2TSTBalance = env.balance(LP2, MPT(TST));
                auto const offer = getAccountOffers(env, LP2)["offers"][0u];
                lp2TakerGets = offer["taker_gets"].asString();
                lp2TakerPays = offer["taker_pays"]["value"].asString();
            });
        // Execute with CLOB offer
        prep(
            [&](Env& env, MPTTester TST) {
                env(offer(LP1, XRPAmount{18'095'131}, TST(1'687'379)),
                    txflags(tfPassive));
            },
            [&](Env& env, MPTTester TST) {
                BEAST_EXPECT(lp2TSTBalance == env.balance(LP2, MPT(TST)));
                auto const offer = getAccountOffers(env, LP2)["offers"][0u];
                BEAST_EXPECT(lp2TakerGets == offer["taker_gets"].asString());
                BEAST_EXPECT(
                    lp2TakerPays == offer["taker_pays"]["value"].asString());
            });
    }

    void
    testTradingFee(FeatureBitset features)
    {
        testcase("Trading Fee");
        using namespace jtx;

        // Single Deposit, 1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // No fee
                ammAlice.deposit(carol, MPT(ammAlice[1])(3'000));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1'000}));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(3'000));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{0}));
                env.require(balance(carol, MPT(ammAlice[1])(30'000)));
                // Set fee to 1%
                ammAlice.vote(alice, 1'000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));

                // Carol gets fewer LPToken ~994, because of the single deposit
                // fee
                ammAlice.deposit(carol, MPT(ammAlice[1])(3'000));
                BEAST_EXPECT(ammAlice.expectLPTokens(
                    carol, IOUAmount{994'981155689671, -12}));
                env.require(balance(carol, MPT(ammAlice[1])(27'000)));

                // Set fee to 0
                ammAlice.vote(alice, 0);
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));

                // Carol gets back less than the original deposit
                if (!features[fixAMMv1_3])
                    env.require(balance(carol, MPT(ammAlice[1])(29'995)));
                else
                    env.require(balance(carol, MPT(ammAlice[1])(29'994)));
            },
            {{USD(1000), AMMMPT(1000)}},
            0,
            std::nullopt,
            {features});

        // Single deposit with EP not exceeding specified:
        // 100MPT with EP not to exceed 0.1 (AssetIn/TokensOut). 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
                auto const balance = env.balance(carol, MPT(ammAlice[1]));
                auto const tokensFee = ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(1000),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 1, -1});
                auto const deposit =
                    balance - env.balance(carol, MPT(ammAlice[1]));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                ammAlice.vote(alice, 0);
                BEAST_EXPECT(ammAlice.expectTradingFee(0));
                auto const tokensNoFee = ammAlice.deposit(carol, deposit);
                BEAST_EXPECT(tokensFee == IOUAmount(485636'0611129, -7));
                if (!features[fixAMMv1_3])
                    BEAST_EXPECT(tokensNoFee == IOUAmount(487659'8005807, -7));
                else
                    BEAST_EXPECT(tokensNoFee == IOUAmount(487612'21584827, -8));
            },
            {{XRP(10'000), AMMMPT(10'000)}},
            1'000,
            std::nullopt,
            {features});

        // Single deposit with EP not exceeding specified:
        // 200MPT with EP not to exceed 0.002020 (AssetIn/TokensOut). 1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
                auto const balance = env.balance(carol, MPT(ammAlice[1]));
                auto const tokensFee = ammAlice.deposit(
                    carol,
                    MPT(ammAlice[1])(200),
                    std::nullopt,
                    STAmount{ammAlice.lptIssue(), 2020, -6});
                auto const deposit =
                    balance - env.balance(carol, MPT(ammAlice[1]));
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                ammAlice.vote(alice, 0);
                BEAST_EXPECT(ammAlice.expectTradingFee(0));
                auto const tokensNoFee = ammAlice.deposit(carol, deposit);
                if (!features[fixAMMv1_3])
                {
                    BEAST_EXPECT(tokensFee == IOUAmount(98'019'80198019, -8));
                    BEAST_EXPECT(tokensNoFee == IOUAmount(98'495'13933556, -8));
                }
                else
                {
                    BEAST_EXPECT(tokensFee == IOUAmount(97527'05893345, -8));
                    BEAST_EXPECT(tokensNoFee == IOUAmount(98000'10293049, -8));
                }
            },
            {{XRP(10'000), AMMMPT(10'000)}},
            1'000,
            std::nullopt,
            {features});

        // Single Withdrawal, 1% fee
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                // No fee
                ammAlice.deposit(carol, MPT(ammAlice[1])(3'000));
                BEAST_EXPECT(ammAlice.expectLPTokens(carol, IOUAmount{1'000}));
                env.require(balance(carol, MPT(ammAlice[1])(27'000)));
                // Set fee to 1%
                ammAlice.vote(alice, 1'000);
                BEAST_EXPECT(ammAlice.expectTradingFee(1'000));
                // Single withdrawal. Carol gets ~5USD less than deposited.
                ammAlice.withdrawAll(carol, MPT(ammAlice[1])(0));
                if (!features[fixAMMv1_3])
                    env.require(balance(carol, MPT(ammAlice[1])(29'995)));
                else
                    env.require(balance(carol, MPT(ammAlice[1])(29'994)));
            },
            {{USD(1000), AMMMPT(1000)}},
            0,
            std::nullopt,
            {features});

        // Withdraw with EPrice limit, 1% fee.
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                ammAlice.deposit(carol, 1'000'000);
                auto const tokensFee = ammAlice.withdraw(
                    carol,
                    MPT(ammAlice[1])(100),
                    std::nullopt,
                    IOUAmount{520, 0});
                env.require(balance(carol, MPT(ammAlice[1])(30443)));

                // Set to original pool size
                auto const deposit = env.balance(carol, MPT(ammAlice[1])) -
                    MPT(ammAlice[1])(29'000);
                ammAlice.deposit(carol, deposit);
                // fee 0%
                ammAlice.vote(alice, 0);
                BEAST_EXPECT(ammAlice.expectTradingFee(0));
                auto const tokensNoFee = ammAlice.withdraw(carol, deposit);
                if (!features[fixAMMv1_3])
                    env.require(balance(carol, MPT(ammAlice[1])(30443)));
                else
                    env.require(balance(carol, MPT(ammAlice[1])(30442)));
                BEAST_EXPECT(tokensNoFee == IOUAmount(746'327'46496649, -8));
                BEAST_EXPECT(tokensFee == IOUAmount(750'588'23529411, -8));
            },
            {{XRP(10'000), AMMMPT(10'000)}},
            1'000,
            std::nullopt,
            {features});

        // Payment, 1% fee
        {
            Env env{*this, features};
            env.fund(XRP(30'000), gw, alice, bob, carol);
            env.close();
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .pay = 30'000,
                 .flags = MPTDEXFlags});

            auto const USD = gw["USD"];
            env.trust(USD(30'000), alice);
            env(pay(gw, alice, USD(30'000)));
            env.trust(USD(30'000), bob);
            env(pay(gw, bob, USD(1'000)));
            env.trust(USD(30'000), carol);
            env(pay(gw, carol, USD(30'000)));
            env.close();

            AMM amm(env, alice, BTC(1000), USD(1010));

            env.require(balance(alice, BTC(29'000)));
            env.require(balance(alice, USD(28'990)));
            env.require(balance(carol, BTC(30'000)));

            // Carol pays to Alice with no fee
            env(pay(carol, alice, USD(10)),
                path(~USD),
                sendmax(BTC(10)),
                txflags(tfNoRippleDirect));
            env.close();

            // Alice has 10USD more and Carol has 10BTC less
            env.require(balance(alice, BTC(29'000)));
            env.require(balance(alice, USD(29'000)));
            env.require(balance(carol, BTC(29'990)));

            // Set fee to 1%
            amm.vote(alice, 1'000);
            BEAST_EXPECT(amm.expectTradingFee(1'000));
            // Bob pays to Carol with 1% fee
            env(pay(bob, carol, BTC(10)),
                path(~BTC),
                sendmax(USD(15)),
                txflags(tfNoRippleDirect));
            env.close();
            // Bob sends 10.1~USD to pay 10BTC
            env.require(
                balance(bob, STAmount{USD, UINT64_C(989'8989898989899), -13}));
            // Carol got 10BTC
            env.require(balance(carol, BTC(30'000)));
            BEAST_EXPECT(amm.expectBalances(
                BTC(1'000),
                STAmount{USD, UINT64_C(1'010'10101010101), -11},
                amm.tokens()));
        }

        // Offer crossing, 0.05% fee MPT/XRP
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const BTC = MPT(ammAlice[1]);
                env(offer(carol, BTC(10), XRP(10)));
                env.close();
                env.require(balance(carol, BTC(30'010)));
                env.require(balance(carol, XRP(29989.99998)));

                // Change pool composition back
                env(offer(carol, XRP(10), BTC(10)));
                env.close();
                env.require(balance(carol, BTC(30'000)));
                env.require(balance(carol, XRP(29999.99997)));

                // set fee to 0.05%
                ammAlice.vote(alice, 50);
                BEAST_EXPECT(ammAlice.expectTradingFee(50));
                env(offer(carol, BTC(10), XRP(10)));
                env.close();
                env.require(balance(carol, BTC(30'009)));
                env.require(balance(carol, XRP(29991.004453)));
                BEAST_EXPECT(
                    expectOffers(env, carol, 1, {{Amounts{BTC(1), XRP(1)}}}));
            },
            {{XRP(1000), AMMMPT(1010)}},
            0,
            std::nullopt,
            {features});

        // Offer crossing, 0.5% fee MPT/IOU
        testAMM(
            [&](AMM& ammAlice, Env& env) {
                auto const BTC = MPT(ammAlice[1]);
                env(offer(carol, BTC(10'000'000), USD(10)));
                env.close();
                env.require(balance(carol, BTC(1'020'001'000)));
                env.require(balance(carol, USD(29'990)));

                // Change pool composition back
                env(offer(carol, USD(10), BTC(10'000'000)));
                env.close();
                env.require(balance(carol, BTC(1'010'001'000)));
                env.require(balance(carol, USD(30'000)));

                // set fee to 0.5%
                ammAlice.vote(alice, 500);
                BEAST_EXPECT(ammAlice.expectTradingFee(500));
                env(offer(carol, BTC(10'000'000), USD(10)));
                env.close();
                env.require(balance(carol, BTC(1'014'975'874)));
                env.require(balance(
                    carol, STAmount{USD, UINT64_C(29'995'02512600184), -11}));
                BEAST_EXPECT(expectOffers(
                    env,
                    carol,
                    1,
                    {{Amounts{
                        BTC(5'025126),
                        STAmount{USD, UINT64_C(5'025126), -6}}}}));
            },
            {{USD(1000), AMMMPT(1010000000)}},
            0,
            std::nullopt,
            {features});

        // Payment with AMM and CLOB offer, 0 fee
        // AMM liquidity is consumed first up to CLOB offer quality
        // CLOB offer is fully consumed next
        // Remaining amount is consumed via AMM liquidity
        {
            Env env{*this, features};
            Account const ed("ed");
            fund(env, gw, {alice, bob, carol, ed}, XRP(30'000), {USD(2'000)});
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .pay = 30'000'000000,
                 .flags = MPTDEXFlags});

            env(offer(carol, BTC(5'000000), USD(5)));
            AMM ammAlice(env, alice, USD(1'005), BTC(1'000'000000));
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(BTC(15'000000)),
                txflags(tfNoRippleDirect));

            env.require(balance(ed, USD(2'010)));
            env.require(balance(bob, BTC(29'989'999999)));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(1'005'000001), USD(1'000), ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. Same as above but with 0.25%
        // fee.
        {
            Env env{*this, features};
            Account const ed("ed");
            fund(env, gw, {alice, bob, carol, ed}, XRP(30'000), {USD(2'000)});
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .pay = 30'000'000000,
                 .flags = MPTDEXFlags});

            env(offer(carol, BTC(5'000000), USD(5)));
            // Set 0.25% fee
            AMM ammAlice(env, alice, USD(1'005), BTC(1'000'000000), false, 250);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(BTC(15'000000)),
                txflags(tfNoRippleDirect));
            env.require(balance(ed, USD(2'010)));
            env.require(balance(bob, BTC(29'989'987453)));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(1'005'012547), USD(1'000), ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. AMM has a better
        // spot price quality, but 1% fee offsets that. As the result
        // the entire trade is executed via LOB.
        {
            Env env{*this, features};
            Account const ed("ed");
            fund(env, gw, {alice, bob, carol, ed}, XRP(30'000), {USD(2'000)});
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .pay = 30'000'000000,
                 .flags = MPTDEXFlags});

            env(offer(carol, BTC(10'000000), USD(10)));
            // Set 1% fee
            AMM ammAlice(
                env, alice, USD(1'005), BTC(1'000'000000), false, 1'000);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(BTC(15'000000)),
                txflags(tfNoRippleDirect));
            env.require(balance(ed, USD(2'010)));
            env.require(balance(bob, BTC(29'990'000000)));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(1'000'000000), USD(1'005), ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // Payment with AMM and CLOB offer. AMM has a better
        // spot price quality, but 1% fee offsets that.
        // The CLOB offer is consumed first and the remaining
        // amount is consumed via AMM liquidity.
        {
            Env env{*this, features};
            Account const ed("ed");
            fund(env, gw, {alice, bob, carol, ed}, XRP(30'000), {USD(2'000)});
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .pay = 30'000'000000,
                 .flags = MPTDEXFlags});

            env(offer(carol, BTC(9'000000), USD(9)));
            // Set 1% fee
            AMM ammAlice(
                env, alice, USD(1'005), BTC(1'000'000000), false, 1'000);
            env(pay(bob, ed, USD(10)),
                path(~USD),
                sendmax(BTC(15'000000)),
                txflags(tfNoRippleDirect));
            env.require(balance(ed, USD(2'010)));
            env.require(balance(bob, BTC(29'989'993923)));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(1'001'006077), USD(1'004), ammAlice.tokens()));
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }
    }

    void
    testAdjustedTokens(FeatureBitset features)
    {
        testcase("Adjusted Deposit/Withdraw Tokens");
        using namespace jtx;

        // Deposit/Withdraw USD from USD/MPT pool
        {
            Env env(*this);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob("bob");
            Account carol("carol");
            Account ed("ed");
            Account paul("paul");
            Account dan("dan");
            Account chris("chris");
            Account simon("simon");
            Account ben("ben");
            Account nataly("nataly");
            std::vector<Account> const holders{
                alice, bob, carol, ed, paul, dan, chris, simon, ben, nataly};
            env.fund(
                XRP(100000),
                gw,
                alice,
                bob,
                carol,
                ed,
                paul,
                dan,
                chris,
                simon,
                ben,
                nataly);
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = holders,
                 .pay = 40'000'000000,
                 .flags = MPTDEXFlags});

            auto const USD = gw["USD"];
            for (auto const& holder : holders)
            {
                env.trust(USD(1'500'000), holder);
                env(pay(gw, holder, USD(1'500'000)));
            }
            env.close();

            auto aliceUSD = env.balance(alice, USD);
            auto bobUSD = env.balance(bob, USD);
            auto carolUSD = env.balance(carol, USD);
            auto edUSD = env.balance(ed, USD);
            auto paulUSD = env.balance(paul, USD);
            auto danUSD = env.balance(dan, USD);
            auto chrisUSD = env.balance(chris, USD);
            auto simonUSD = env.balance(simon, USD);
            auto benUSD = env.balance(ben, USD);
            auto natalyUSD = env.balance(nataly, USD);

            AMM ammAlice(env, alice, BTC(10'000'000000), USD(10000));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(10'000'000000), USD(10'000), IOUAmount{10'000'000}));

            for (int i = 0; i < 10; ++i)
            {
                ammAlice.deposit(ben, STAmount{USD, 1, -10});
                ammAlice.withdrawAll(ben, USD(0));
                ammAlice.deposit(simon, USD(0.1));
                ammAlice.withdrawAll(simon, USD(0));
                ammAlice.deposit(chris, USD(1));
                ammAlice.withdrawAll(chris, USD(0));
                ammAlice.deposit(dan, USD(10));
                ammAlice.withdrawAll(dan, USD(0));
                ammAlice.deposit(bob, USD(100));
                ammAlice.withdrawAll(bob, USD(0));
                ammAlice.deposit(carol, USD(1'000));
                ammAlice.withdrawAll(carol, USD(0));
                ammAlice.deposit(ed, USD(10'000));
                ammAlice.withdrawAll(ed, USD(0));
                ammAlice.deposit(paul, USD(100'000));
                ammAlice.withdrawAll(paul, USD(0));
                ammAlice.deposit(nataly, USD(1'000'000));
                ammAlice.withdrawAll(nataly, USD(0));
            }

            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(10'000'000000),
                STAmount{USD, UINT64_C(10000'0000000003), -10},
                IOUAmount{10'000'000}));

            env.require(balance(bob, bobUSD));
            env.require(balance(carol, carolUSD));
            env.require(balance(ed, edUSD));
            env.require(balance(paul, paulUSD));
            env.require(balance(dan, danUSD));
            env.require(balance(chris, chrisUSD));
            env.require(balance(simon, simonUSD));
            env.require(balance(ben, benUSD));
            env.require(balance(nataly, natalyUSD));

            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());
            env.require(balance(alice, aliceUSD));
        }

        // Same as above but deposit/withdraw MPT from USD/MPT pool
        {
            Env env(*this);
            Account gw{"gateway"};
            Account alice{"alice"};
            Account bob("bob");
            Account carol("carol");
            Account ed("ed");
            Account paul("paul");
            Account dan("dan");
            Account chris("chris");
            Account simon("simon");
            Account ben("ben");
            Account nataly("nataly");
            std::vector<Account> const holders{
                alice, bob, carol, ed, paul, dan, chris, simon, ben, nataly};
            env.fund(
                XRP(100000),
                gw,
                alice,
                bob,
                carol,
                ed,
                paul,
                dan,
                chris,
                simon,
                ben,
                nataly);
            env.close();

            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = holders,
                 .pay = 40'000'000000,
                 .flags = MPTDEXFlags});

            auto const USD = gw["USD"];
            for (auto const& holder : holders)
            {
                env.trust(USD(1'500'000), holder);
                env(pay(gw, holder, USD(1'500'000)));
            }
            env.close();

            auto aliceBTC = env.balance(alice, BTC);
            auto bobBTC = env.balance(bob, BTC);
            auto carolBTC = env.balance(carol, BTC);
            auto edBTC = env.balance(ed, BTC);
            auto paulBTC = env.balance(paul, BTC);
            auto danBTC = env.balance(dan, BTC);
            auto chrisBTC = env.balance(chris, BTC);
            auto simonBTC = env.balance(simon, BTC);
            auto benBTC = env.balance(ben, BTC);
            auto natalyBTC = env.balance(nataly, BTC);

            AMM ammAlice(env, alice, BTC(10'000'000000), USD(10000));
            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(10'000'000000), USD(10'000), IOUAmount{10'000'000}));

            for (int i = 0; i < 10; ++i)
            {
                ammAlice.deposit(ben, BTC(1));
                ammAlice.withdrawAll(ben, BTC(0));
                ammAlice.deposit(simon, BTC(1'000));
                ammAlice.withdrawAll(simon, BTC(0));
                ammAlice.deposit(chris, BTC(1));
                ammAlice.withdrawAll(chris, BTC(0));
                ammAlice.deposit(dan, BTC(10));
                ammAlice.withdrawAll(dan, BTC(0));
                ammAlice.deposit(bob, BTC(100));
                ammAlice.withdrawAll(bob, BTC(0));
                ammAlice.deposit(carol, BTC(1'000));
                ammAlice.withdrawAll(carol, BTC(0));
                ammAlice.deposit(ed, BTC(10'000));
                ammAlice.withdrawAll(ed, BTC(0));
                ammAlice.deposit(paul, BTC(100'000));
                ammAlice.withdrawAll(paul, BTC(0));
                ammAlice.deposit(nataly, BTC(1'000'000));
                ammAlice.withdrawAll(nataly, BTC(0));
            }

            BEAST_EXPECT(ammAlice.expectBalances(
                BTC(10'000'000090), USD(10'000), IOUAmount{10'000'000}));

            env.require(balance(bob, bobBTC - BTC(10)));
            env.require(balance(carol, carolBTC - BTC(10)));
            env.require(balance(ed, edBTC - BTC(10)));
            env.require(balance(paul, paulBTC - BTC(10)));
            env.require(balance(dan, danBTC - BTC(10)));
            env.require(balance(chris, chrisBTC - BTC(10)));
            env.require(balance(simon, simonBTC - BTC(10)));
            env.require(balance(ben, benBTC - BTC(10)));
            env.require(balance(nataly, natalyBTC - BTC(10)));

            ammAlice.withdrawAll(alice);
            BEAST_EXPECT(!ammAlice.ammExists());
            env.require(balance(alice, aliceBTC + BTC(90)));
        }
    }

    void
    testAMMID()
    {
        testcase("AMMID");
        using namespace jtx;

        // MPT/XRP
        testAMM(
            [&](AMM& amm, Env& env) {
                amm.setClose(false);
                auto const info = env.rpc(
                    "json",
                    "account_info",
                    std::string(
                        "{\"account\": \"" + to_string(amm.ammAccount()) +
                        "\"}"));
                try
                {
                    BEAST_EXPECT(
                        info[jss::result][jss::account_data][jss::AMMID]
                            .asString() == to_string(amm.ammID()));
                }
                catch (...)
                {
                    fail();
                }

                amm.deposit(carol, 1'000);

                auto affected = env.meta()->getJson(
                    JsonOptions::none)[sfAffectedNodes.fieldName];
                try
                {
                    bool found = false;
                    for (auto const& node : affected)
                    {
                        if (node.isMember(sfModifiedNode.fieldName) &&
                            node[sfModifiedNode.fieldName]
                                [sfLedgerEntryType.fieldName]
                                    .asString() == "AccountRoot" &&
                            node[sfModifiedNode.fieldName]
                                [sfFinalFields.fieldName][jss::Account]
                                    .asString() == to_string(amm.ammAccount()))
                        {
                            found =
                                node[sfModifiedNode.fieldName]
                                    [sfFinalFields.fieldName][jss::AMMID]
                                        .asString() == to_string(amm.ammID());
                            break;
                        }
                    }
                    BEAST_EXPECT(found);
                }
                catch (...)
                {
                    fail();
                }
            },
            {{XRP(1000), AMMMPT(1000'000)}});
    }

    void
    testSelection(FeatureBitset features)
    {
        testcase("Offer/Strand Selection");
        using namespace jtx;
        Account const ed("ed");
        Account const gw1("gw1");

        // These tests are expected to fail if the OwnerPaysFee feature
        // is ever supported. Updates will need to be made to AMM handling
        // in the payment engine, and these tests will need to be updated.

        struct MPTList
        {
            MPTTester const USD;
            MPTTester const ETH;
            MPTTester const CAN;
        };

        auto prep = [&](Env& env,
                        uint16_t gwTransferFee,
                        uint16_t gw1TransferFee) -> MPTList {
            env.fund(XRP(2'000), gw, gw1, alice, bob, carol, ed);
            MPTTester USD(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = gwTransferFee,
                 .pay = 2'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester ETH(
                {.env = env,
                 .issuer = gw1,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = gw1TransferFee,
                 .pay = 2'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            MPTTester CAN(
                {.env = env,
                 .issuer = gw1,
                 .holders = {alice, bob, carol, ed},
                 .transferFee = gw1TransferFee,
                 .pay = 2'000'000'000'000'000,
                 .flags = MPTDEXFlags});
            env.close();

            return MPTList{
                .USD = std::move(USD),
                .ETH = std::move(ETH),
                .CAN = std::move(CAN),
            };
        };

        for (auto const& rates :
             {std::make_pair(10'000, 50'000), std::make_pair(50'000, 10'000)})
        {
            // Offer Selection

            // Cross-currency payment: AMM has the same spot price quality
            // as CLOB's offer and can't generate a better quality offer.
            // The transfer fee in this case doesn't change the CLOB quality
            // because trIn is ignored on adjustment and trOut on payment is
            // also ignored because ownerPaysTransferFee is false in this
            // case. Run test for 0) offer, 1) AMM, 2) offer and AMM to
            // verify that the quality is better in the first case, and CLOB
            // is selected in the second case.
            {
                std::array<Quality, 3> q;
                for (auto i = 0; i < 3; ++i)
                {
                    Env env(*this, features);
                    auto mpts = prep(env, rates.first, rates.second);
                    auto USD = mpts.USD;
                    auto ETH = mpts.ETH;
                    auto CAN = mpts.CAN;
                    std::optional<AMM> amm;

                    if (i == 0 || i == 2)
                    {
                        env(offer(
                                ed,
                                ETH(400'000'000'000'000),
                                USD(400'000'000'000'000)),
                            txflags(tfPassive));
                        env.close();
                    }
                    if (i > 0)
                        amm.emplace(
                            env,
                            ed,
                            USD(1'000'000'000'000'000),
                            ETH(1'000'000'000'000'000));
                    env(pay(carol, bob, USD(100'000'000'000'000)),
                        path(~MPT(USD)),
                        sendmax(ETH(500'000'000'000'000)));
                    env.close();
                    // CLOB and AMM, AMM is not selected
                    if (i == 2)
                    {
                        BEAST_EXPECT(amm->expectBalances(
                            USD(1'000'000'000'000'000),
                            ETH(1'000'000'000'000'000),
                            amm->tokens()));
                    }
                    env.require(balance(bob, USD(2'100'000'000'000'000)));
                    q[i] = Quality(Amounts{
                        ETH(2'000'000'000'000'000) -
                            env.balance(carol, MPT(ETH)),
                        env.balance(bob, MPT(USD)) -
                            USD(2'000'000'000'000'000)});
                }
                // CLOB is better quality than AMM
                BEAST_EXPECT(q[0] > q[1]);
                // AMM is not selected with CLOB
                BEAST_EXPECT(q[0] == q[2]);
            }
            // Offer crossing: AMM has the same spot price quality
            // as CLOB's offer and can't generate a better quality offer.
            // The transfer fee in this case doesn't change the CLOB quality
            // because the quality adjustment is ignored for the offer
            // crossing.
            for (auto i = 0; i < 3; ++i)
            {
                Env env(*this, features);
                auto mpts = prep(env, rates.first, rates.second);
                auto USD = mpts.USD;
                auto ETH = mpts.ETH;
                auto CAN = mpts.CAN;
                std::optional<AMM> amm;
                if (i == 0 || i == 2)
                {
                    env(offer(
                            ed,
                            ETH(400'000'000'000'000),
                            USD(400'000'000'000'000)),
                        txflags(tfPassive));
                    env.close();
                }
                if (i > 0)
                    amm.emplace(
                        env,
                        ed,
                        USD(1'000'000'000'000'000),
                        ETH(1'000'000'000'000'000));
                env(offer(
                    alice, USD(400'000'000'000'000), ETH(400'000'000'000'000)));
                env.close();
                // AMM is not selected
                if (i > 0)
                {
                    BEAST_EXPECT(amm->expectBalances(
                        USD(1'000'000'000'000'000),
                        ETH(1'000'000'000'000'000),
                        amm->tokens()));
                }
                if (i == 0 || i == 2)
                {
                    // Fully crosses
                    BEAST_EXPECT(expectOffers(env, alice, 0));
                }
                // Fails to cross because AMM is not selected
                else
                {
                    BEAST_EXPECT(expectOffers(
                        env,
                        alice,
                        1,
                        {Amounts{
                            USD(400'000'000'000'000),
                            ETH(400'000'000'000'000)}}));
                }
                BEAST_EXPECT(expectOffers(env, ed, 0));
            }

            // Show that the CLOB quality reduction
            // results in AMM offer selection.

            // Same as the payment but reduced offer quality
            {
                std::array<Quality, 3> q;
                for (auto i = 0; i < 3; ++i)
                {
                    Env env(*this, features);
                    auto mpts = prep(env, rates.first, rates.second);
                    auto USD = mpts.USD;
                    auto ETH = mpts.ETH;
                    auto CAN = mpts.CAN;
                    std::optional<AMM> amm;
                    if (i == 0 || i == 2)
                    {
                        env(offer(
                                ed,
                                ETH(400'000'000'000'000),
                                USD(330'000'000'000'000)),
                            txflags(tfPassive));
                        env.close();
                    }
                    if (i > 0)
                        amm.emplace(
                            env,
                            ed,
                            USD(1'000'000'000'000'000),
                            ETH(1'000'000'000'000'000));
                    env(pay(carol, bob, USD(100'000'000'000'000)),
                        path(~MPT(USD)),
                        sendmax(ETH(500'000'000'000'000)));
                    env.close();
                    // AMM and CLOB are selected
                    if (i > 0)
                    {
                        BEAST_EXPECT(!amm->expectBalances(
                            USD(1'000'000'000'000'000),
                            ETH(1'000'000'000'000'000),
                            amm->tokens()));
                    }

                    if (i == 2)
                    {
                        if (rates.first == 10'000)
                        {
                            BEAST_EXPECT(expectOffers(
                                env,
                                ed,
                                1,
                                {{Amounts{
                                    ETH(377'824'113'661'517),
                                    USD(311'704'893'770'751),
                                }}}));
                        }
                        else
                        {
                            BEAST_EXPECT(expectOffers(
                                env,
                                ed,
                                1,
                                {{Amounts{
                                    ETH(329'339'265'176'670),
                                    USD(271'704'893'770'752),
                                }}}));
                        }
                    }
                    env.require(balance(bob, USD(2'100'000'000'000'000)));
                    q[i] = Quality(Amounts{
                        ETH(2'000'000'000'000'000) -
                            env.balance(carol, MPT(ETH)),
                        env.balance(bob, MPT(USD)) -
                            USD(2'000'000'000'000'000)});
                }
                // AMM is better quality
                BEAST_EXPECT(q[1] > q[0]);
                // AMM and CLOB produce better quality
                BEAST_EXPECT(q[2] > q[1]);
            }

            // Same as the offer-crossing but reduced offer quality
            for (auto i = 0; i < 3; ++i)
            {
                Env env(*this, features);
                auto mpts = prep(env, rates.first, rates.second);
                auto USD = mpts.USD;
                auto ETH = mpts.ETH;
                auto CAN = mpts.CAN;
                std::optional<AMM> amm;
                if (i == 0 || i == 2)
                {
                    env(offer(
                            ed,
                            ETH(400'000'000'000'000),
                            USD(325'000'000'000'000)),
                        txflags(tfPassive));
                    env.close();
                }
                if (i > 0)
                    amm.emplace(
                        env,
                        ed,
                        USD(1'000'000'000'000'000),
                        ETH(1'000'000'000'000'000));
                env(offer(
                    alice, USD(325'000'000'000'000), ETH(400'000'000'000'000)));
                env.close();
                // AMM is selected in both cases
                if (i > 0)
                {
                    BEAST_EXPECT(!amm->expectBalances(
                        USD(1'000'000'000'000'000),
                        ETH(1'000'000'000'000'000),
                        amm->tokens()));
                }
                // Partially crosses, AMM is selected, CLOB fails
                // limitQuality
                if (i == 2)
                {
                    if (rates.first == 10'000)
                    {
                        // Ed offer is partially crossed.
                        // The updated rounding makes limitQuality
                        // work if both amendments are enabled
                        BEAST_EXPECT(expectOffers(
                            env,
                            ed,
                            1,
                            {{Amounts{
                                ETH(121'368'838'318'772),
                                USD(98'612'181'134'002),
                            }}}));
                        BEAST_EXPECT(expectOffers(env, alice, 0));
                    }
                    else
                    {
                        // Ed offer is partially crossed.
                        BEAST_EXPECT(expectOffers(
                            env,
                            ed,
                            1,
                            {{Amounts{
                                ETH(121'368'838'318'772),
                                USD(98'612'181'134'002),
                            }}}));
                        BEAST_EXPECT(expectOffers(env, alice, 0));
                    }
                }
            }

            // Strand selection

            // Two book steps strand quality is 1.
            // AMM strand's best quality is equal to AMM's spot price
            // quality, which is 1. Both strands (steps) are adjusted
            // for the transfer fee in qualityUpperBound. In case
            // of two strands, AMM offers have better quality and are
            // consumed first, remaining liquidity is generated by CLOB
            // offers. Liquidity from two strands is better in this case
            // than in case of one strand with two book steps. Liquidity
            // from one strand with AMM has better quality than either one
            // strand with two book steps or two strands. It may appear
            // unintuitive, but one strand with AMM is optimized and
            // generates one AMM offer, while in case of two strands,
            // multiple AMM offers are generated, which results in slightly
            // worse overall quality.
            {
                std::array<Quality, 3> q;
                for (auto i = 0; i < 3; ++i)
                {
                    Env env(*this, features);
                    auto mpts = prep(env, rates.first, rates.second);
                    auto USD = mpts.USD;
                    auto ETH = mpts.ETH;
                    auto CAN = mpts.CAN;
                    std::optional<AMM> amm;

                    if (i == 0 || i == 2)
                    {
                        env(offer(
                                ed,
                                ETH(400'000'000'000'000),
                                CAN(375'000'000'000'000)),
                            txflags(tfPassive));
                        env(offer(
                            ed,
                            CAN(375'000'000'000'000),
                            USD(338'000'000'000'000))),
                            txflags(tfPassive);
                    }

                    if (i > 0)
                        amm.emplace(
                            env,
                            ed,
                            ETH(1'000'000'000'000'000),
                            USD(1'000'000'000'000'000));

                    env(pay(carol, bob, USD(100'000'000'000'000)),
                        path(~MPT(USD)),
                        path(~MPT(CAN), ~MPT(USD)),
                        sendmax(ETH(600'000'000'000'000)));
                    env.close();

                    env.require(balance(bob, USD(2'100'000'000'000'000)));

                    if (i == 2)
                    {
                        if (rates.first == 10'000)
                        {
                            // Liquidity is consumed from AMM strand only
                            BEAST_EXPECT(amm->expectBalances(
                                ETH(1'124'584'914'606'399),
                                USD(889'999'999'999'992),
                                amm->tokens()));
                        }
                        else
                        {
                            BEAST_EXPECT(amm->expectBalances(
                                ETH(1'103'724'137'931'003),
                                USD(906'023'494'126'504),
                                amm->tokens()));
                            BEAST_EXPECT(expectOffers(
                                env,
                                ed,
                                2,
                                {{Amounts{
                                      ETH(327'070'007'645'972),
                                      CAN(306'628'132'168'098),
                                  },
                                  Amounts{
                                      CAN(312'843'756'516'453),
                                      USD(281'976'505'873'496),
                                  }}}));
                        }
                    }
                    q[i] = Quality(Amounts{
                        ETH(2'000'000'000'000'000) -
                            env.balance(carol, MPT(ETH)),
                        env.balance(bob, MPT(USD)) -
                            USD(2'000'000'000'000'000)});
                }
                BEAST_EXPECT(q[1] > q[0]);
                BEAST_EXPECT(q[2] > q[0] && q[2] < q[1]);
            }
        }
    }

    void
    testMalformed()
    {
        testcase("Malformed");
        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .flags = tfSingleAsset,
                    .err = ter(temMALFORMED),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .flags = tfOneAssetLPToken,
                    .err = ter(temMALFORMED),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .flags = tfLimitLPToken,
                    .err = ter(temMALFORMED),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .asset1Out = MPT(ammAlice[1])(100),
                    .asset2Out = MPT(ammAlice[1])(100),
                    .err = ter(temBAD_AMM_TOKENS),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                MPTTester BTC(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol},
                     .pay = 2'000,
                     .flags = tfMPTCanLock | MPTDEXFlags});
                WithdrawArg args{
                    .asset1Out = XRP(100),
                    .asset2Out = BTC(100),
                    .err = ter(temBAD_AMM_TOKENS),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(10'000), AMMMPT(10'000)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Flags] = tfLimitLPToken;
                jv[jss::Account] = alice.human();
                ammAlice.setTokens(jv);
                XRP(100).value().setJson(jv[jss::Amount]);
                MPTTester BTC(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol},
                     .pay = 2'000,
                     .flags = tfMPTCanLock | MPTDEXFlags});
                BTC(100).value().setJson(jv[jss::EPrice]);
                env(jv, ter(telENV_RPC_FAILED));
            },
            {{XRP(10'000), AMMMPT(10'000)}});
    }

    void
    testFixAMMOfferBlockedByLOB(FeatureBitset features)
    {
        testcase("AMM Offer Blocked By LOB");
        using namespace jtx;

        // Low quality LOB offer blocks AMM liquidity

        // USD/MPT crosses AMM despite of low quality LOB
        {
            Env env(*this, features);

            fund(env, gw, {alice, carol}, XRP(1'000'000), {USD(1'000'000)});
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 40'000'000000,
                 .flags = MPTDEXFlags});

            env(offer(alice, BTC(1), USD(0.01)));
            env.close();
            AMM amm(env, gw, BTC(200'000), USD(100'000));
            env(offer(carol, USD(0.49), BTC(1)));
            env.close();

            if (!features[fixAMMv1_1] && !features[fixAMMv1_3])
            {
                BEAST_EXPECT(amm.expectBalances(
                    BTC(200'000), USD(100'000), amm.tokens()));
                BEAST_EXPECT(expectOffers(
                    env, alice, 1, {{Amounts{BTC(1), USD(0.01)}}}));
                BEAST_EXPECT(expectOffers(
                    env, carol, 1, {{Amounts{USD(0.49), BTC(1)}}}));
            }

            if (features[fixAMMv1_1] && features[fixAMMv1_3])
            {
                BEAST_EXPECT(amm.expectBalances(
                    BTC(200'001), USD(99'999.51), amm.tokens()));
                BEAST_EXPECT(expectOffers(
                    env, alice, 1, {{Amounts{BTC(1), USD(0.01)}}}));
                // Carol's offer crosses AMM
                BEAST_EXPECT(expectOffers(env, carol, 0));
            }
        }

        // XRP/MPT crosses AMM despite of low quality LOB
        {
            Env env(*this, features);

            fund(env, gw, {alice, carol}, XRP(1'000'000), {USD(1'000'000)});
            MPT BTC = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .pay = 40'000'000000,
                 .flags = MPTDEXFlags});

            env(offer(alice, BTC(1), XRP(0.01)));
            env.close();
            AMM amm(env, gw, BTC(200'000), XRP(100'000));
            env(offer(carol, XRP(0.49), BTC(1)));
            env.close();

            if (!features[fixAMMv1_1] && !features[fixAMMv1_3])
            {
                BEAST_EXPECT(amm.expectBalances(
                    BTC(200'000), XRP(100'000), amm.tokens()));
                BEAST_EXPECT(expectOffers(
                    env, alice, 1, {{Amounts{BTC(1), XRP(0.01)}}}));
                BEAST_EXPECT(expectOffers(
                    env, carol, 1, {{Amounts{XRP(0.49), BTC(1)}}}));
            }

            if (features[fixAMMv1_1] && features[fixAMMv1_3])
            {
                BEAST_EXPECT(amm.expectBalances(
                    BTC(200'001), XRP(99'999.51), amm.tokens()));
                BEAST_EXPECT(expectOffers(
                    env, alice, 1, {{Amounts{BTC(1), XRP(0.01)}}}));
                // Carol's offer crosses AMM
                BEAST_EXPECT(expectOffers(env, carol, 0));
            }
        }
    }

    void
    testLPTokenBalance(FeatureBitset features)
    {
        testcase("LPToken Balance");
        using namespace jtx;

        Env env(*this, features);
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

        MPT BTC = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .pay = 40'000'000000,
             .flags = tfMPTCanClawback | tfMPTCanLock | tfMPTCanTransfer});

        AMM amm(env, alice, BTC(2), USD(1));
        amm.deposit(alice, IOUAmount{1'876123487565916, -15});
        amm.deposit(bob, IOUAmount{1'000});
        amm.withdraw(alice, IOUAmount{1'876123487565916, -15});
        amm.withdrawAll(bob);

        auto const lpToken =
            getAccountLines(
                env, alice, amm.lptIssue())[jss::lines][0u][jss::balance]
                .asString();
        auto const lpTokenBalance =
            amm.ammRpcInfo()[jss::amm][jss::lp_token][jss::value].asString();

        BEAST_EXPECT(
            lpToken == "1.414213562374011" &&
            lpTokenBalance == "1.414213562374");

        auto res =
            isOnlyLiquidityProvider(*env.current(), amm.lptIssue(), alice);
        BEAST_EXPECT(res && res.value());

        amm.withdrawAll(alice);
        BEAST_EXPECT(!amm.ammExists());
    }

    void
    testAMMDepositWithFrozenAssets()
    {
        testcase("test AMMDeposit with frozen assets");
        using namespace jtx;

        // This lambda function is used to create trustline, MPT.
        // and create an AMM account.
        // And also test the callback function.
        auto testAMMDeposit =
            [&](Env& env, std::function<void(AMM & amm, MPTTester & BTC)> cb) {
                env.fund(XRP(1'000), gw, alice);
                env.close();
                MPTTester BTC(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice},
                     .pay = 30'000,
                     .flags = tfMPTCanLock | MPTDEXFlags});

                AMM amm(env, alice, BTC(100), XRP(100));
                env.close();
                BTC.set({.holder = alice, .flags = tfMPTLock});
                cb(amm, BTC);
            };

        // Deposit two assets, one of which is frozen,
        // then we should get tecFROZEN error.
        {
            Env env(*this);
            testAMMDeposit(env, [&](AMM& amm, MPTTester& BTC) {
                amm.deposit(
                    alice,
                    BTC(100),
                    XRP(100),
                    std::nullopt,
                    tfTwoAsset,
                    ter(tecFROZEN));
            });
        }

        // Deposit one asset, which is the frozen token,
        // then we should get tecFROZEN error.
        {
            Env env(*this);
            testAMMDeposit(env, [&](AMM& amm, MPTTester& BTC) {
                amm.deposit(
                    alice,
                    BTC(100),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    ter(tecFROZEN));
            });
        }

        // Deposit one asset which is not the frozen token,
        // but the other asset is frozen. We should get tecFROZEN error
        // when feature AMMClawback is enabled.
        {
            Env env(*this);
            testAMMDeposit(env, [&](AMM& amm, MPTTester& BTC) {
                amm.deposit(
                    alice,
                    XRP(100),
                    std::nullopt,
                    std::nullopt,
                    tfSingleAsset,
                    ter(tecFROZEN));
            });
        }
    }

    void
    run() override
    {
        FeatureBitset const all{jtx::testable_amendments()};
        testInstanceCreate();
        testInvalidInstance();
        testInvalidDeposit(all);
        testInvalidDeposit(all - featureAMMClawback);
        testDeposit();
        testInvalidWithdraw();
        testWithdraw();
        testInvalidFeeVote();
        testFeeVote();
        testInvalidBid();
        testBid(all);
        testClawback();
        testClawbackFromAMMAccount(all);
        testClawbackFromAMMAccount(all - featureSingleAssetVault);
        testInvalidAMMPayment();
        testBasicPaymentEngine();
        testAMMTokens();
        testAmendment();
        testAMMAndCLOB(all);
        testTradingFee(all);
        testTradingFee(all - fixAMMv1_3);
        testAdjustedTokens(all);
        testAMMID();
        testSelection(all);
        testMalformed();
        testFixAMMOfferBlockedByLOB(all - fixAMMv1_1 - fixAMMv1_3);
        testFixAMMOfferBlockedByLOB(all);
        testLPTokenBalance(all);
        testLPTokenBalance(all - fixAMMv1_3);
        testAMMDepositWithFrozenAssets();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMMPT, app, ripple, 1);

}  // namespace test
}  // namespace ripple