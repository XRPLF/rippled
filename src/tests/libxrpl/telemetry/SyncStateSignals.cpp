/**
 * @file SyncStateSignals.cpp
 * Unit tests for the decision rule behind the sync-state stall signals.
 *
 * `sync_state{metric="server_stall_seconds"}` and
 * `server_stall_events_total` are both derived from one rule:
 * LoadManager::evaluateStall(). The registry callbacks that export them only
 * read atomics, so the rule is the only place a defect can hide -- an
 * off-by-one on the threshold, or an episode counted once per tick instead of
 * once per stall, would silently misreport every stall.
 *
 * The rule is a pure `static constexpr` member, so these tests assert it
 * directly: no LoadManager instance, no monitor thread, no sleeping, and no
 * test-only mutator added to production code to make it reachable.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined, because that is the
 * configuration in which the test target has `src/` on its include path and can
 * therefore reach <xrpld/app/main/LoadManager.h>. The rule itself is not
 * telemetry-conditional; only this file's ability to include the header is.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpld/app/main/LoadManager.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

using namespace xrpl;
using namespace std::chrono_literals;

namespace {

/**
 * The threshold production uses (LoadManager::run's kReportingIntervalSeconds).
 */
constexpr auto kThreshold = 10s;

}  // namespace

// Below the threshold nothing is reported: a brief scheduling hiccup is not a
// stall, so both outputs stay at their healthy values. Asserts the boundary
// exactly at threshold-1, not merely "some small value".
TEST(SyncStateSignals, sub_threshold_stall_reports_healthy)
{
    auto const zero = LoadManager::evaluateStall(0, 0s, kThreshold);
    EXPECT_EQ(zero.seconds, 0U);
    EXPECT_FALSE(zero.newEpisode);

    // One second below the threshold is still healthy.
    auto const justUnder = LoadManager::evaluateStall(0, 9s, kThreshold);
    EXPECT_EQ(justUnder.seconds, 0U);
    EXPECT_FALSE(justUnder.newEpisode);
}

// The threshold is inclusive: exactly 10 s is reportable and starts an episode.
// This is the boundary the log line uses, so gauge and log must agree here.
TEST(SyncStateSignals, threshold_is_inclusive_and_starts_an_episode)
{
    auto const atThreshold = LoadManager::evaluateStall(0, kThreshold, kThreshold);
    EXPECT_EQ(atThreshold.seconds, 10U);
    EXPECT_TRUE(atThreshold.newEpisode);
}

// One continuous stall spanning many ticks is ONE episode. The seconds value
// tracks the growing duration while newEpisode stays false after the first
// tick -- this is what makes "one long stall" distinguishable from "repeated
// short stalls" on the dashboard.
TEST(SyncStateSignals, continuous_stall_counts_exactly_one_episode)
{
    // Tick 1: crosses the threshold.
    auto const first = LoadManager::evaluateStall(0, 10s, kThreshold);
    EXPECT_EQ(first.seconds, 10U);
    EXPECT_TRUE(first.newEpisode);

    // Ticks 2..4: still stalled, longer each time, but the SAME episode.
    auto const second = LoadManager::evaluateStall(first.seconds, 11s, kThreshold);
    EXPECT_EQ(second.seconds, 11U);
    EXPECT_FALSE(second.newEpisode);

    auto const third = LoadManager::evaluateStall(second.seconds, 90s, kThreshold);
    EXPECT_EQ(third.seconds, 90U);
    EXPECT_FALSE(third.newEpisode);

    auto const fourth = LoadManager::evaluateStall(third.seconds, 600s, kThreshold);
    EXPECT_EQ(fourth.seconds, 600U);
    EXPECT_FALSE(fourth.newEpisode);
}

// Recovery clears the seconds, and a LATER stall is a NEW episode. Without the
// healthy tick in between this would be indistinguishable from the continuous
// case above, so this is the test that pins the transition semantics.
TEST(SyncStateSignals, stall_after_recovery_is_a_new_episode)
{
    // Stalled, then recovered: seconds drop back to 0 and no episode starts on
    // the recovery tick itself.
    auto const stalled = LoadManager::evaluateStall(0, 30s, kThreshold);
    EXPECT_EQ(stalled.seconds, 30U);
    EXPECT_TRUE(stalled.newEpisode);

    auto const recovered = LoadManager::evaluateStall(stalled.seconds, 0s, kThreshold);
    EXPECT_EQ(recovered.seconds, 0U);
    EXPECT_FALSE(recovered.newEpisode);

    // Stalling again after recovery: a second, distinct episode.
    auto const again = LoadManager::evaluateStall(recovered.seconds, 15s, kThreshold);
    EXPECT_EQ(again.seconds, 15U);
    EXPECT_TRUE(again.newEpisode);
}

// A stall that decays to a sub-threshold value counts as recovered, so the next
// crossing is a new episode. Edge case: the previous value was non-zero but
// below the threshold, which must be treated as healthy, not as "still stalled".
TEST(SyncStateSignals, decay_below_threshold_is_treated_as_recovered)
{
    // Contrived previous value: below threshold, so it reads as healthy even
    // though it is non-zero. (Production never publishes such a value -- it
    // stores 0 when healthy -- so this pins the rule, not just the caller.)
    auto const crossing = LoadManager::evaluateStall(9, kThreshold, kThreshold);
    EXPECT_EQ(crossing.seconds, 10U);
    EXPECT_TRUE(crossing.newEpisode);
}

// Compile-time proof that the rule is genuinely constexpr and side-effect free:
// if it ever grows state or a runtime-only dependency, these fail the build
// rather than the run.
TEST(SyncStateSignals, rule_is_evaluated_at_compile_time)
{
    static_assert(LoadManager::evaluateStall(0, 10s, 10s).newEpisode);
    static_assert(LoadManager::evaluateStall(0, 10s, 10s).seconds == 10U);
    static_assert(!LoadManager::evaluateStall(0, 9s, 10s).newEpisode);
    static_assert(LoadManager::evaluateStall(0, 9s, 10s).seconds == 0U);
    static_assert(!LoadManager::evaluateStall(10, 20s, 10s).newEpisode);

    // Keeps the test body non-empty for readers scanning for an assertion.
    EXPECT_TRUE(LoadManager::evaluateStall(0, 10s, 10s).newEpisode);
}

#endif  // XRPL_ENABLE_TELEMETRY
