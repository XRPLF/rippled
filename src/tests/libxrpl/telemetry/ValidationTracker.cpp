/** @file ValidationTracker.cpp
    Unit tests for xrpl::telemetry::ValidationTracker.
*/

#include <xrpld/telemetry/ValidationTracker.h>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace xrpl;
using namespace xrpl::telemetry;

/// Helper to create a unique uint256 from an integer seed.
static uint256
makeHash(std::uint64_t n)
{
    return uint256(n);
}

/// Test fixture providing a fresh ValidationTracker per test.
class ValidationTrackerTest : public ::testing::Test
{
protected:
    ValidationTracker tracker_;
};

// ---------------------------------------------------------------
// 1. Normal agreement
//    Record both our validation and network validation for the
//    same hash, then reconcile after the grace period elapses.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, NormalAgreement)
{
    auto const hash = makeHash(1);
    LedgerIndex const seq = 100;

    tracker_.recordOurValidation(hash, seq);
    tracker_.recordNetworkValidation(hash, seq);

    // Immediately after recording, nothing is reconciled yet
    // (grace period has not elapsed).
    tracker_.reconcile();
    EXPECT_EQ(tracker_.totalValidationsSent(), 1u);
    EXPECT_EQ(tracker_.totalValidationsChecked(), 1u);

    // Wait for the grace period (8 seconds) to elapse, then reconcile.
    std::this_thread::sleep_for(std::chrono::seconds(9));
    tracker_.reconcile();

    EXPECT_EQ(tracker_.totalAgreements(), 1u);
    EXPECT_EQ(tracker_.totalMissed(), 0u);
    EXPECT_EQ(tracker_.agreements1h(), 1u);
    EXPECT_EQ(tracker_.missed1h(), 0u);
    EXPECT_DOUBLE_EQ(tracker_.agreementPct1h(), 100.0);
}

// ---------------------------------------------------------------
// 2. Missed validation
//    Only the network validates; we never do. After grace period
//    the event should be reconciled as a miss.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, MissedValidation)
{
    auto const hash = makeHash(2);
    LedgerIndex const seq = 200;

    tracker_.recordNetworkValidation(hash, seq);

    // Wait for grace period then reconcile.
    std::this_thread::sleep_for(std::chrono::seconds(9));
    tracker_.reconcile();

    EXPECT_EQ(tracker_.totalAgreements(), 0u);
    EXPECT_EQ(tracker_.totalMissed(), 1u);
    EXPECT_EQ(tracker_.agreements1h(), 0u);
    EXPECT_EQ(tracker_.missed1h(), 1u);
    EXPECT_DOUBLE_EQ(tracker_.agreementPct1h(), 0.0);
}

// ---------------------------------------------------------------
// 3. Late repair
//    Network validates first, grace period elapses (miss), then
//    our validation arrives within the 5-minute repair window and
//    the miss is flipped to an agreement.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, LateRepair)
{
    auto const hash = makeHash(3);
    LedgerIndex const seq = 300;

    // Network validates, but we do not (yet).
    tracker_.recordNetworkValidation(hash, seq);

    // Grace period elapses -- reconciled as a miss.
    std::this_thread::sleep_for(std::chrono::seconds(9));
    tracker_.reconcile();
    EXPECT_EQ(tracker_.totalMissed(), 1u);
    EXPECT_EQ(tracker_.totalAgreements(), 0u);
    EXPECT_EQ(tracker_.missed1h(), 1u);

    // Late arrival of our validation (within repair window).
    tracker_.recordOurValidation(hash, seq);
    tracker_.reconcile();

    // Miss should be repaired to agreement.
    EXPECT_EQ(tracker_.totalAgreements(), 1u);
    EXPECT_EQ(tracker_.totalMissed(), 0u);
    EXPECT_EQ(tracker_.agreements1h(), 1u);
    EXPECT_EQ(tracker_.missed1h(), 0u);
    EXPECT_DOUBLE_EQ(tracker_.agreementPct1h(), 100.0);
}

// ---------------------------------------------------------------
// 4. Empty window returns 0%
//    When no events have been recorded the percentage methods
//    must return 0.0, not NaN or any other value.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, EmptyWindowReturnsZero)
{
    EXPECT_DOUBLE_EQ(tracker_.agreementPct1h(), 0.0);
    EXPECT_DOUBLE_EQ(tracker_.agreementPct24h(), 0.0);
    EXPECT_EQ(tracker_.agreements1h(), 0u);
    EXPECT_EQ(tracker_.missed1h(), 0u);
    EXPECT_EQ(tracker_.agreements24h(), 0u);
    EXPECT_EQ(tracker_.missed24h(), 0u);
    EXPECT_EQ(tracker_.totalAgreements(), 0u);
    EXPECT_EQ(tracker_.totalMissed(), 0u);
    EXPECT_EQ(tracker_.totalValidationsSent(), 0u);
    EXPECT_EQ(tracker_.totalValidationsChecked(), 0u);
}

