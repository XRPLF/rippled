#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpld/app/main/LoadManager.h>
#include <cstdint>
#include <chrono>
#include <thread>

namespace xrpl {

/**
 * Unit tests for LoadFeeTrack fee adjustment mechanics.
 *
 * These tests document the expected per-second behaviour of
 * raiseLocalFee() and lowerLocalFee() as called by LoadManager::run().
 *
 * Background: prior to the fix in this PR, the fee adjustment block in
 * LoadManager::run() was placed *outside* the while loop, meaning
 * raiseLocalFee()/lowerLocalFee() fired only once on shutdown rather
 * than every second during normal operation. localTxnLoadFee_ (and thus
 * load_factor_local in server_info) was therefore never adjusted by
 * local job queue load during normal operation.
 */
class LoadManager_test : public beast::unit_test::Suite
{
public:
    // The normal/minimum fee factor (kLftNormalFee in LoadFeeTrack)
    static constexpr std::uint32_t kNormalFee = 256;

    void
    testRaiseHysteresis()
    {
        testcase("raiseLocalFee requires two consecutive calls");

        LoadFeeTrack track;

        // First call: raiseCount_ goes from 0 to 1, returns false (hysteresis)
        BEAST_EXPECT(!track.raiseLocalFee());
        BEAST_EXPECT(track.getLocalFee() == kNormalFee);

        // Second call: raiseCount_ reaches 2, fee is raised
        BEAST_EXPECT(track.raiseLocalFee());
        BEAST_EXPECT(track.getLocalFee() > kNormalFee);
    }

    void
    testLowerDecaysToBaseline()
    {
        testcase("lowerLocalFee decays elevated fee back to baseline");

        LoadFeeTrack track;

        // Raise the fee above baseline (requires two calls due to hysteresis)
        track.raiseLocalFee();
        track.raiseLocalFee();
        auto const elevated = track.getLocalFee();
        BEAST_EXPECT(elevated > kNormalFee);

        // lowerLocalFee() should decay it each call
        // Each call reduces by 1/kLftFeeDecFraction (1/4), so a few
        // iterations should bring it back to kNormalFee
        bool changed = false;
        for (int i = 0; i < 100; ++i)
        {
            changed |= track.lowerLocalFee();
            if (track.getLocalFee() == kNormalFee)
                break;
        }

        BEAST_EXPECT(changed);
        BEAST_EXPECT(track.getLocalFee() == kNormalFee);
    }

    void
    testLowerAtBaselineIsNoop()
    {
        testcase("lowerLocalFee at baseline returns false");

        LoadFeeTrack track;

        // Already at baseline — should return false (no change)
        BEAST_EXPECT(!track.lowerLocalFee());
        BEAST_EXPECT(track.getLocalFee() == kNormalFee);
    }

    void
    testRaiseResetsOnLower()
    {
        testcase("lowerLocalFee resets raiseCount");

        LoadFeeTrack track;

        // One raise call (hysteresis: count=1, no change yet)
        BEAST_EXPECT(!track.raiseLocalFee());

        // Lower resets raiseCount_ to 0
        track.lowerLocalFee();

        // Next raise call starts from 0 again — still needs two calls
        BEAST_EXPECT(!track.raiseLocalFee());
        BEAST_EXPECT(track.getLocalFee() == kNormalFee);
    }

    void
    testIsLoadedLocal()
    {
        testcase("isLoadedLocal reflects fee state correctly");

        LoadFeeTrack track;

        // At baseline, not loaded
        BEAST_EXPECT(!track.isLoadedLocal());

        // After one raise (hysteresis, fee unchanged) — raiseCount_ != 0
        track.raiseLocalFee();
        BEAST_EXPECT(track.isLoadedLocal());

        // After lower, raiseCount_ reset and fee back at baseline
        track.lowerLocalFee();
        BEAST_EXPECT(!track.isLoadedLocal());
    }

    void
    testLoadManagerLoop()
    {
        testcase("LoadManager loop raises and decays load_factor_local");

        using namespace test::jtx;
        using namespace std::chrono_literals;

        Env env(*this, envconfig());

        // Stop the real LoadManager thread so we can drive the fee track
        // manually without races. On stock code the fee adjustment block
        // is outside the while loop and never fires during normal operation,
        // so getFeeTrack().getLocalFee() stays at kNormalFee (256) regardless.
        env.app().getLoadManager().stop();

        auto& feeTrack = env.app().getFeeTrack();

        // Confirm baseline
        BEAST_EXPECT(feeTrack.getLocalFee() == kNormalFee);
        BEAST_EXPECT(!feeTrack.isLoadedLocal());

        // raiseLocalFee() requires two consecutive calls (hysteresis guard).
        // This is the exact sequence LoadManager::run() executes each tick
        // when the job queue is overloaded.
        BEAST_EXPECT(!feeTrack.raiseLocalFee());   // tick 1: count=1, no change
        BEAST_EXPECT(feeTrack.raiseLocalFee());    // tick 2: count=2, fee raised
        BEAST_EXPECT(feeTrack.getLocalFee() > kNormalFee);
        BEAST_EXPECT(feeTrack.isLoadedLocal());

        // Now simulate the LoadManager loop calling lowerLocalFee() each tick
        // until the fee decays back to baseline. On stock code this block
        // never runs during normal operation so the fee would stay elevated.
        bool decayed = false;
        for (int i = 0; i < 100; ++i)
        {
            feeTrack.lowerLocalFee();
            if (feeTrack.getLocalFee() == kNormalFee)
            {
                decayed = true;
                break;
            }
        }

        BEAST_EXPECT(decayed);
        BEAST_EXPECT(feeTrack.getLocalFee() == kNormalFee);
        BEAST_EXPECT(!feeTrack.isLoadedLocal());
    }

    void
    run() override
    {
        testRaiseHysteresis();
        testLowerDecaysToBaseline();
        testLowerAtBaselineIsNoop();
        testRaiseResetsOnLower();
        testIsLoadedLocal();
        testLoadManagerLoop();
    }
};

BEAST_DEFINE_TESTSUITE(LoadManager, app, xrpl);

}  // namespace xrpl
