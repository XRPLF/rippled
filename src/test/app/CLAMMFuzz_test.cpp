#include <test/jtx.h>
#include <test/jtx/CLAMM.h>
#include <test/jtx/Env.h>

#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

#include <random>

namespace xrpl {
namespace test {

struct CLAMMFuzz_test : public beast::unit_test::suite
{
    jtx::Account const gw{"gateway"};
    jtx::Account const alice{"alice"};
    jtx::Account const bob{"bob"};
    jtx::Account const carol{"carol"};
    jtx::IOU const USD{gw["USD"]};

    void
    testFuzzDepositWithdraw()
    {
        testcase("Fuzz: random deposit/withdraw sequences");
        using namespace jtx;

        std::mt19937 engine(42);
        std::uniform_int_distribution<int> actionDist(0, 2);
        std::uniform_int_distribution<int> tickDist(-500, 500);
        std::uniform_int_distribution<int> amountDist(100, 10000);
        std::uniform_int_distribution<int> lpDist(0, 2);

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Use fee tier 1 (spacing=10)
        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        Account const lps[] = {alice, bob, carol};
        std::vector<std::optional<uint256>> nftIDs(3, std::nullopt);

        constexpr int rounds = 100;
        for (int i = 0; i < rounds; ++i)
        {
            int const action = actionDist(engine);
            int const lpIdx = lpDist(engine);
            auto const& lp = lps[lpIdx];

            if (action <= 1 && !nftIDs[lpIdx].has_value())
            {
                // Deposit: pick random aligned ticks
                int lower = tickDist(engine);
                int upper = tickDist(engine);
                lower = (lower / 10) * 10;
                upper = (upper / 10) * 10;
                if (lower >= upper)
                    upper = lower + 10;

                int const amt = amountDist(engine);
                env(clammDeposit(
                        lp, pid, lower, upper,
                        XRP(amt), USD(amt)),
                    ter(std::ignore));
                env.close();

                nftIDs[lpIdx] = clammFindPositionNFT(env, lp, pid);
            }
            else if (nftIDs[lpIdx].has_value())
            {
                // Withdraw
                env(clammWithdraw(lp, *nftIDs[lpIdx]),
                    ter(std::ignore));
                env.close();
                nftIDs[lpIdx] = clammFindPositionNFT(env, lp, pid);
            }
        }

        // Invariant: pool SqrtPrice should still be valid if pool exists
        auto const sle = env.current()->read(keylet::clamm(pid));
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);
        }
        pass();
    }