// ---------------------------------------------------------------
// 5. Grace period boundary
//    Events recorded less than 8 seconds ago must NOT be
//    reconciled. Verify that an immediate reconcile is a no-op.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, GracePeriodBoundary)
{
    auto const hash = makeHash(5);
    LedgerIndex const seq = 500;

    tracker_.recordOurValidation(hash, seq);
    tracker_.recordNetworkValidation(hash, seq);

    // Reconcile immediately -- grace period has not elapsed.
    tracker_.reconcile();

    // Nothing should be reconciled yet.
    EXPECT_EQ(tracker_.totalAgreements(), 0u);
    EXPECT_EQ(tracker_.totalMissed(), 0u);
    EXPECT_EQ(tracker_.agreements1h(), 0u);
    EXPECT_EQ(tracker_.missed1h(), 0u);

    // Lifetime send/check counters should still be incremented.
    EXPECT_EQ(tracker_.totalValidationsSent(), 1u);
    EXPECT_EQ(tracker_.totalValidationsChecked(), 1u);
}

// ---------------------------------------------------------------
// 6. Max pending events -- trimming
//    Add more than kMaxPendingEvents (1000) events. After
//    reconciliation and a second reconcile pass the pending map
//    should be trimmed. Lifetime totals must remain consistent.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, MaxPendingEventsTrimming)
{
    constexpr std::size_t kCount = 1100;

    for (std::size_t i = 0; i < kCount; ++i)
    {
        auto const hash = makeHash(i + 1);
        LedgerIndex const seq = static_cast<LedgerIndex>(i + 1);
        tracker_.recordOurValidation(hash, seq);
        tracker_.recordNetworkValidation(hash, seq);
    }

    EXPECT_EQ(tracker_.totalValidationsSent(), kCount);
    EXPECT_EQ(tracker_.totalValidationsChecked(), kCount);

    // Wait for grace period so all events can be reconciled.
    std::this_thread::sleep_for(std::chrono::seconds(9));
    tracker_.reconcile();

    // All events should be reconciled as agreements.
    EXPECT_EQ(tracker_.totalAgreements(), kCount);
    EXPECT_EQ(tracker_.totalMissed(), 0u);

    // Reconcile again to trigger pending eviction / trimming.
    // The pending map should be trimmed, but totals remain correct.
    tracker_.reconcile();
    EXPECT_EQ(tracker_.totalAgreements(), kCount);
    EXPECT_EQ(tracker_.totalMissed(), 0u);
}

// ---------------------------------------------------------------
// 7. Multiple distinct ledgers -- mixed results
//    Record a mix of agreements and misses to verify that window
//    counts and percentages are computed correctly.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, MixedAgreementsAndMisses)
{
    // 3 agreements: both sides validate.
    for (int i = 1; i <= 3; ++i)
    {
        auto const hash = makeHash(static_cast<std::uint64_t>(i));
        tracker_.recordOurValidation(hash, static_cast<LedgerIndex>(i));
        tracker_.recordNetworkValidation(hash, static_cast<LedgerIndex>(i));
    }

    // 2 misses: only network validates.
    for (int i = 4; i <= 5; ++i)
    {
        auto const hash = makeHash(static_cast<std::uint64_t>(i));
        tracker_.recordNetworkValidation(hash, static_cast<LedgerIndex>(i));
    }

    // Wait for grace period then reconcile.
    std::this_thread::sleep_for(std::chrono::seconds(9));
    tracker_.reconcile();

    EXPECT_EQ(tracker_.totalAgreements(), 3u);
    EXPECT_EQ(tracker_.totalMissed(), 2u);
    EXPECT_EQ(tracker_.agreements1h(), 3u);
    EXPECT_EQ(tracker_.missed1h(), 2u);

    // 3 out of 5 = 60%
    EXPECT_DOUBLE_EQ(tracker_.agreementPct1h(), 60.0);
}

// ---------------------------------------------------------------
// 8. Duplicate recording for same hash
//    Recording the same hash multiple times should not create
//    duplicate pending entries or double-count totals beyond the
//    per-call increments.
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, DuplicateRecordingSameHash)
{
    auto const hash = makeHash(42);
    LedgerIndex const seq = 42;

    // Record our validation twice for the same hash.
    tracker_.recordOurValidation(hash, seq);
    tracker_.recordOurValidation(hash, seq);
    tracker_.recordNetworkValidation(hash, seq);

    // Each call increments the lifetime counter.
    EXPECT_EQ(tracker_.totalValidationsSent(), 2u);
    EXPECT_EQ(tracker_.totalValidationsChecked(), 1u);

    // But only one pending event exists, so only one agreement.
    std::this_thread::sleep_for(std::chrono::seconds(9));
    tracker_.reconcile();

    EXPECT_EQ(tracker_.totalAgreements(), 1u);
    EXPECT_EQ(tracker_.totalMissed(), 0u);
}

// ---------------------------------------------------------------
// 9. Only-we-validated scenario
//    We validate but the network does not. After grace period
//    this should be a miss (not an agreement).
// ---------------------------------------------------------------
TEST_F(ValidationTrackerTest, OnlyWeValidated)
{
    auto const hash = makeHash(99);
    LedgerIndex const seq = 99;

    tracker_.recordOurValidation(hash, seq);

    std::this_thread::sleep_for(std::chrono::seconds(9));
    tracker_.reconcile();

    EXPECT_EQ(tracker_.totalAgreements(), 0u);
    EXPECT_EQ(tracker_.totalMissed(), 1u);
    EXPECT_EQ(tracker_.missed1h(), 1u);
    EXPECT_DOUBLE_EQ(tracker_.agreementPct1h(), 0.0);
}
