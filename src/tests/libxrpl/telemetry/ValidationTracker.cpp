/**
 * @file ValidationTracker.cpp
 * Unit tests for xrpl::telemetry::ValidationTracker.
 *
 * The tracker reads time through an injected function, so every test places
 * its events on a fake timeline. That is what makes a window edge, the grace
 * period and a bucket boundary reachable without waiting for one.
 */

#include <xrpld/telemetry/ValidationTracker.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

using namespace xrpl;
using namespace xrpl::telemetry;

using Tracker = xrpl::telemetry::ValidationTracker;

namespace {

/**
 * The fake current time. Every tracker built here reads it.
 */
Tracker::TimePoint gNow{};

/**
 * Time source handed to the tracker under test.
 */
Tracker::TimePoint
testNow()
{
    return gNow;
}

/**
 * Move the fake clock forward.
 */
void
advance(std::chrono::seconds by)
{
    gNow += by;
}

/**
 * Build a tracker on a fresh timeline. The start point is far from the clock
 * epoch so subtracting a window cannot go negative.
 */
Tracker
makeTracker()
{
    gNow = Tracker::TimePoint{} + std::chrono::hours(1000);
    return Tracker(&testNow);
}

/**
 * Push the clock past the grace period and reconcile, which is what turns a
 * recorded event into an agreement or a miss.
 */
void
settle(Tracker& t)
{
    advance(Tracker::gracePeriod() + std::chrono::seconds(1));
    t.reconcile();
}

/**
 * Distinct ledger hash per integer seed.
 */
uint256
makeHash(std::uint64_t n)
{
    return uint256(n);
}

}  // namespace

// ---- pairing ---------------------------------------------------------------

TEST(ValidationTracker, both_sides_within_grace_counts_one_agreement)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(1), 1);
    t.recordNetworkValidation(makeHash(1), 1);
    settle(t);

    EXPECT_EQ(t.agreements1h(), 1u);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.agreements24h(), 1u);
    EXPECT_EQ(t.missed24h(), 0u);
    EXPECT_EQ(t.agreements7d(), 1u);
    EXPECT_EQ(t.missed7d(), 0u);
    EXPECT_DOUBLE_EQ(t.agreementPct1h(), 100.0);
    EXPECT_DOUBLE_EQ(t.agreementPct24h(), 100.0);
    EXPECT_DOUBLE_EQ(t.agreementPct7d(), 100.0);
}

TEST(ValidationTracker, only_our_side_counts_one_miss)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(2), 2);
    settle(t);

    EXPECT_EQ(t.agreements1h(), 0u);
    EXPECT_EQ(t.missed1h(), 1u);
    EXPECT_DOUBLE_EQ(t.agreementPct1h(), 0.0);
}

TEST(ValidationTracker, only_network_side_counts_one_miss)
{
    auto t = makeTracker();
    t.recordNetworkValidation(makeHash(3), 3);
    settle(t);

    EXPECT_EQ(t.missed1h(), 1u);
    EXPECT_EQ(t.agreements1h(), 0u);
}

TEST(ValidationTracker, inside_the_grace_period_nothing_is_counted)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(4), 4);
    t.recordNetworkValidation(makeHash(4), 4);

    // One second short of the grace period, so the pair is still undecided.
    advance(Tracker::gracePeriod() - std::chrono::seconds(1));
    t.reconcile();

    EXPECT_EQ(t.agreements1h(), 0u);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_DOUBLE_EQ(t.agreementPct1h(), 0.0);

    // The message counters do not wait for a decision.
    EXPECT_EQ(t.totalValidationsSent(), 1u);
    EXPECT_EQ(t.totalValidationsChecked(), 1u);
}

TEST(ValidationTracker, percentage_mixes_agreements_and_misses)
{
    auto t = makeTracker();
    for (std::uint64_t i = 0; i < 3; ++i)
    {
        t.recordOurValidation(makeHash(100 + i), static_cast<LedgerIndex>(100 + i));
        t.recordNetworkValidation(makeHash(100 + i), static_cast<LedgerIndex>(100 + i));
    }
    t.recordOurValidation(makeHash(200), 200);
    settle(t);

    EXPECT_EQ(t.agreements1h(), 3u);
    EXPECT_EQ(t.missed1h(), 1u);
    EXPECT_NEAR(t.agreementPct1h(), 75.0, 1e-9);
}