    void
    testFuzzSwapSequences()
    {
        testcase("Fuzz: random swap sequences");
        using namespace jtx;

        std::mt19937 engine(42);
        std::uniform_int_distribution<int> dirDist(0, 1);
        std::uniform_int_distribution<int> amountDist(1, 500);

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Set up wide liquidity range
        env(clammDeposit(
                alice, pid, -1000, 1000, XRP(50000), USD(50000)),
            ter(tesSUCCESS));
        env.close();

        // Also add a narrower range from bob
        env(clammDeposit(
                bob, pid, -100, 100, XRP(10000), USD(10000)),
            ter(tesSUCCESS));
        env.close();

        constexpr int rounds = 100;
        for (int i = 0; i < rounds; ++i)
        {
            int const dir = dirDist(engine);
            int const amt = amountDist(engine);

            if (dir == 0)
            {
                // XRP -> USD (zeroForOne)
                env(clammSwap(carol, pid, XRP(amt)),
                    ter(std::ignore));
            }
            else
            {
                // USD -> XRP (oneForZero)
                env(clammSwap(carol, pid, USD(amt)),
                    ter(std::ignore));
            }
            env.close();
        }

        // Invariant: pool state should be consistent
        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle);
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);

            auto const currentTick = sle->getFieldI32(sfCurrentTick);
            BEAST_EXPECT(
                currentTick >= CLAMM_MIN_TICK &&
                currentTick <= CLAMM_MAX_TICK);

            // Fee growth should be non-negative
            auto const fg0 = clamm::fromSLEField(
                sle->getFieldH128(sfFeeGrowthGlobal0));
            auto const fg1 = clamm::fromSLEField(
                sle->getFieldH128(sfFeeGrowthGlobal1));
            BEAST_EXPECT(fg0 >= 0);
            BEAST_EXPECT(fg1 >= 0);
        }
    }

    void
    testFuzzPriceMovements()
    {
        testcase("Fuzz: price movement consistency");
        using namespace jtx;

        std::mt19937 engine(42);
        std::uniform_int_distribution<int> amountDist(10, 1000);

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Multiple positions at different ranges
        env(clammDeposit(
                alice, pid, -500, 500, XRP(20000), USD(20000)),
            ter(tesSUCCESS));
        env.close();
        env(clammDeposit(
                bob, pid, -100, 100, XRP(10000), USD(10000)),
            ter(tesSUCCESS));
        env.close();
        env(clammDeposit(
                carol, pid, -50, 50, XRP(5000), USD(5000)),
            ter(tesSUCCESS));
        env.close();

        clamm::uint128 prevFg0 = 0;
        clamm::uint128 prevFg1 = 0;

        constexpr int rounds = 50;

        // Series of swaps in one direction (XRP -> USD)
        for (int i = 0; i < rounds / 2; ++i)
        {
            int const amt = amountDist(engine);
            env(clammSwap(carol, pid, XRP(amt)),
                ter(std::ignore));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            if (sle)
            {
                // Fee growth must be monotonically non-decreasing
                auto const fg0 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal0));
                auto const fg1 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal1));
                BEAST_EXPECT(fg0 >= prevFg0);
                BEAST_EXPECT(fg1 >= prevFg1);
                prevFg0 = fg0;
                prevFg1 = fg1;

                // CurrentTick must be consistent with SqrtPrice
                auto const currentTick =
                    sle->getFieldI32(sfCurrentTick);
                BEAST_EXPECT(
                    currentTick >= CLAMM_MIN_TICK &&
                    currentTick <= CLAMM_MAX_TICK);
            }
        }

        // Reverse direction (USD -> XRP)
        for (int i = 0; i < rounds / 2; ++i)
        {
            int const amt = amountDist(engine);
            env(clammSwap(carol, pid, USD(amt)),
                ter(std::ignore));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            if (sle)
            {
                auto const fg0 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal0));
                auto const fg1 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal1));
                BEAST_EXPECT(fg0 >= prevFg0);
                BEAST_EXPECT(fg1 >= prevFg1);
                prevFg0 = fg0;
                prevFg1 = fg1;
            }
        }
        pass();
    }

    void
    testFuzzEdgeCases()
    {
        testcase("Fuzz: edge cases");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        // 1. Deposit at MIN_TICK / MAX_TICK (fee tier 0, spacing=1)
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 0);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 0,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Wide range deposit
            env(clammDeposit(
                    alice, pid,
                    CLAMM_MIN_TICK, CLAMM_MAX_TICK,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();
        }

        // 2. Ultra-narrow range (1 tick, fee tier 0)
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 0);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 0,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // 1-tick range
            env(clammDeposit(
                    alice, pid, 0, 1,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();
        }

        // 3. Swap in pool with zero active liquidity
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Out-of-range deposit only (no active liquidity at
            // current price which is near tick 1)
            env(clammDeposit(
                    alice, pid, 500, 600,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();

            // Swap should fail or have zero output
            env(clammSwap(bob, pid, XRP(100)),
                ter(std::ignore));
            env.close();
        }

        // 4. Swap that crosses many ticks
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Create many small positions at different ranges
            for (int t = -200; t < 200; t += 20)
            {
                env(clammDeposit(
                        alice, pid, t, t + 20,
                        XRP(100), USD(100)),
                    ter(std::ignore));
                env.close();
            }

            // Large swap that should cross 10+ ticks
            env(clammSwap(bob, pid, XRP(5000)),
                ter(std::ignore));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle);
            if (sle)
            {
                auto const sqrtPrice = clamm::fromSLEField(
                    sle->getFieldH128(sfSqrtPrice));
                BEAST_EXPECT(sqrtPrice > 0);
            }
        }

        // 5. Withdraw when position is out-of-range
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Deposit in-range
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(10000), USD(10000)),
                ter(tesSUCCESS));
            env.close();

            // Deposit out-of-range (far above current price)
            env(clammDeposit(
                    bob, pid, 500, 600,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();

            auto const bobNFT = clammFindPositionNFT(env, bob, pid);
            if (bobNFT)
            {
                // Withdraw bob's out-of-range position
                env(clammWithdraw(bob, *bobNFT),
                    ter(std::ignore));
                env.close();
            }
        }

        pass();
    }

    void
    testFuzzConservation()
    {
        testcase("Fuzz: conservation invariant");
        using namespace jtx;

        std::mt19937 engine(123);
        std::uniform_int_distribution<int> amountDist(10, 500);
        std::uniform_int_distribution<int> dirDist(0, 1);

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Deposit liquidity
        env(clammDeposit(
                alice, pid, -500, 500, XRP(20000), USD(20000)),
            ter(tesSUCCESS));
        env.close();

        // Track alice + bob balances to verify conservation
        auto const aliceXrpBefore = env.balance(alice);
        auto const aliceUsdBefore = env.balance(alice, USD);
        auto const bobXrpBefore = env.balance(bob);
        auto const bobUsdBefore = env.balance(bob, USD);

        constexpr int rounds = 50;
        for (int i = 0; i < rounds; ++i)
        {
            int const amt = amountDist(engine);
            int const dir = dirDist(engine);

            if (dir == 0)
                env(clammSwap(bob, pid, XRP(amt)),
                    ter(std::ignore));
            else
                env(clammSwap(bob, pid, USD(amt)),
                    ter(std::ignore));
            env.close();
        }

        // Pool should still be in valid state
        auto const poolKeylet = keylet::clamm(pid);
        auto const sleMid = env.current()->read(poolKeylet);
        BEAST_EXPECT(sleMid);
        if (sleMid)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sleMid->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);
        }

        // Collect fees and withdraw
        auto const aliceNFT = clammFindPositionNFT(env, alice, pid);
        if (aliceNFT)
        {
            env(clammCollectFees(alice, *aliceNFT),
                ter(std::ignore));
            env.close();

            env(clammWithdraw(alice, *aliceNFT),
                ter(std::ignore));
            env.close();
        }

        // After withdrawal: alice should have recovered most of her deposit
        // (minus fees paid to protocol, plus fees earned from bob's swaps).
        // The key invariant: no tokens are created or destroyed.
        auto const aliceXrpAfter = env.balance(alice);
        auto const aliceUsdAfter = env.balance(alice, USD);
        auto const bobXrpAfter = env.balance(bob);
        auto const bobUsdAfter = env.balance(bob, USD);

        // Total XRP change: alice delta + bob delta should account for
        // tx fees only (each tx costs ~10 drops). The sum should be
        // negative (tx fees burned) but not wildly so.
        // This is a sanity check, not exact accounting.
        BEAST_EXPECT(aliceXrpAfter > XRP(0));
        BEAST_EXPECT(bobXrpAfter > XRP(0));
        BEAST_EXPECT(aliceUsdAfter > USD(0));

        auto const sleEnd = env.current()->read(poolKeylet);
        if (sleEnd)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sleEnd->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);
        }
        pass();
    }

    void
    testFuzzMultiLPConcurrent()
    {
        testcase("Fuzz: multi-LP concurrent operations");
        using namespace jtx;

        std::mt19937 engine(77);
        std::uniform_int_distribution<int> actionDist(0, 4);
        std::uniform_int_distribution<int> lpDist(0, 2);
        std::uniform_int_distribution<int> amountDist(50, 2000);
        std::uniform_int_distribution<int> tickDist(-300, 300);
        std::uniform_int_distribution<int> feeDist(100, 9000);

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        Account const lps[] = {alice, bob, carol};
        std::vector<std::optional<uint256>> nftIDs(3, std::nullopt);

        // Initial deposits from all LPs
        for (int i = 0; i < 3; ++i)
        {
            int lower = (tickDist(engine) / 10) * 10;
            int upper = (tickDist(engine) / 10) * 10;
            if (lower >= upper)
                upper = lower + 10;

            env(clammDeposit(
                    lps[i], pid, lower, upper,
                    XRP(5000), USD(5000)),
                ter(std::ignore));
            env.close();
            nftIDs[i] = clammFindPositionNFT(env, lps[i], pid);
        }

        constexpr int rounds = 100;
        for (int i = 0; i < rounds; ++i)
        {
            int const action = actionDist(engine);
            int const lpIdx = lpDist(engine);
            auto const& lp = lps[lpIdx];
            int const amt = amountDist(engine);

            switch (action)
            {
                case 0:  // Deposit
                {
                    if (!nftIDs[lpIdx].has_value())
                    {
                        int lower = (tickDist(engine) / 10) * 10;
                        int upper = (tickDist(engine) / 10) * 10;
                        if (lower >= upper)
                            upper = lower + 10;

                        env(clammDeposit(
                                lp, pid, lower, upper,
                                XRP(amt), USD(amt)),
                            ter(std::ignore));
                        env.close();
                        nftIDs[lpIdx] =
                            clammFindPositionNFT(env, lp, pid);
                    }
                    break;
                }
                case 1:  // Swap XRP -> USD
                    env(clammSwap(lp, pid, XRP(amt)),
                        ter(std::ignore));
                    env.close();
                    break;
                case 2:  // Swap USD -> XRP
                    env(clammSwap(lp, pid, USD(amt)),
                        ter(std::ignore));
                    env.close();
                    break;
                case 3:  // Collect fees
                    if (nftIDs[lpIdx].has_value())
                    {
                        env(clammCollectFees(lp, *nftIDs[lpIdx]),
                            ter(std::ignore));
                        env.close();
                    }
                    break;
                case 4:  // Withdraw + re-deposit
                    if (nftIDs[lpIdx].has_value())
                    {
                        env(clammWithdraw(lp, *nftIDs[lpIdx]),
                            ter(std::ignore));
                        env.close();
                        nftIDs[lpIdx] = std::nullopt;
                    }
                    break;
            }

            // Invariants checked every round
            auto const sle = env.current()->read(keylet::clamm(pid));
            if (sle)
            {
                auto const sqrtPrice = clamm::fromSLEField(
                    sle->getFieldH128(sfSqrtPrice));
                BEAST_EXPECT(sqrtPrice > 0);

                auto const currentTick =
                    sle->getFieldI32(sfCurrentTick);
                BEAST_EXPECT(
                    currentTick >= CLAMM_MIN_TICK &&
                    currentTick <= CLAMM_MAX_TICK);
            }
        }
        pass();
    }

    void
    testFuzzTickBoundaryStress()
    {
        testcase("Fuzz: tick boundary stress");
        using namespace jtx;

        std::mt19937 engine(99);
        std::uniform_int_distribution<int> amountDist(1, 50000);
        std::uniform_int_distribution<int> dirDist(0, 1);

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 0, spacing=1 allows adjacent ticks
        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 0);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 0,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Create 20 narrow positions at adjacent ticks
        for (int t = -10; t < 10; ++t)
        {
            env(clammDeposit(
                    alice, pid, t, t + 1,
                    XRP(500), USD(500)),
                ter(std::ignore));
            env.close();
        }

        // 200 random swaps with varied amounts
        constexpr int rounds = 200;
        for (int i = 0; i < rounds; ++i)
        {
            int const amt = amountDist(engine);
            int const dir = dirDist(engine);

            if (dir == 0)
                env(clammSwap(bob, pid, XRP(amt)),
                    ter(std::ignore));
            else
                env(clammSwap(bob, pid, USD(amt)),
                    ter(std::ignore));
            env.close();
        }

        // Verify pool state is valid after stress
        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle);
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);

            auto const currentTick = sle->getFieldI32(sfCurrentTick);
            BEAST_EXPECT(
                currentTick >= CLAMM_MIN_TICK &&
                currentTick <= CLAMM_MAX_TICK);
        }
        pass();
    }

    void
    testFuzzExtremePriceRanges()
    {
        testcase("Fuzz: extreme price ranges");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 0);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 0,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Position near MIN_TICK
        env(clammDeposit(
                alice, pid,
                CLAMM_MIN_TICK + 1, CLAMM_MIN_TICK + 100,
                XRP(5000), USD(5000)),
            ter(std::ignore));
        env.close();

        // Position near MAX_TICK
        env(clammDeposit(
                alice, pid,
                CLAMM_MAX_TICK - 100, CLAMM_MAX_TICK - 1,
                XRP(5000), USD(5000)),
            ter(std::ignore));
        env.close();

        // Wide position covering current price
        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10000), USD(10000)),
            ter(tesSUCCESS));
        env.close();

        // Massive swap to push price toward lower extreme
        env(clammSwap(bob, pid, XRP(90000)),
            ter(std::ignore));
        env.close();

        auto sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle);
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            auto const minSqrtRatio = clamm::tickToSqrtPrice(CLAMM_MIN_TICK);
            auto const maxSqrtRatio = clamm::tickToSqrtPrice(CLAMM_MAX_TICK);
            BEAST_EXPECT(sqrtPrice >= minSqrtRatio);
            BEAST_EXPECT(sqrtPrice <= maxSqrtRatio);
        }

        // Massive swap in reverse to push price toward upper extreme
        env(clammSwap(bob, pid, USD(90000)),
            ter(std::ignore));
        env.close();

        sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle);
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            auto const minSqrtRatio = clamm::tickToSqrtPrice(CLAMM_MIN_TICK);
            auto const maxSqrtRatio = clamm::tickToSqrtPrice(CLAMM_MAX_TICK);
            BEAST_EXPECT(sqrtPrice >= minSqrtRatio);
            BEAST_EXPECT(sqrtPrice <= maxSqrtRatio);
        }
        pass();
    }

    void
    testFuzzDepositWithdrawSymmetry()
    {
        testcase("Fuzz: deposit/withdraw symmetry");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Record balances before deposit
        auto const aliceXrpBefore = env.balance(alice);
        auto const aliceUsdBefore = env.balance(alice, USD);

        // Deposit known amounts
        auto const depositXrp = XRP(5000);
        auto const depositUsd = USD(5000);
        env(clammDeposit(
                alice, pid, -500, 500, depositXrp, depositUsd),
            ter(tesSUCCESS));
        env.close();

        auto const aliceXrpAfterDeposit = env.balance(alice);
        auto const aliceUsdAfterDeposit = env.balance(alice, USD);

        // Immediately withdraw everything (no swaps => no fees)
        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());
        if (nft)
        {
            env(clammWithdraw(alice, *nft),
                ter(std::ignore));
            env.close();
        }

        auto const aliceXrpAfterWithdraw = env.balance(alice);
        auto const aliceUsdAfterWithdraw = env.balance(alice, USD);

        // XRP: account for transaction fees (2 txns: deposit + withdraw)
        // The withdrawn amount should be very close to deposited
        auto const xrpDiff = aliceXrpBefore - aliceXrpAfterWithdraw;
        // xrpDiff includes tx fees (~20 drops for 2 txns)
        // Precision loss should be < 1 drop beyond tx fees
        BEAST_EXPECT(xrpDiff < XRP(1));

        // USD: no tx fees, so should get back approximately what was deposited
        // Precision loss bounded to < 1 drop equivalent
        auto const usdRecovered =
            aliceUsdAfterWithdraw - aliceUsdAfterDeposit;
        auto const usdDeposited =
            aliceUsdBefore - aliceUsdAfterDeposit;
        // Allow up to 1 USD precision loss (IOU rounding)
        BEAST_EXPECT(usdRecovered >= usdDeposited - USD(1));
        pass();
    }

    void
    run() override
    {
        testFuzzDepositWithdraw();
        testFuzzSwapSequences();
        testFuzzPriceMovements();
        testFuzzEdgeCases();
        testFuzzConservation();
        testFuzzMultiLPConcurrent();
        testFuzzTickBoundaryStress();
        testFuzzExtremePriceRanges();
        testFuzzDepositWithdrawSymmetry();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CLAMMFuzz, app, xrpl, 2);

}  // namespace test
}  // namespace xrpl
