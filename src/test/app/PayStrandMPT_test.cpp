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

#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/paths/RippleCalc.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/ledger/ApplyViewImpl.h>
#include <xrpld/ledger/PaymentSandbox.h>

#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

struct PayStrandMPT_test : public beast::unit_test::suite
{
    void
    testToStrand(FeatureBitset features)
    {
        testcase("To Strand");

        using namespace jtx;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USDC = gw["USD"];
        auto const EURC = gw["EUR"];

        using M = MPTEndpointStepInfo;
        using B = ripple::Book;
        using XRPS = XRPEndpointStepInfo;

        AMMContext ammContext(alice, false);

        auto test = [&, this](
                        jtx::Env& env,
                        Asset const& deliver,
                        std::optional<Asset> const& sendMaxIssue,
                        STPath const& path,
                        TER expTer,
                        auto&&... expSteps) {
            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                bob,
                deliver,
                std::nullopt,
                sendMaxIssue,
                path,
                true,
                OfferCrossing::no,
                ammContext,
                std::nullopt,
                env.app().logs().journal("Flow"));
            BEAST_EXPECT(ter == expTer);
            if (sizeof...(expSteps) != 0)
                BEAST_EXPECT(jtx::equal(
                    strand, std::forward<decltype(expSteps)>(expSteps)...));
        };

        {
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .maxAmt = 1'000});
            MPT const bobUSD = MPTTester(
                {.env = env,
                 .issuer = bob,
                 .holders = {alice},
                 .maxAmt = 1'000});
            MPT const EUR = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .maxAmt = 1'000});
            MPT const bobEUR = MPTTester(
                {.env = env,
                 .issuer = bob,
                 .holders = {alice},
                 .maxAmt = 1'000});
            env(pay(gw, alice, EUR(100)));

            {
                // Original test is
                // STPath({ipe(bob["USD"]), cpe(EUR.currency)});
                // which ripples through same currency, different issuer.
                // This results in 5 steps:
                // 1 DirectStep alice -> gw EUR/gw
                // 2 Book EUR/gw USD/bob
                // 3 Book USD/bob EUR/bob
                // 4 Book EUR/bob XRP
                // 5 XRPEndpoint
                // This is somewhat equivalent path with MPT
                STPath const path =
                    STPath({ipe(bobUSD), ipe(bobEUR), cpe(xrpCurrency())});
                auto [ter, _] = toStrand(
                    *env.current(),
                    alice,
                    alice,
                    /*deliver*/ xrpIssue(),
                    /*limitQuality*/ std::nullopt,
                    /*sendMaxIssue*/ EUR,
                    path,
                    true,
                    OfferCrossing::no,
                    ammContext,
                    std::nullopt,
                    env.app().logs().journal("Flow"));
                (void)_;
                BEAST_EXPECT(ter == tesSUCCESS);
            }
            {
                STPath const path = STPath({ipe(USD), cpe(xrpCurrency())});
                auto [ter, _] = toStrand(
                    *env.current(),
                    alice,
                    alice,
                    /*deliver*/ xrpIssue(),
                    /*limitQuality*/ std::nullopt,
                    /*sendMaxIssue*/ EUR,
                    path,
                    true,
                    OfferCrossing::no,
                    ammContext,
                    std::nullopt,
                    env.app().logs().journal("Flow"));
                (void)_;
                BEAST_EXPECT(ter == tesSUCCESS);
            }
        }
        {
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, carol, gw);
            auto USDM = MPTTester({.env = env, .issuer = gw, .maxAmt = 1'000});
            MPT const USD = USDM;
            auto EURM = MPTTester({.env = env, .issuer = gw, .maxAmt = 1'000});
            MPT const EUR = EURM;

            test(env, USD, std::nullopt, STPath(), tecNO_AUTH);

            USDM.authorize(Holders{alice, bob, carol});

            test(env, USD, std::nullopt, STPath(), tecPATH_DRY);

            env(pay(gw, alice, USD(100)));
            env(pay(gw, carol, USD(100)));

            // Insert implied account
            test(
                env,
                USD,
                std::nullopt,
                STPath(),
                tesSUCCESS,
                M{alice, gw, USD},
                M{gw, bob, USD});
            EURM.authorize(Holders{alice, bob});

            // Insert implied offer
            test(
                env,
                EUR,
                USD,
                STPath(),
                tesSUCCESS,
                M{alice, gw, USD},
                B{USD, EUR, std::nullopt},
                M{gw, bob, EUR});

            // Path with explicit offer
            test(
                env,
                EUR,
                USD,
                STPath({ipe(EUR)}),
                tesSUCCESS,
                M{alice, gw, USD},
                B{USD, EUR, std::nullopt},
                M{gw, bob, EUR});

            // Path with XRP src currency
            test(
                env,
                USD,
                xrpIssue(),
                STPath({ipe(USD)}),
                tesSUCCESS,
                XRPS{alice},
                B{XRP, USD, std::nullopt},
                M{gw, bob, USD});

            // Path with XRP dst currency.
            test(
                env,
                xrpIssue(),
                USD,
                STPath({STPathElement{
                    STPathElement::typeCurrency,
                    xrpAccount(),
                    xrpCurrency(),
                    xrpAccount()}}),
                tesSUCCESS,
                M{alice, gw, USD},
                B{USD, XRP, std::nullopt},
                XRPS{bob});

            // Path with XRP cross currency bridged payment
            test(
                env,
                EUR,
                USD,
                STPath({cpe(xrpCurrency())}),
                tesSUCCESS,
                M{alice, gw, USD},
                B{USD, XRP, std::nullopt},
                B{XRP, EUR, std::nullopt},
                M{gw, bob, EUR});

            // Create an offer with the same in/out issue
            test(env, EUR, USD, STPath({ipe(USD), ipe(EUR)}), temBAD_PATH);

            // The same offer can't appear more than once on a path
            test(
                env,
                EUR,
                USD,
                STPath({ipe(EUR), ipe(USD), ipe(EUR)}),
                temBAD_PATH_LOOP);
        }

        {
            // cannot have more than one offer with the same output issue

            using namespace jtx;
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});
            MPT const EUR = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});

            env(pay(gw, bob, USD(100)));
            env(pay(gw, bob, EUR(100)));

            env(offer(bob, XRP(100), USD(100)));
            env(offer(bob, USD(100), EUR(100)), txflags(tfPassive));
            env(offer(bob, EUR(100), USD(100)), txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/EUR -> EUR/USD
            env(pay(alice, carol, USD(100)),
                path(~USD, ~EUR, ~USD),
                sendmax(XRP(200)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }

        {
            // check global freeze
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            auto USDM = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .flags = MPTDEXFlags | tfMPTCanLock,
                 .maxAmt = 1'000});
            MPT const USD = USDM;
            env(pay(gw, alice, USD(100)));

            // Account can't issue payments
            USDM.set({.holder = alice, .flags = tfMPTLock});
            test(env, USD, std::nullopt, STPath(), tecLOCKED);
            USDM.set({.holder = alice, .flags = tfMPTUnlock});
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);

            // Account can not issue funds
            USDM.set({.flags = tfMPTLock});
            test(env, USD, std::nullopt, STPath(), tecLOCKED);
            USDM.set({.flags = tfMPTUnlock});
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);

            // Account can not receive funds
            USDM.set({.holder = bob, .flags = tfMPTLock});
            test(env, USD, std::nullopt, STPath(), tecLOCKED);
            USDM.set({.holder = bob, .flags = tfMPTUnlock});
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);
        }

        {
            // check no auth
            // An account may require authorization to receive MPTs from an
            // issuer
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            auto USDM = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .flags = MPTDEXFlags | tfMPTRequireAuth,
                 .maxAmt = 1'000});
            MPT const USD = USDM;

            // Authorize alice but not bob
            USDM.authorize({.account = alice});
            USDM.authorize({.holder = alice});
            env(pay(gw, alice, USD(100)));
            env.require(balance(alice, USD(100)));
            test(env, USD, std::nullopt, STPath(), tecNO_AUTH);

            // Check pure issue redeem still works
            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                gw,
                USD,
                std::nullopt,
                std::nullopt,
                STPath(),
                true,
                OfferCrossing::no,
                ammContext,
                std::nullopt,
                env.app().logs().journal("Flow"));
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(equal(strand, M{alice, gw, USD}));
        }

        {
            // last step xrp from offer
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .maxAmt = 1'000});
            env(pay(gw, alice, USD(100)));

            // alice -> USD/XRP -> bob
            STPath path;
            path.emplace_back(std::nullopt, xrpCurrency(), std::nullopt);

            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                bob,
                XRP,
                std::nullopt,
                USD,
                path,
                false,
                OfferCrossing::no,
                ammContext,
                std::nullopt,
                env.app().logs().journal("Flow"));
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(equal(
                strand,
                M{alice, gw, USD},
                B{USD, xrpIssue(), std::nullopt},
                XRPS{bob}));
        }
    }