// ---- an event is counted exactly once --------------------------------------

TEST(ValidationTracker, a_decided_ledger_is_not_counted_twice)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(5), 5);
    t.recordNetworkValidation(makeHash(5), 5);
    settle(t);
    EXPECT_EQ(t.agreements1h(), 1u);

    // The same hash arrives again long after it was counted and evicted. It
    // must not open a fresh pending entry and be counted a second time.
    advance(Tracker::lateRepairWindow() + std::chrono::seconds(1));
    t.reconcile();
    t.recordOurValidation(makeHash(5), 5);
    t.recordNetworkValidation(makeHash(5), 5);
    settle(t);

    EXPECT_EQ(t.agreements1h(), 1u);
    EXPECT_EQ(t.missed1h(), 0u);
}

TEST(ValidationTracker, duplicate_recording_of_one_ledger_counts_one_agreement)
{
    auto t = makeTracker();

    // Our side reports the same ledger twice before anything is decided.
    t.recordOurValidation(makeHash(30), 30);
    t.recordOurValidation(makeHash(30), 30);
    t.recordNetworkValidation(makeHash(30), 30);
    settle(t);

    EXPECT_EQ(t.agreements1h(), 1u);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.totalAgreements(), 1u);
    EXPECT_EQ(t.totalMissed(), 0u);
}

TEST(ValidationTracker, send_and_check_counters_count_messages_not_ledgers)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(31), 31);
    t.recordNetworkValidation(makeHash(31), 31);
    settle(t);
    EXPECT_EQ(t.agreements1h(), 1u);

    // The same ledger is reported again once it is past repair. Neither total
    // moves, but both message counters do.
    advance(Tracker::lateRepairWindow() + std::chrono::seconds(1));
    t.reconcile();
    t.recordOurValidation(makeHash(31), 31);
    t.recordNetworkValidation(makeHash(31), 31);
    settle(t);

    EXPECT_EQ(t.agreements1h(), 1u);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.totalAgreements(), 1u);
    EXPECT_EQ(t.totalMissed(), 0u);
    EXPECT_EQ(t.totalValidationsSent(), 2u);
    EXPECT_EQ(t.totalValidationsChecked(), 2u);
}

// ---- late repair -----------------------------------------------------------

TEST(ValidationTracker, gross_totals_count_only_the_first_classification)
{
    // Three agreements and two misses, all decided in one pass.
    auto t = makeTracker();
    for (unsigned i = 1; i <= 3; ++i)
    {
        t.recordOurValidation(makeHash(i), i);
        t.recordNetworkValidation(makeHash(i), i);
    }
    for (unsigned i = 4; i <= 5; ++i)
        t.recordNetworkValidation(makeHash(i), i);
    settle(t);

    // With no repair yet, the gross pair equals the net pair.
    EXPECT_EQ(t.totalAgreements(), 3u);
    EXPECT_EQ(t.totalAgreementsEver(), 3u);
    EXPECT_EQ(t.totalMissed(), 2u);
    EXPECT_EQ(t.totalMissedEver(), 2u);

    // Repair one miss inside the repair window.
    advance(std::chrono::seconds(30));
    t.recordOurValidation(makeHash(4), 4);
    t.reconcile();

    // The net pair moves with the repair, the gross pair does not.
    EXPECT_EQ(t.totalAgreements(), 4u);
    EXPECT_EQ(t.totalMissed(), 1u);
    EXPECT_EQ(t.totalAgreementsEver(), 3u);
    EXPECT_EQ(t.totalMissedEver(), 2u);

    // Every decided ledger is counted once across the two gross tallies.
    EXPECT_EQ(t.totalAgreementsEver() + t.totalMissedEver(), 5u);
}

