
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/paths/RippleCalc.h>
#include <xrpl/tx/paths/detail/Steps.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <optional>
#include <type_traits>

namespace xrpl::test {

struct PayStrandMPT_test : public beast::unit_test::Suite
{
    static jtx::DirectStepInfo
    makeEndpointStep(jtx::Account const& src, jtx::Account const& dst, jtx::IOU const& iou)
    {
        return jtx::DirectStepInfo{.src = src, .dst = dst, .currency = iou.currency};
    }
    static jtx::MPTEndpointStepInfo
    makeEndpointStep(jtx::Account const& src, jtx::Account const& dst, jtx::MPT const& mpt)
    {
        return jtx::MPTEndpointStepInfo{.src = src, .dst = dst, .mptid = mpt.mpt()};
    }

    void
    testToStrand(FeatureBitset features)
    {
        testcase("To Strand");

        using namespace jtx;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");

        using M = MPTEndpointStepInfo;
        using B = xrpl::Book;
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
                OfferCrossing::No,
                ammContext,
                std::nullopt,
                env.app().getLogs().journal("Flow"));
            BEAST_EXPECT(ter == expTer);
            if (sizeof...(expSteps) != 0)
                BEAST_EXPECT(jtx::equal(strand, std::forward<decltype(expSteps)>(expSteps)...));
        };