    void
    testRIPD1373(FeatureBitset features)
    {
        using namespace jtx;
        testcase("RIPD1373");

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});

            env(pay(gw, bob, USD(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), XRP(100)), txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                path(~USD, ~XRP),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});

            env(pay(gw, bob, USD(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), XRP(100)), txflags(tfPassive));

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

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const EUR = gw["EUR"];
        auto const CNY = gw["CNY"];

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);
            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});

            env(pay(gw, bob, USD(100)));
            env(pay(gw, alice, USD(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), XRP(100)), txflags(tfPassive));

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
            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});
            MPT const EUR = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});
            MPT const CNY = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .maxAmt = 10'000});

            env(pay(gw, bob, USD(100)));
            env(pay(gw, bob, EUR(100)));
            env(pay(gw, bob, CNY(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), EUR(100)), txflags(tfPassive));
            env(offer(bob, EUR(100), CNY(100)), txflags(tfPassive));

            // payment path: XRP->XRP/USD->USD/EUR->USD/CNY
            env(pay(alice, carol, CNY(100)),
                sendmax(XRP(100)),
                path(~USD, ~EUR, ~USD, ~CNY),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
    }

    void
    testNoAccount(FeatureBitset features)
    {
        testcase("test no account");
        using namespace jtx;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        Env env(*this, features);
        env.fund(XRP(10'000), alice, bob, gw);
        MPT const USD =
            MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        STAmount sendMax{USD, 100, 1};
        STAmount noAccountAmount{MPTIssue{0, noAccount()}, 100, 1};
        STAmount deliver;
        AccountID const srcAcc = alice.id();
        AccountID dstAcc = bob.id();
        STPathSet pathSet;
        ::ripple::path::RippleCalc::Input inputs;
        inputs.defaultPathsAllowed = true;
        try
        {
            PaymentSandbox sb{env.current().get(), tapNONE};
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    dstAcc,
                    noAccount(),
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    noAccount(),
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    noAccountAmount,
                    deliver,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    noAccountAmount,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
        }
        catch (...)
        {
            this->fail();
        }
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = supported_amendments();
        testToStrand(sa);

        testRIPD1373(sa);

        testLoop(sa);

        testNoAccount(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PayStrandMPT, app, ripple);

}  // namespace test
}  // namespace ripple