TEST(ValidationTracker, a_repair_never_decrements_the_gross_missed_total)
{
    // One miss, then its other half arrives. A Prometheus counter is fed from
    // the gross tally, so that tally must not go down here.
    auto t = makeTracker();
    t.recordNetworkValidation(makeHash(10), 1000);
    settle(t);

    EXPECT_EQ(t.totalMissed(), 1u);
    EXPECT_EQ(t.totalMissedEver(), 1u);
    EXPECT_EQ(t.totalAgreements(), 0u);
    EXPECT_EQ(t.totalAgreementsEver(), 0u);

    advance(std::chrono::seconds(30));
    t.recordOurValidation(makeHash(10), 1000);
    t.reconcile();

    EXPECT_EQ(t.totalMissed(), 0u);
    EXPECT_EQ(t.totalAgreements(), 1u);
    // Frozen at first classification: the miss stays, no agreement is added.
    EXPECT_EQ(t.totalMissedEver(), 1u);
    EXPECT_EQ(t.totalAgreementsEver(), 0u);
}

TEST(ValidationTracker, late_repair_turns_a_miss_into_an_agreement)
{
    auto t = makeTracker();
    t.recordNetworkValidation(makeHash(6), 6);
    settle(t);
    EXPECT_EQ(t.missed1h(), 1u);
    EXPECT_EQ(t.agreements1h(), 0u);

    // Our own validation shows up inside the repair window.
    advance(std::chrono::seconds(30));
    t.recordOurValidation(makeHash(6), 6);
    t.reconcile();

    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.agreements1h(), 1u);
    EXPECT_DOUBLE_EQ(t.agreementPct1h(), 100.0);
}

TEST(ValidationTracker, late_repair_works_across_a_bucket_boundary)
{
    auto t = makeTracker();
    t.recordNetworkValidation(makeHash(7), 7);
    settle(t);
    EXPECT_EQ(t.missed1h(), 1u);

    // Cross into a later minute bucket before repairing, so the repair has to
    // find the bucket the event was recorded in rather than the current one.
    advance(std::chrono::seconds(120));
    t.recordOurValidation(makeHash(7), 7);
    t.reconcile();

    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.agreements1h(), 1u);
    EXPECT_EQ(t.agreements7d(), 1u);
    EXPECT_EQ(t.missed7d(), 0u);
}

TEST(ValidationTracker, repair_after_the_window_closes_is_ignored)
{
    auto t = makeTracker();
    t.recordNetworkValidation(makeHash(8), 8);
    settle(t);
    EXPECT_EQ(t.missed1h(), 1u);

    // Past the repair window, so the miss stands.
    advance(Tracker::lateRepairWindow() + std::chrono::seconds(10));
    t.recordOurValidation(makeHash(8), 8);
    t.reconcile();

    EXPECT_EQ(t.missed1h(), 1u);
    EXPECT_EQ(t.agreements1h(), 0u);
}

// ---- window expiry ---------------------------------------------------------

TEST(ValidationTracker, an_event_leaves_the_1h_window_but_stays_in_the_others)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(9), 9);
    t.recordNetworkValidation(makeHash(9), 9);
    settle(t);
    EXPECT_EQ(t.agreements1h(), 1u);

    advance(std::chrono::hours(1) + std::chrono::minutes(2));
    t.reconcile();

    EXPECT_EQ(t.agreements1h(), 0u);
    EXPECT_EQ(t.agreements24h(), 1u);
    EXPECT_EQ(t.agreements7d(), 1u);
    EXPECT_DOUBLE_EQ(t.agreementPct1h(), 0.0);
    EXPECT_DOUBLE_EQ(t.agreementPct24h(), 100.0);
}

TEST(ValidationTracker, an_event_leaves_the_24h_window_but_stays_in_7d)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(10), 10);
    t.recordNetworkValidation(makeHash(10), 10);
    settle(t);

    advance(std::chrono::hours(24) + std::chrono::minutes(2));
    t.reconcile();

    EXPECT_EQ(t.agreements1h(), 0u);
    EXPECT_EQ(t.agreements24h(), 0u);
    EXPECT_EQ(t.agreements7d(), 1u);
}

TEST(ValidationTracker, an_event_leaves_every_window_after_7d)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(11), 11);
    t.recordNetworkValidation(makeHash(11), 11);
    settle(t);

    advance(std::chrono::hours(168) + std::chrono::minutes(2));
    t.reconcile();

    EXPECT_EQ(t.agreements7d(), 0u);
    EXPECT_EQ(t.missed7d(), 0u);
    EXPECT_DOUBLE_EQ(t.agreementPct7d(), 0.0);
}

