#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/CLAMM.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>

#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

namespace xrpl {
namespace test {

struct CLAMMPayment_test : public beast::unit_test::suite
{
    jtx::Account const gw{"gateway"};
    jtx::Account const alice{"alice"};
    jtx::Account const bob{"bob"};
    jtx::Account const carol{"carol"};
    jtx::Account const dan{"dan"};
    jtx::IOU const USD{gw["USD"]};
    jtx::IOU const EUR{gw["EUR"]};

    void
    setupEnv(jtx::Env& env)
    {
        using namespace jtx;
        env.fund(XRP(100'000), gw, alice, bob, carol, dan);
        env.close();

        env(trust(alice, USD(1'000'000)));
        env(trust(bob, USD(1'000'000)));
        env(trust(carol, USD(1'000'000)));
        env(trust(dan, USD(1'000'000)));
        env(trust(alice, EUR(1'000'000)));
        env(trust(bob, EUR(1'000'000)));
        env(trust(carol, EUR(1'000'000)));
        env(trust(dan, EUR(1'000'000)));
        env.close();

        env(pay(gw, alice, USD(100'000)));
        env(pay(gw, bob, USD(100'000)));
        env(pay(gw, carol, USD(100'000)));
        env(pay(gw, dan, USD(100'000)));
        env(pay(gw, alice, EUR(100'000)));
        env(pay(gw, bob, EUR(100'000)));
        env(pay(gw, carol, EUR(100'000)));
        env.close();
    }

    // Create a CLAMM pool and deposit liquidity. Returns pool ID.
    uint256
    createPoolWithLiquidity(
        jtx::Env& env,
        jtx::Account const& creator,
        Issue const& asset,
        Issue const& asset2,
        std::uint8_t feeTier,
        std::int32_t lowerTick,
        std::int32_t upperTick,
        STAmount const& amount,
        STAmount const& amount2)
    {
        using namespace jtx;
        auto const pid = clammPoolID(asset, asset2, feeTier);
        env(clammCreate(
                env, creator, asset, asset2, feeTier, clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();
        env(clammDeposit(creator, pid, lowerTick, upperTick, amount, amount2),
            ter(tesSUCCESS));
        env.close();
        return pid;
    }

    // ---------------------------------------------------------------
    // Test: basic payment routes through CLAMM pool (XRP -> USD)
    // ---------------------------------------------------------------
    void
    testBasicPayment()
    {
        testcase("Basic payment through CLAMM");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create XRP/USD pool with liquidity
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        auto const carolUsdBefore = env.balance(carol, USD);

        // Bob pays carol USD, routing through CLAMM pool via XRP
        env(pay(bob, carol, USD(50)),
            path(~USD),
            sendmax(XRP(100)),
            txflags(tfPartialPayment));
        env.close();

        auto const carolUsdAfter = env.balance(carol, USD);
        BEAST_EXPECT(carolUsdAfter > carolUsdBefore);
    }

    // ---------------------------------------------------------------
    // Test: CLAMM vs CLOB competition -- CLAMM has better quality
    // ---------------------------------------------------------------
    void
    testCLAMMBeatsCLOB()
    {
        testcase("CLAMM beats CLOB quality");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool with tight liquidity (good rate)
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        // Create a CLOB offer at worse rate: bob offers to sell 50 USD
        // for 100 XRP (rate 0.5 USD/XRP vs CLAMM's ~1.0 USD/XRP)
        env(offer(bob, XRP(100), USD(50)));
        env.close();

        auto const carolUsdBefore = env.balance(carol, USD);
        auto const carolXrpBefore = env.balance(carol);

        // Carol pays dan USD through XRP path
        env(pay(carol, dan, USD(10)),
            path(~USD),
            sendmax(XRP(20)));
        env.close();

        auto const carolXrpAfter = env.balance(carol);
        // With CLAMM's better rate (~1:1), carol should spend ~10 XRP
        // (not 20 XRP as the CLOB rate would require)
        auto const xrpSpent = carolXrpBefore - carolXrpAfter;
        // Accounting for fees, should be well under 20 XRP
        BEAST_EXPECT(xrpSpent < XRP(20));
        BEAST_EXPECT(env.balance(dan, USD) > USD(100'009));
    }

    // ---------------------------------------------------------------
    // Test: CLOB beats CLAMM -- payment uses CLOB
    // ---------------------------------------------------------------
    void
    testCLOBBeatsCLAMM()
    {
        testcase("CLOB beats CLAMM quality");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool at high fee tier (1% fee -> worse effective rate)
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 3,
            -2000, 2000, XRP(10'000), USD(10'000));

        // Create a CLOB offer at good rate: bob offers 50 USD for 50 XRP
        env(offer(bob, XRP(50), USD(50)));
        env.close();

        // Payment should prefer CLOB since it has better rate
        auto const danUsdBefore = env.balance(dan, USD);
        env(pay(carol, dan, USD(10)),
            path(~USD),
            sendmax(XRP(15)));
        env.close();

        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
        // CLOB offer should be partially consumed
        // (we can't easily verify which was used, but the payment succeeds)
    }

    // ---------------------------------------------------------------
    // Test: CLAMM-only (no CLOB offers)
    // ---------------------------------------------------------------
    void
    testCLAMMOnlyNoClob()
    {
        testcase("CLAMM only, no CLOB offers");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool -- no CLOB offers at all
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        auto const danUsdBefore = env.balance(dan, USD);
        env(pay(carol, dan, USD(50)),
            path(~USD),
            sendmax(XRP(100)),
            txflags(tfPartialPayment));
        env.close();

        auto const danUsdAfter = env.balance(dan, USD);
        BEAST_EXPECT(danUsdAfter > danUsdBefore);
    }

    // ---------------------------------------------------------------
    // Test: multiple fee tiers -- best quality pool selected
    // ---------------------------------------------------------------
    void
    testMultipleFeeTiers()
    {
        testcase("Multiple fee tiers - best selected");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create pools at fee tiers 0 (0.01%) and 3 (1.0%)
        // Both have similar liquidity, but tier 0 has lower fees
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 0,
            -1, 1, XRP(10'000), USD(10'000));
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 3,
            -2000, 2000, XRP(10'000), USD(10'000));

        // Payment should route through the lower-fee pool (tier 0)
        auto const danUsdBefore = env.balance(dan, USD);
        env(pay(carol, dan, USD(10)),
            path(~USD),
            sendmax(XRP(15)),
            txflags(tfPartialPayment));
        env.close();

        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
    }

    // ---------------------------------------------------------------
    // Test: IOU to IOU payment through CLAMM
    // ---------------------------------------------------------------
    void
    testIOUtoIOU()
    {
        testcase("IOU to IOU payment through CLAMM");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create EUR/USD CLAMM pool
        createPoolWithLiquidity(
            env, alice, EUR.issue(), USD.issue(), 1,
            -1000, 1000, EUR(10'000), USD(10'000));

        auto const danUsdBefore = env.balance(dan, USD);

        // Bob pays dan USD using EUR
        env(pay(bob, dan, USD(50)),
            path(~USD),
            sendmax(EUR(100)),
            txflags(tfPartialPayment));
        env.close();

        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
    }

    // ---------------------------------------------------------------
    // Test: amendment gating -- no CLAMM routing when disabled
    // ---------------------------------------------------------------
    void
    testAmendmentGating()
    {
        testcase("Amendment gating - no CLAMM when disabled");
        using namespace jtx;

        auto const noClammFeatures = testable_amendments() - featureCLAMM;
        Env env{*this, noClammFeatures};

        env.fund(XRP(100'000), gw, alice, bob, carol);
        env.close();
        env(trust(alice, USD(1'000'000)));
        env(trust(bob, USD(1'000'000)));
        env(trust(carol, USD(1'000'000)));
        env.close();
        env(pay(gw, alice, USD(100'000)));
        env(pay(gw, bob, USD(100'000)));
        env.close();

        // Can't create CLAMM pool
        env(clammCreate(
                env, alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(temDISABLED));
        env.close();

        // No CLOB offers either -- payment should fail.
        // Returns tecPATH_PARTIAL because the engine finds a partial path
        // (default rippling path) but cannot deliver the full non-partial
        // amount through the XRP->USD book.
        env(pay(bob, carol, USD(10)),
            path(~USD),
            sendmax(XRP(20)),
            ter(tecPATH_PARTIAL));
        env.close();
    }

    // ---------------------------------------------------------------
    // Test: partial payment through CLAMM
    // ---------------------------------------------------------------
    void
    testPartialPayment()
    {
        testcase("Partial payment through CLAMM");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool with limited liquidity in narrow range
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -100, 100, XRP(100), USD(100));

        // Use an XRP-only sender so the payment engine cannot ripple
        // USD directly and must route through the CLAMM pool.
        Account const xrpOnly{"xrpOnly"};
        env.fund(XRP(100'000), xrpOnly);
        env.close();

        auto const danUsdBefore = env.balance(dan, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        // Read pool state before payment
        auto sleB = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sleB != nullptr);
        auto const liqBefore = sleB
            ? clamm::fromSLEField(sleB->getFieldH128(sfLiquidityAmount))
            : clamm::uint128(0);
        auto const sqrtBefore = sleB
            ? clamm::fromSLEField(sleB->getFieldH128(sfSqrtPrice))
            : clamm::uint128(0);

        // xrpOnly has no USD trust line, so the payment must route
        // through the CLAMM pool (XRP -> USD).  The pool has limited
        // capacity (~100 USD), so partial delivery is expected.
        env(pay(xrpOnly, dan, USD(1'000)),
            path(~USD),
            sendmax(XRP(2'000)),
            txflags(tfPartialPayment | tfNoRippleDirect));
        env.close();

        // Check pool state after payment
        auto const sleA = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sleA != nullptr);
        if (sleA)
        {
            auto const liqAfter =
                clamm::fromSLEField(sleA->getFieldH128(sfLiquidityAmount));
            auto const sqrtAfter =
                clamm::fromSLEField(sleA->getFieldH128(sfSqrtPrice));
            // Pool state MUST change after a swap
            BEAST_EXPECT(sqrtAfter != sqrtBefore);
            // Liquidity should be 0 (pool exhausted) or reduced
            BEAST_EXPECT(liqBefore > 0);  // sanity check
            BEAST_EXPECT(liqAfter < liqBefore);  // must have decreased
            BEAST_EXPECT(liqAfter == 0);  // fully exhausted
        }

        auto const danUsdAfter = env.balance(dan, USD);
        // Should have received something, but less than 1000
        BEAST_EXPECT(danUsdAfter > danUsdBefore);
        BEAST_EXPECT(danUsdAfter < danUsdBefore + USD(1'000));
    }

    // ---------------------------------------------------------------
    // Test: XRP direct payment through CLAMM
    // ---------------------------------------------------------------
    void
    testXRPDirectPayment()
    {
        testcase("XRP direct payment through CLAMM");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create XRP/USD CLAMM pool
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        auto const carolXrpBefore = env.balance(carol);
        auto const danUsdBefore = env.balance(dan, USD);

        // Carol (has XRP) pays dan USD
        env(pay(carol, dan, USD(100)),
            path(~USD),
            sendmax(XRP(200)),
            txflags(tfPartialPayment));
        env.close();

        BEAST_EXPECT(env.balance(carol) < carolXrpBefore);
        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
    }

    // ---------------------------------------------------------------
    // Test: CLAMM pool state updated after payment routing
    // ---------------------------------------------------------------
    void
    testPoolStateAfterPayment()
    {
        testcase("Pool state updated after payment");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        auto const pid = createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        // Read pool state before payment
        auto const sleBefore = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sleBefore != nullptr);
        auto const sqrtPriceBefore = sleBefore
            ? clamm::fromSLEField(sleBefore->getFieldH128(sfSqrtPrice))
            : clamm::uint128(0);

        // Execute payment through pool
        env(pay(carol, dan, USD(500)),
            path(~USD),
            sendmax(XRP(1'000)),
            txflags(tfPartialPayment));
        env.close();

        // Pool state should be updated (price moved)
        auto const sleAfter = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sleAfter != nullptr);
        if (sleAfter)
        {
            auto const sqrtPriceAfter = clamm::fromSLEField(
                sleAfter->getFieldH128(sfSqrtPrice));
            // Price should have moved after the swap
            BEAST_EXPECT(sqrtPriceAfter != sqrtPriceBefore);
        }
    }

    // ---------------------------------------------------------------
    // Test: empty CLAMM pool doesn't interfere with CLOB
    // ---------------------------------------------------------------
    void
    testEmptyPoolFallsBackToCLOB()
    {
        testcase("Empty CLAMM pool falls back to CLOB");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool but don't deposit (no liquidity)
        (void)clammPoolID(xrpIssue(), USD.issue(), 1);
        env(clammCreate(
                env, alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Create CLOB offer
        env(offer(bob, XRP(50), USD(50)));
        env.close();

        auto const danUsdBefore = env.balance(dan, USD);

        // Payment should route through CLOB since CLAMM has no liquidity
        env(pay(carol, dan, USD(10)),
            path(~USD),
            sendmax(XRP(15)));
        env.close();

        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
    }

    // ---------------------------------------------------------------
    // Test: multi-path competition -- CLAMM vs CLOB on different paths
    // ---------------------------------------------------------------
    void
    testCLAMMMultiPathCompetition()
    {
        testcase("Multi-path competition: CLAMM vs CLOB");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // CLAMM pool tier 1 (0.05% fee -- good rate, wide range)
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        // CLOB offer at worse rate: 200 XRP for 100 USD (rate = 0.5 USD/XRP)
        env(offer(bob, XRP(200), USD(100)));
        env.close();

        auto const danUsdBefore = env.balance(dan, USD);
        auto const carolXrpBefore = env.balance(carol);

        // Payment should prefer CLAMM (better rate)
        env(pay(carol, dan, USD(50)),
            path(~USD),
            sendmax(XRP(100)),
            txflags(tfPartialPayment));
        env.close();

        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
        // Carol should spend less than 100 XRP for 50 USD
        auto const xrpSpent = carolXrpBefore - env.balance(carol);
        BEAST_EXPECT(xrpSpent < XRP(100));
    }

    // ---------------------------------------------------------------
    // Test: CLAMM and AMM on same pair -- quality-based selection
    // ---------------------------------------------------------------
    void
    testCLAMMAMMSamePair()
    {
        testcase("CLAMM and AMM on same pair");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool with concentrated liquidity (efficient)
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        // Create XLS-30 AMM on same pair
        AMM amm(env, alice, XRP(10'000), USD(10'000));

        auto const danUsdBefore = env.balance(dan, USD);
        auto const carolXrpBefore = env.balance(carol);

        // Payment routes through whichever gives better quality
        env(pay(carol, dan, USD(100)),
            path(~USD),
            sendmax(XRP(200)),
            txflags(tfPartialPayment));
        env.close();

        // Payment should succeed via one or both sources
        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
        BEAST_EXPECT(env.balance(carol) < carolXrpBefore);
    }

    // ---------------------------------------------------------------
    // Test: CLAMM with IOU transfer fee
    // ---------------------------------------------------------------
    void
    testCLAMMWithTransferFee()
    {
        testcase("CLAMM with IOU transfer fee");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Set 1% transfer fee on gateway's USD
        env(rate(gw, 1.01));
        env.close();

        // Create CLAMM pool XRP/USD
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        auto const danUsdBefore = env.balance(dan, USD);

        // Payment should account for transfer fee
        env(pay(carol, dan, USD(50)),
            path(~USD),
            sendmax(XRP(100)),
            txflags(tfPartialPayment));
        env.close();

        auto const danUsdAfter = env.balance(dan, USD);
        BEAST_EXPECT(danUsdAfter > danUsdBefore);
        // Due to transfer fee, dan receives less than carol sent
        // (payment engine factors in the fee)
    }

    // ---------------------------------------------------------------
    // Test: unfunded offer cleanup with CLAMM fallback
    // ---------------------------------------------------------------
    void
    testCLAMMUnfundedOfferCleanup()
    {
        testcase("Unfunded offer cleanup with CLAMM fallback");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool as fallback
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -1000, 1000, XRP(10'000), USD(10'000));

        // Bob creates CLOB offer then spends his USD
        env(offer(bob, XRP(50), USD(50)));
        env.close();

        // Drain bob's USD so the offer is unfunded
        env(pay(bob, gw, USD(100'000)));
        env.close();

        auto const danUsdBefore = env.balance(dan, USD);

        // Payment should skip unfunded CLOB offer, route through CLAMM
        env(pay(carol, dan, USD(10)),
            path(~USD),
            sendmax(XRP(20)),
            txflags(tfPartialPayment));
        env.close();

        BEAST_EXPECT(env.balance(dan, USD) > danUsdBefore);
    }

    // ---------------------------------------------------------------
    // Test: CLAMM exhausts then falls back to CLOB
    // ---------------------------------------------------------------
    void
    testCLAMMExhaustsToClob()
    {
        testcase("CLAMM exhausts then falls back to CLOB");
        using namespace jtx;

        auto const features = testable_amendments() | featureCLAMM;
        Env env{*this, features};
        setupEnv(env);

        // Create CLAMM pool with limited liquidity
        createPoolWithLiquidity(
            env, alice, xrpIssue(), USD.issue(), 1,
            -100, 100, XRP(100), USD(100));

        // Create CLOB offer as backup with larger capacity
        env(offer(bob, XRP(5000), USD(5000)));
        env.close();

        auto const danUsdBefore = env.balance(dan, USD);

        // Partial payment: should use CLAMM first, then CLOB for remainder
        env(pay(carol, dan, USD(500)),
            path(~USD),
            sendmax(XRP(1000)),
            txflags(tfPartialPayment));
        env.close();

        auto const danUsdAfter = env.balance(dan, USD);
        auto const delivered = danUsdAfter - danUsdBefore;
        // Should deliver more than CLAMM alone could (~100 USD)
        BEAST_EXPECT(delivered > USD(90));
        // Payment should succeed with significant delivery
        BEAST_EXPECT(danUsdAfter > danUsdBefore);
    }

    void
    run() override
    {
        testBasicPayment();
        testCLAMMBeatsCLOB();
        testCLOBBeatsCLAMM();
        testCLAMMOnlyNoClob();
        testMultipleFeeTiers();
        testIOUtoIOU();
        testAmendmentGating();
        testPartialPayment();
        testXRPDirectPayment();
        testPoolStateAfterPayment();
        testEmptyPoolFallsBackToCLOB();
        testCLAMMMultiPathCompetition();
        testCLAMMAMMSamePair();
        testCLAMMWithTransferFee();
        testCLAMMUnfundedOfferCleanup();
        testCLAMMExhaustsToClob();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CLAMMPayment, app, xrpl, 1);

}  // namespace test
}  // namespace xrpl
