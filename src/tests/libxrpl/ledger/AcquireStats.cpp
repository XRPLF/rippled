/**
 * @file AcquireStats.cpp
 * Unit tests for the ledger-acquisition counters (AcquireStats.h).
 *
 * The counters exist to make one specific failure readable from metrics: a
 * saturated job lane makes the acquisition timer re-arm without running its
 * body, so the retry count never advances and the give-up path never fires.
 * Deferrals climbing while timeouts stay flat is the only signature of that,
 * so the tests below pin down that the two counters move independently and
 * that a stalled run and a healthy run produce different, exact readings.
 *
 * AcquireStats is header-only, so nothing from xrpld needs to be linked here.
 *
 * The counts are held in telemetry::Counter, which carries no storage and
 * records nothing when telemetry is compiled out. Every expectation on a
 * non-zero count therefore has a mirror expectation of zero, so this file
 * pins both configurations rather than leaving one of them unasserted.
 * Expectations of zero that hold either way -- a deferral not disturbing the
 * timeout counter, for instance -- are asserted unconditionally.
 */

#include <xrpld/app/ledger/AcquireStats.h>

#include <xrpl/telemetry/Recording.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

namespace {

using xrpl::AcquireStats;
namespace telemetry = xrpl::telemetry;

/**
 * A fresh instance reads zero on every counter, so a later test can attribute
 * every increment to its own call rather than to construction. The reading is
 * the same in both configurations, which is what lets a caller report these
 * accessors without knowing which build it is in.
 */
TEST(AcquireStatsTest, StartsAtZero)
{
    // Braces matter: without telemetry the counters hold no state, so the type
    // is trivially default constructible and MSVC rejects a const instance left
    // to the compiler-generated constructor.
    AcquireStats const stats{};

    EXPECT_EQ(stats.getDeferrals(), 0u);
    EXPECT_EQ(stats.getTimeouts(), 0u);
    EXPECT_EQ(stats.getGiveUps(), 0u);
    EXPECT_EQ(stats.getAborts(), 0u);
    EXPECT_EQ(stats.getAbortsWithPartialWork(), 0u);
    EXPECT_EQ(stats.getCompletions(), 0u);
    EXPECT_EQ(stats.getSweepEvictions(), 0u);
}

/**
 * Each counter must move alone, and the others must stay exactly where they
 * were. A deferral that also nudged the timeout counter would erase the
 * divergence that identifies the stall, so each step asserts both the counter
 * that should have moved and the ones that must not have.
 */
TEST(AcquireStatsTest, CountersAdvanceIndependently)
{
    AcquireStats stats;

    stats.recordDeferral();
    stats.recordDeferral();
    stats.recordDeferral();
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stats.getDeferrals(), 3u);
    }
    else
    {
        EXPECT_EQ(stats.getDeferrals(), 0u);
    }
    // Zero either way: a deferral must not reach the other counters.
    EXPECT_EQ(stats.getTimeouts(), 0u);
    EXPECT_EQ(stats.getGiveUps(), 0u);

    stats.recordTimeout();
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stats.getTimeouts(), 1u);
        EXPECT_EQ(stats.getDeferrals(), 3u);
    }
    else
    {
        EXPECT_EQ(stats.getTimeouts(), 0u);
        EXPECT_EQ(stats.getDeferrals(), 0u);
    }

    stats.recordGiveUp();
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stats.getGiveUps(), 1u);
        EXPECT_EQ(stats.getTimeouts(), 1u);
        EXPECT_EQ(stats.getDeferrals(), 3u);
    }
    else
    {
        EXPECT_EQ(stats.getGiveUps(), 0u);
        EXPECT_EQ(stats.getTimeouts(), 0u);
        EXPECT_EQ(stats.getDeferrals(), 0u);
    }

    stats.recordCompletion();
    stats.recordCompletion();
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stats.getCompletions(), 2u);
    }
    else
    {
        EXPECT_EQ(stats.getCompletions(), 0u);
    }
    EXPECT_EQ(stats.getSweepEvictions(), 0u);

    stats.recordSweepEviction();
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stats.getSweepEvictions(), 1u);
        EXPECT_EQ(stats.getCompletions(), 2u);
    }
    else
    {
        EXPECT_EQ(stats.getSweepEvictions(), 0u);
        EXPECT_EQ(stats.getCompletions(), 0u);
    }

    // Nothing above records an abort, so both abort counters stay at zero.
    EXPECT_EQ(stats.getAborts(), 0u);
    EXPECT_EQ(stats.getAbortsWithPartialWork(), 0u);
}

/**
 * An abort that discards a partly built map is the expensive case, because the
 * whole acquisition restarts. It must be distinguishable from a cheap abort
 * that had built nothing yet, and the cheap one must leave the partial-work
 * counter untouched.
 */