TEST(ValidationTracker, an_event_exactly_one_window_old_has_left_the_window)
{
    // The 1h window covers 60 minute buckets. An event 60 minutes old is out;
    // 59 minutes old is still in. An off-by-one in the bound breaks one of
    // these two checks.
    auto t = makeTracker();
    t.recordOurValidation(makeHash(20), 20);
    t.recordNetworkValidation(makeHash(20), 20);
    settle(t);
    EXPECT_EQ(t.agreements1h(), 1u);

    advance(std::chrono::minutes(59) - Tracker::gracePeriod());
    t.reconcile();
    EXPECT_EQ(t.agreements1h(), 1u);

    advance(std::chrono::minutes(1));
    t.reconcile();
    EXPECT_EQ(t.agreements1h(), 0u);
    EXPECT_EQ(t.agreements24h(), 1u);
}

TEST(ValidationTracker, an_event_exactly_one_day_old_has_left_the_day_window)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(21), 21);
    t.recordNetworkValidation(makeHash(21), 21);
    settle(t);

    advance(std::chrono::minutes((24 * 60) - 1) - Tracker::gracePeriod());
    t.reconcile();
    EXPECT_EQ(t.agreements24h(), 1u);

    advance(std::chrono::minutes(1));
    t.reconcile();
    EXPECT_EQ(t.agreements24h(), 0u);
    EXPECT_EQ(t.agreements7d(), 1u);
}

TEST(ValidationTracker, a_gap_longer_than_the_7d_grid_does_not_resurrect_old_counts)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(12), 12);
    t.recordNetworkValidation(makeHash(12), 12);
    settle(t);
    EXPECT_EQ(t.agreements7d(), 1u);

    // Idle for more than one full trip around the bucket grid. Every bucket
    // the new event reuses must be zeroed, not added to.
    advance(std::chrono::hours(400));
    t.recordOurValidation(makeHash(13), 13);
    t.recordNetworkValidation(makeHash(13), 13);
    settle(t);

    EXPECT_EQ(t.agreements7d(), 1u);
    EXPECT_EQ(t.missed7d(), 0u);
    EXPECT_EQ(t.agreements1h(), 1u);
}

TEST(ValidationTracker, steady_traffic_across_the_grid_boundary_keeps_recent_counts)
{
    // Events every minute for slightly more than 14 days, so bucket slots get
    // reused while the tracker is continuously busy. The "idle longer than the
    // grid" fast path must not fire here: recent minutes are still valid data.
    //
    // Two full trips around the grid, so a slot that was reused is later
    // retired. One trip only proves reuse happened; the miscount from a bucket
    // that was not cleared shows up when that bucket is subtracted.
    auto t = makeTracker();

    constexpr std::uint64_t kMinutes = (2 * 7 * 24 * 60) + 5;
    for (std::uint64_t i = 0; i < kMinutes; ++i)
    {
        t.recordOurValidation(makeHash(i), static_cast<LedgerIndex>(i));
        t.recordNetworkValidation(makeHash(i), static_cast<LedgerIndex>(i));
        advance(std::chrono::seconds(9));
        t.reconcile();
        advance(std::chrono::seconds(51));
    }

    // One event per minute means each window holds exactly its own span, so
    // these are exact, not "roughly". A bucket reused without being cleared
    // would push them above the span.
    EXPECT_EQ(t.agreements1h(), 60u);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.agreements24h(), 24u * 60u);
    EXPECT_EQ(t.agreements7d(), 7u * 24u * 60u);
    EXPECT_EQ(t.missed7d(), 0u);
}

// ---- lifetime totals -------------------------------------------------------

TEST(ValidationTracker, lifetime_totals_survive_window_expiry)
{
    auto t = makeTracker();
    t.recordOurValidation(makeHash(14), 14);
    t.recordNetworkValidation(makeHash(14), 14);
    t.recordOurValidation(makeHash(15), 15);
    settle(t);
    EXPECT_EQ(t.totalAgreements(), 1u);
    EXPECT_EQ(t.totalMissed(), 1u);

    advance(std::chrono::hours(200));
    t.reconcile();

    EXPECT_EQ(t.agreements7d(), 0u);
    EXPECT_EQ(t.totalAgreements(), 1u);
    EXPECT_EQ(t.totalMissed(), 1u);
}