        {
            auto testMultiToken = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);
                env.fund(XRP(10'000), alice, bob, gw);
                MPT const usd =
                    MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 1'000});
                auto const bobUSD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = bob,
                     .holders = {alice},
                     .limit = 1'000});
                MPT const eur =
                    MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 1'000});
                auto const bobEUR = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = bob,
                     .holders = {alice},
                     .limit = 1'000});
                env(pay(gw, alice, eur(100)));

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
                    STPath const path = STPath({ipe(bobUSD), ipe(bobEUR), cpe(xrpCurrency())});
                    auto [ter, _] = toStrand(
                        *env.current(),
                        alice,
                        alice,
                        /*deliver*/ xrpIssue(),
                        /*limitQuality*/ std::nullopt,
                        /*sendMaxIssue*/ eur,
                        path,
                        true,
                        OfferCrossing::No,
                        ammContext,
                        std::nullopt,
                        env.app().getLogs().journal("Flow"));
                    (void)_;
                    BEAST_EXPECT(ter == tesSUCCESS);
                }
                {
                    STPath const path = STPath({ipe(usd), cpe(xrpCurrency())});
                    auto [ter, _] = toStrand(
                        *env.current(),
                        alice,
                        alice,
                        /*deliver*/ xrpIssue(),
                        /*limitQuality*/ std::nullopt,
                        /*sendMaxIssue*/ eur,
                        path,
                        true,
                        OfferCrossing::No,
                        ammContext,
                        std::nullopt,
                        env.app().getLogs().journal("Flow"));
                    (void)_;
                    BEAST_EXPECT(ter == tesSUCCESS);
                }
            };
            testHelper2TokensMix(testMultiToken);
        }
        {
            auto testMultiToken = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);
                env.fund(XRP(10'000), alice, bob, carol, gw);
                auto usd = issue1({.env = env, .token = "USD", .issuer = gw, .limit = 1'000});
                using tUSD = std::decay_t<decltype(usd)>;
                auto eur = issue2({.env = env, .token = "EUR", .issuer = gw, .limit = 1'000});
                using tEUR = std::decay_t<decltype(eur)>;

                auto const err = [&]() {
                    if constexpr (std::is_same_v<tUSD, MPT>)
                    {
                        return tecNO_AUTH;
                    }
                    else
                    {
                        return terNO_LINE;
                    }
                }();
                test(env, usd, std::nullopt, STPath(), err);

                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    MPTTester(env, gw, usd).authorizeHolders({alice, bob, carol});
                }
                else
                {
                    env.trust(usd(1'000), alice, bob, carol);
                }

                test(env, usd, std::nullopt, STPath(), tecPATH_DRY);

                env(pay(gw, alice, usd(100)));
                env(pay(gw, carol, usd(100)));

                // Insert implied account
                test(
                    env,
                    usd,
                    std::nullopt,
                    STPath(),
                    tesSUCCESS,
                    makeEndpointStep(alice, gw, usd),
                    makeEndpointStep(gw, bob, usd));
                if constexpr (std::is_same_v<tEUR, MPT>)
                {
                    MPTTester(env, gw, eur).authorizeHolders({alice, bob});
                }
                else
                {
                    env.trust(eur(1'000), alice, bob);
                }

                // Insert implied offer
                test(
                    env,
                    eur,
                    usd,
                    STPath(),
                    tesSUCCESS,
                    makeEndpointStep(alice, gw, usd),
                    B{usd, eur, std::nullopt},
                    makeEndpointStep(gw, bob, eur));

                // Path with explicit offer
                test(
                    env,
                    eur,
                    usd,
                    STPath({ipe(eur)}),
                    tesSUCCESS,
                    makeEndpointStep(alice, gw, usd),
                    B{usd, eur, std::nullopt},
                    makeEndpointStep(gw, bob, eur));

                // Path with XRP src currency
                test(
                    env,
                    usd,
                    xrpIssue(),
                    STPath({ipe(usd)}),
                    tesSUCCESS,
                    XRPS{alice},
                    B{XRP, usd, std::nullopt},
                    makeEndpointStep(gw, bob, usd));

                // Path with XRP dst currency.
                test(
                    env,
                    xrpIssue(),
                    usd,
                    STPath({STPathElement{
                        STPathElement::TypeCurrency, xrpAccount(), xrpCurrency(), xrpAccount()}}),
                    tesSUCCESS,
                    makeEndpointStep(alice, gw, usd),
                    B{usd, XRP, std::nullopt},
                    XRPS{bob});

                // Path with XRP cross currency bridged payment
                test(
                    env,
                    eur,
                    usd,
                    STPath({cpe(xrpCurrency())}),
                    tesSUCCESS,
                    makeEndpointStep(alice, gw, usd),
                    B{usd, XRP, std::nullopt},
                    B{XRP, eur, std::nullopt},
                    makeEndpointStep(gw, bob, eur));

                // Create an offer with the same in/out issue
                test(env, eur, usd, STPath({ipe(usd), ipe(eur)}), temBAD_PATH);

                // The same offer can't appear more than once on a path
                test(env, eur, usd, STPath({ipe(eur), ipe(usd), ipe(eur)}), temBAD_PATH_LOOP);
            };
            testHelper2TokensMix(testMultiToken);
        }

        {
            // cannot have more than one offer with the same output issue

            using namespace jtx;

            auto testMultiToken = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 10'000});
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 10'000});

                env(pay(gw, bob, usd(100)));
                env(pay(gw, bob, eur(100)));

                env(offer(bob, XRP(100), usd(100)));
                env(offer(bob, usd(100), eur(100)), Txflags(tfPassive));
                env(offer(bob, eur(100), usd(100)), Txflags(tfPassive));

                // payment path: XRP -> XRP/USD -> USD/EUR -> EUR/USD
                env(pay(alice, carol, usd(100)),
                    Path(~usd, ~eur, ~usd),
                    Sendmax(XRP(200)),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
            };
            testHelper2TokensMix(testMultiToken);
        }

        {
            // check global freeze
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            auto usdm = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .flags = kMptDexFlags | tfMPTCanLock,
                 .maxAmt = 1'000});
            MPT const usd = usdm;
            env(pay(gw, alice, usd(100)));

            // Account can't issue payments
            usdm.set({.holder = alice, .flags = tfMPTLock});
            test(env, usd, std::nullopt, STPath(), terLOCKED);
            usdm.set({.holder = alice, .flags = tfMPTUnlock});
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);

            // Account can not issue funds
            usdm.set({.flags = tfMPTLock});
            test(env, usd, std::nullopt, STPath(), terLOCKED);
            usdm.set({.flags = tfMPTUnlock});
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);

            // Account can not receive funds
            usdm.set({.holder = bob, .flags = tfMPTLock});
            test(env, usd, std::nullopt, STPath(), terLOCKED);
            usdm.set({.holder = bob, .flags = tfMPTUnlock});
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);
        }

        {
            // check no auth
            // An account may require authorization to receive MPTs from an
            // issuer
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            auto usdm = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .flags = kMptDexFlags | tfMPTRequireAuth,
                 .maxAmt = 1'000});
            MPT const usd = usdm;

            // Authorize alice but not bob
            usdm.authorize({.account = alice});
            usdm.authorize({.holder = alice});
            env(pay(gw, alice, usd(100)));
            env.require(Balance(alice, usd(100)));
            test(env, usd, std::nullopt, STPath(), tecNO_AUTH);

            // Check pure issue redeem still works
            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                gw,
                usd,
                std::nullopt,
                std::nullopt,
                STPath(),
                true,
                OfferCrossing::No,
                ammContext,
                std::nullopt,
                env.app().getLogs().journal("Flow"));
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(equal(strand, M{alice, gw, usd}));
        }

        {
            // last step xrp from offer
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 1'000});
            env(pay(gw, alice, usd(100)));

            // alice -> USD/XRP -> bob
            STPath path;
            path.emplaceBack(std::nullopt, xrpCurrency(), std::nullopt);

            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                bob,
                XRP,
                std::nullopt,
                usd,
                path,
                false,
                OfferCrossing::No,
                ammContext,
                std::nullopt,
                env.app().getLogs().journal("Flow"));
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(
                equal(strand, M{alice, gw, usd}, B{usd, xrpIssue(), std::nullopt}, XRPS{bob}));
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
            MPT const usd = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}, .maxAmt = 10'000});

            env(pay(gw, bob, usd(100)));

            env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
            env(offer(bob, usd(100), XRP(100)), Txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                Path(~usd, ~XRP),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            MPT const usd = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}, .maxAmt = 10'000});

            env(pay(gw, bob, usd(100)));

            env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
            env(offer(bob, usd(100), XRP(100)), Txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                Path(~usd, ~XRP),
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

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const eur = gw["EUR"];
        auto const cny = gw["CNY"];

        {
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);
            MPT const usd = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}, .maxAmt = 10'000});

            env(pay(gw, bob, usd(100)));
            env(pay(gw, alice, usd(100)));

            env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
            env(offer(bob, usd(100), XRP(100)), Txflags(tfPassive));

            // payment path: USD -> USD/XRP -> XRP/USD
            env(pay(alice, carol, usd(100)),
                Sendmax(usd(100)),
                Path(~XRP, ~usd),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
        }
        {
            auto testMultiToken = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);
                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 10'000});
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 10'000});
                auto const cny = issue3(
                    {.env = env,
                     .token = "CNY",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 10'000});

                env(pay(gw, bob, usd(100)));
                env(pay(gw, bob, eur(100)));
                env(pay(gw, bob, cny(100)));

                env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
                env(offer(bob, usd(100), eur(100)), Txflags(tfPassive));
                env(offer(bob, eur(100), cny(100)), Txflags(tfPassive));

                // payment path: XRP->XRP/USD->USD/EUR->USD/CNY
                env(pay(alice, carol, cny(100)),
                    Sendmax(XRP(100)),
                    Path(~usd, ~eur, ~usd, ~cny),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
            };
            testHelper3TokensMix(testMultiToken);
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
        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        STAmount const sendMax{usd, 100, 1};
        STAmount const noAccountAmount{MPTIssue{0, noAccount()}, 100, 1};
        STAmount const deliver;
        AccountID const srcAcc = alice.id();
        AccountID const dstAcc = bob.id();
        STPathSet const pathSet;
        xrpl::path::RippleCalc::Input inputs;
        inputs.defaultPathsAllowed = true;
        try
        {
            PaymentSandbox sb{env.current().get(), TapNone};
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    dstAcc,
                    noAccount(),
                    pathSet,
                    std::nullopt,
                    env.app(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    noAccount(),
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    noAccountAmount,
                    deliver,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    noAccountAmount,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app(),
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
        auto const sa = testableAmendments();
        testToStrand(sa);

        testRIPD1373(sa);

        testLoop(sa);

        testNoAccount(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PayStrandMPT, app, xrpl);

}  // namespace xrpl::test