TEST(AcquireStatsTest, AbortDistinguishesPartialWork)
{
    AcquireStats stats;

    stats.recordAbort(false);
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stats.getAborts(), 1u);
    }
    else
    {
        EXPECT_EQ(stats.getAborts(), 0u);
    }
    // Zero either way: a cheap abort must not reach the partial-work counter.
    EXPECT_EQ(stats.getAbortsWithPartialWork(), 0u);

    stats.recordAbort(true);
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stats.getAborts(), 2u);
        EXPECT_EQ(stats.getAbortsWithPartialWork(), 1u);
    }
    else
    {
        EXPECT_EQ(stats.getAborts(), 0u);
        EXPECT_EQ(stats.getAbortsWithPartialWork(), 0u);
    }

    // An abort never counts as a completion or a give-up.
    EXPECT_EQ(stats.getCompletions(), 0u);
    EXPECT_EQ(stats.getGiveUps(), 0u);
}

/**
 * The stalled shape: many deferrals, no timeouts, no completions, and sweeps
 * discarding partial work. Timeouts staying at exactly zero is what proves the
 * give-up path cannot fire, so that assertion is the point of the test.
 */
TEST(AcquireStatsTest, StalledShapeIsDistinguishable)
{
    AcquireStats stalled;
    for (int i = 0; i < 1000; ++i)
        stalled.recordDeferral();
    for (int i = 0; i < 5; ++i)
    {
        stalled.recordSweepEviction();
        stalled.recordAbort(true);
    }

    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(stalled.getDeferrals(), 1000u);
        EXPECT_EQ(stalled.getSweepEvictions(), 5u);
        EXPECT_EQ(stalled.getAborts(), 5u);
        EXPECT_EQ(stalled.getAbortsWithPartialWork(), 5u);
    }
    else
    {
        // With telemetry compiled out every counter reads zero, so the stalled
        // shape is not readable at all. That is the intended reading, not a
        // healthy one.
        EXPECT_EQ(stalled.getDeferrals(), 0u);
        EXPECT_EQ(stalled.getSweepEvictions(), 0u);
        EXPECT_EQ(stalled.getAborts(), 0u);
        EXPECT_EQ(stalled.getAbortsWithPartialWork(), 0u);
    }

    // Zero either way, and the point of the test: no timeout accrued, so the
    // give-up path cannot fire.
    EXPECT_EQ(stalled.getTimeouts(), 0u);
    EXPECT_EQ(stalled.getGiveUps(), 0u);
    EXPECT_EQ(stalled.getCompletions(), 0u);
}

/**
 * The healthy shape, for contrast: timeouts accrue, give-up fires, and
 * completions dominate. The same seven counters, read the opposite way, which
 * is what makes the stalled reading above meaningful.
 */
TEST(AcquireStatsTest, HealthyShapeIsDistinguishable)
{
    AcquireStats healthy;
    for (int i = 0; i < 10; ++i)
        healthy.recordDeferral();
    for (int i = 0; i < 7; ++i)
        healthy.recordTimeout();
    healthy.recordGiveUp();
    for (int i = 0; i < 27; ++i)
        healthy.recordCompletion();

    if constexpr (telemetry::kEnabled)
    {
        EXPECT_EQ(healthy.getDeferrals(), 10u);
        EXPECT_EQ(healthy.getTimeouts(), 7u);
        EXPECT_EQ(healthy.getGiveUps(), 1u);
        EXPECT_EQ(healthy.getCompletions(), 27u);
    }
    else
    {
        EXPECT_EQ(healthy.getDeferrals(), 0u);
        EXPECT_EQ(healthy.getTimeouts(), 0u);
        EXPECT_EQ(healthy.getGiveUps(), 0u);
        EXPECT_EQ(healthy.getCompletions(), 0u);
    }

    // Zero either way: nothing above sweeps or aborts.
    EXPECT_EQ(healthy.getSweepEvictions(), 0u);
    EXPECT_EQ(healthy.getAborts(), 0u);
    EXPECT_EQ(healthy.getAbortsWithPartialWork(), 0u);
}

/**
 * Concurrent recording must lose no increment. Every counter is read as a
 * rate, so a dropped increment silently reads as a slower rate rather than as
 * an error.
 *
 * The expected totals below are written as literals on purpose. Deriving them
 * from the loop bounds would make the assertion agree with the loop by
 * construction and pass even if every increment were lost.
 */
TEST(AcquireStatsTest, ConcurrentRecordingLosesNothing)
{
    AcquireStats stats;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&stats]() {
            for (int i = 0; i < kPerThread; ++i)
            {
                stats.recordDeferral();
                stats.recordCompletion();
            }
        });
    }
    for (auto& th : threads)
        th.join();

    if constexpr (telemetry::kEnabled)
    {
        // 4 threads * 1000 iterations, stated independently of the loop bounds.
        EXPECT_EQ(stats.getDeferrals(), std::uint64_t{4000});
        EXPECT_EQ(stats.getCompletions(), std::uint64_t{4000});
    }
    else
    {
        // Nothing was recorded, so concurrent calls also have nothing to lose.
        EXPECT_EQ(stats.getDeferrals(), std::uint64_t{0});
        EXPECT_EQ(stats.getCompletions(), std::uint64_t{0});
    }

    // The threads record only those two events, so the rest stay at zero.
    EXPECT_EQ(stats.getTimeouts(), 0u);
    EXPECT_EQ(stats.getGiveUps(), 0u);
    EXPECT_EQ(stats.getAborts(), 0u);
    EXPECT_EQ(stats.getAbortsWithPartialWork(), 0u);
    EXPECT_EQ(stats.getSweepEvictions(), 0u);
}

}  // namespace