TEST(ValidationTracker, a_repair_moves_a_lifetime_total_from_missed_to_agreed)
{
    auto t = makeTracker();
    t.recordNetworkValidation(makeHash(16), 16);
    settle(t);
    EXPECT_EQ(t.totalMissed(), 1u);
    EXPECT_EQ(t.totalAgreements(), 0u);

    advance(std::chrono::seconds(20));
    t.recordOurValidation(makeHash(16), 16);
    t.reconcile();

    EXPECT_EQ(t.totalMissed(), 0u);
    EXPECT_EQ(t.totalAgreements(), 1u);
}

// ---- rings -----------------------------------------------------------------

TEST(ValidationTracker, an_empty_tracker_reports_zero_everywhere)
{
    auto t = makeTracker();
    EXPECT_EQ(t.agreements1h(), 0u);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.agreements24h(), 0u);
    EXPECT_EQ(t.missed24h(), 0u);
    EXPECT_EQ(t.agreements7d(), 0u);
    EXPECT_EQ(t.missed7d(), 0u);
    EXPECT_DOUBLE_EQ(t.agreementPct1h(), 0.0);
    EXPECT_DOUBLE_EQ(t.agreementPct24h(), 0.0);
    EXPECT_DOUBLE_EQ(t.agreementPct7d(), 0.0);
    EXPECT_EQ(t.totalAgreements(), 0u);
    EXPECT_EQ(t.totalMissed(), 0u);
    EXPECT_EQ(t.totalValidationsSent(), 0u);
    EXPECT_EQ(t.totalValidationsChecked(), 0u);
    EXPECT_EQ(t.droppedEvents(), 0u);
}

TEST(ValidationTracker, a_full_ring_drops_and_counts_instead_of_blocking)
{
    auto t = makeTracker();

    // One more than the ring holds, with no drain in between.
    for (std::size_t i = 0; i < Tracker::ringCapacity() + 1; ++i)
        t.recordOurValidation(makeHash(1000 + i), static_cast<LedgerIndex>(1000 + i));

    EXPECT_EQ(t.droppedEvents(), 1u);

    settle(t);
    EXPECT_EQ(t.missed1h(), Tracker::ringCapacity());
}

TEST(ValidationTracker, a_drop_on_one_ring_leaves_the_other_alone)
{
    auto t = makeTracker();
    for (std::size_t i = 0; i < Tracker::ringCapacity() + 4; ++i)
        t.recordOurValidation(makeHash(2000 + i), static_cast<LedgerIndex>(2000 + i));
    EXPECT_EQ(t.droppedEvents(), 4u);

    t.recordNetworkValidation(makeHash(3000), 3000);
    settle(t);

    // The network ring was never full, so its event still missed normally:
    // capacity misses from our ring, plus this one.
    EXPECT_EQ(t.missed1h(), Tracker::ringCapacity() + 1);
    EXPECT_EQ(t.droppedEvents(), 4u);
}

TEST(ValidationTracker, draining_frees_ring_space_again)
{
    auto t = makeTracker();
    for (std::size_t i = 0; i < Tracker::ringCapacity(); ++i)
        t.recordOurValidation(makeHash(4000 + i), static_cast<LedgerIndex>(4000 + i));
    EXPECT_EQ(t.droppedEvents(), 0u);

    t.reconcile();  // drains, decides nothing yet

    for (std::size_t i = 0; i < Tracker::ringCapacity(); ++i)
        t.recordOurValidation(makeHash(5000 + i), static_cast<LedgerIndex>(5000 + i));

    EXPECT_EQ(t.droppedEvents(), 0u);
}

TEST(ValidationTracker, a_burst_larger_than_one_ring_is_counted_in_full_when_drained)
{
    // Pending events are bounded by the repair window, not by a count, so a
    // burst several ring-loads long is counted in full as long as the reducer
    // runs between loads.
    auto t = makeTracker();

    constexpr std::size_t kBatches = 4;
    auto const perBatch = Tracker::ringCapacity();

    for (std::size_t b = 0; b < kBatches; ++b)
    {
        for (std::size_t i = 0; i < perBatch; ++i)
        {
            auto const n = (b * perBatch) + i + 1;
            t.recordOurValidation(makeHash(n), static_cast<LedgerIndex>(n));
            t.recordNetworkValidation(makeHash(n), static_cast<LedgerIndex>(n));
        }
        t.reconcile();
    }

    settle(t);

    auto const expected = kBatches * perBatch;
    EXPECT_EQ(t.droppedEvents(), 0u);
    EXPECT_EQ(t.agreements1h(), expected);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.totalAgreements(), expected);
    EXPECT_EQ(t.totalValidationsSent(), expected);
    EXPECT_EQ(t.totalValidationsChecked(), expected);
}

// ---- concurrency -----------------------------------------------------------

TEST(ValidationTracker, two_producers_and_a_reducer_lose_nothing)
{
    // Real threads on the real code path. Each producer stays under the ring
    // capacity between drains, so no drop is expected and every pair must be
    // counted exactly once.
    gNow = Tracker::TimePoint{} + std::chrono::hours(1000);
    Tracker t(&testNow);

    constexpr std::uint64_t kLedgers = 64;

    std::thread ours([&t] {
        for (std::uint64_t i = 0; i < kLedgers; ++i)
            t.recordOurValidation(makeHash(6000 + i), static_cast<LedgerIndex>(6000 + i));
    });
    std::thread theirs([&t] {
        for (std::uint64_t i = 0; i < kLedgers; ++i)
            t.recordNetworkValidation(makeHash(6000 + i), static_cast<LedgerIndex>(6000 + i));
    });
    std::thread reader([&t] {
        for (int i = 0; i < 200; ++i)
        {
            t.reconcile();
            static_cast<void>(t.agreements1h());
        }
    });

    ours.join();
    theirs.join();
    reader.join();

    settle(t);
    EXPECT_EQ(t.droppedEvents(), 0u);
    EXPECT_EQ(t.agreements1h(), kLedgers);
    EXPECT_EQ(t.missed1h(), 0u);
}

TEST(ValidationTracker, a_reader_runs_alongside_the_reducer_without_racing_it)
{
    // The production shape: one thread reconciles while another only reads the
    // gauges. Every event is still counted once, and a reader never stops the
    // reducer from finishing a cycle.
    gNow = Tracker::TimePoint{} + std::chrono::hours(3000);
    Tracker t(&testNow);

    constexpr std::uint64_t kLedgers = 500;
    std::atomic<bool> stop{false};

    std::thread reader([&t, &stop] {
        std::uint64_t seen = 0;
        while (!stop.load(std::memory_order_relaxed))
            seen += t.agreements1h() + t.missed1h() + t.agreements7d();
        static_cast<void>(seen);
    });

    for (std::uint64_t i = 0; i < kLedgers; ++i)
    {
        t.recordOurValidation(makeHash(8000 + i), static_cast<LedgerIndex>(8000 + i));
        t.recordNetworkValidation(makeHash(8000 + i), static_cast<LedgerIndex>(8000 + i));
        advance(std::chrono::seconds(9));
        t.reconcile();
    }

    stop.store(true, std::memory_order_relaxed);
    reader.join();

    EXPECT_EQ(t.droppedEvents(), 0u);
    EXPECT_EQ(t.totalAgreements(), kLedgers);
    EXPECT_EQ(t.totalMissed(), 0u);
    EXPECT_EQ(t.missed1h(), 0u);
}

TEST(ValidationTracker, concurrent_reducer_entry_does_not_deadlock_or_double_count)
{
    gNow = Tracker::TimePoint{} + std::chrono::hours(2000);
    Tracker t(&testNow);

    for (std::uint64_t i = 0; i < 32; ++i)
    {
        t.recordOurValidation(makeHash(7000 + i), static_cast<LedgerIndex>(7000 + i));
        t.recordNetworkValidation(makeHash(7000 + i), static_cast<LedgerIndex>(7000 + i));
    }
    advance(Tracker::gracePeriod() + std::chrono::seconds(1));

    std::vector<std::thread> readers;
    readers.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        readers.emplace_back([&t] {
            for (int j = 0; j < 500; ++j)
                t.reconcile();
        });
    }
    for (auto& r : readers)
        r.join();

    EXPECT_EQ(t.agreements1h(), 32u);
    EXPECT_EQ(t.missed1h(), 0u);
    EXPECT_EQ(t.totalAgreements(), 32u);
}
