#pragma once

/** @file ValidationTracker.h
    Standalone validation agreement tracker for telemetry.
*/

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>

namespace xrpl {
namespace telemetry {

/**
 * Tracks whether this validator's validations agree with network consensus,
 * maintaining rolling 1-hour and 24-hour windows plus lifetime totals.
 *
 * The tracker operates by recording two independent events per ledger:
 *   1. "We validated" -- our node published a validation for a ledger hash.
 *   2. "Network validated" -- the network reached consensus on a ledger hash.
 *
 * After a configurable grace period (kGracePeriod), the reconcile() method
 * compares the two flags.  If both are set the ledger is counted as an
 * "agreement"; otherwise it is a "miss".  A late-repair mechanism allows a
 * miss to be upgraded to an agreement if matching evidence arrives within
 * kLateRepairWindow.
 *
 * Architecture / dependency diagram:
 * @code
 *  +--------------------------+
 *  |   ConsensusAdapter /     |
 *  |   ValidatorSite          |
 *  |   (callers)              |
 *  +---+-------------+-------+
 *      |             |
 *      | recordOur   | recordNetwork
 *      | Validation  | Validation
 *      v             v
 *  +---------------------------+
 *  |   ValidationTracker       |
 *  |---------------------------|
 *  |  pending_  (hash_map)     |----> LedgerEvent per hash
 *  |  window1h_ (deque)        |----> WindowEvent sliding window
 *  |  window24h_ (deque)       |----> WindowEvent sliding window
 *  |  atomic totals            |
 *  +---------------------------+
 *              |
 *              | reconcile() called periodically
 *              v
 *        agreement / miss counters updated
 * @endcode
 *
 * Usage -- basic recording and querying:
 * @code
 *     xrpl::telemetry::ValidationTracker tracker;
 *
 *     // On local validation:
 *     tracker.recordOurValidation(ledgerHash, seq);
 *
 *     // On network consensus:
 *     tracker.recordNetworkValidation(ledgerHash, seq);
 *
 *     // Periodically (e.g. every few seconds):
 *     tracker.reconcile();
 *
 *     // Query agreement percentage:
 *     double pct = tracker.agreementPct1h();
 * @endcode
 *
 * Usage -- edge case with late arrival:
 * @code
 *     xrpl::telemetry::ValidationTracker tracker;
 *
 *     // Network validates first, our validation arrives late:
 *     tracker.recordNetworkValidation(hash, seq);
 *     tracker.reconcile();  // initially counted as a miss
 *
 *     // Late local validation arrives within repair window:
 *     tracker.recordOurValidation(hash, seq);
 *     tracker.reconcile();  // repaired to agreement
 * @endcode
 *
 * @note Thread-safety: all public methods are thread-safe. The pending_
 * map and sliding-window deques are protected by mutex_. Lifetime totals
 * use std::atomic for lock-free reads.
 */
class ValidationTracker
{
public:
    /// Monotonic clock used for all internal timestamps.
    using Clock = std::chrono::steady_clock;

    /// Time point type from the monotonic clock.
    using TimePoint = Clock::time_point;

    /**
     * Record that this node sent a validation for the given ledger.
     * @param ledgerHash Hash of the ledger we validated.
     * @param seq        Ledger sequence number.
     */
    void
    recordOurValidation(uint256 const& ledgerHash, LedgerIndex seq);

    /**
     * Record that the network reached consensus on the given ledger.
     * @param ledgerHash Hash of the network-validated ledger.
     * @param seq        Ledger sequence number.
     */
    void
    recordNetworkValidation(uint256 const& ledgerHash, LedgerIndex seq);

    /**
     * Reconcile pending ledger events whose grace period has elapsed.
     * Should be called periodically (e.g. every few seconds). Moves
     * reconciled events into the sliding windows and updates totals.
     * Also performs late-repair and eviction of stale data.
     */
    void
    reconcile();

    /** @name Rolling-window percentage getters */
    /** @{ */

    /** Agreement percentage over the last 1 hour.
     *  @return Percentage [0.0, 100.0], or 0.0 if no data.
     */
    double
    agreementPct1h() const;

    /** Agreement percentage over the last 24 hours.
     *  @return Percentage [0.0, 100.0], or 0.0 if no data.
     */
    double
    agreementPct24h() const;

    /** @} */

    /** @name Rolling-window count getters */
    /** @{ */

    /** Number of agreements in the 1-hour window. */
    uint64_t
    agreements1h() const;

    /** Number of misses in the 1-hour window. */
    uint64_t
    missed1h() const;

    /** Number of agreements in the 24-hour window. */
    uint64_t
    agreements24h() const;

    /** Number of misses in the 24-hour window. */
    uint64_t
    missed24h() const;

    /** @} */

    /** @name Lifetime totals (atomic, lock-free reads) */
    /** @{ */

    /** Total agreements since process start. */
    uint64_t
    totalAgreements() const;

    /** Total misses since process start. */
    uint64_t
    totalMissed() const;

    /** Total validations this node sent. */
    uint64_t
    totalValidationsSent() const;

    /** Total network validations observed for comparison. */
    uint64_t
    totalValidationsChecked() const;

    /** @} */

private:
    /**
     * Per-ledger tracking state held in the pending map.
     */
    struct LedgerEvent
    {
        uint256 ledgerHash;             ///< Ledger hash being tracked.
        LedgerIndex seq;                ///< Ledger sequence number.
        TimePoint recordTime;           ///< Time the event was first recorded.
        bool weValidated = false;       ///< True if we sent a validation.
        bool networkValidated = false;  ///< True if network reached consensus.
        bool reconciled = false;        ///< True once grace period elapsed.
        bool agreed = false;            ///< True if both flags set at reconcile.
    };

    /**
     * Lightweight event stored in the sliding-window deques.
     */
    struct WindowEvent
    {
        TimePoint time;      ///< When the event was reconciled.
        uint256 ledgerHash;  ///< Ledger hash for late-repair matching.
        bool agreed;         ///< Whether this was an agreement.
    };

    /// Grace period before reconciling a ledger event.
    static constexpr auto kGracePeriod = std::chrono::seconds(8);

    /// Window during which a missed event can be repaired.
    static constexpr auto kLateRepairWindow = std::chrono::minutes(5);

    /// Maximum number of pending (unreconciled + recently reconciled) events.
    static constexpr std::size_t kMaxPendingEvents = 1000;

    /// Duration of the short rolling window.
    static constexpr auto kWindow1h = std::chrono::hours(1);

    /// Duration of the long rolling window.
    static constexpr auto kWindow24h = std::chrono::hours(24);

    /// Protects pending_, window1h_, and window24h_.
    mutable std::mutex mutex_;

    /// Pending ledger events indexed by ledger hash.
    hash_map<uint256, LedgerEvent> pending_;

    /// Sliding window of reconciled events (last 1 hour).
    std::deque<WindowEvent> window1h_;

    /// Sliding window of reconciled events (last 24 hours).
    std::deque<WindowEvent> window24h_;

    /// Lifetime count of agreements.
    std::atomic<uint64_t> totalAgreements_{0};

    /// Lifetime count of misses.
    std::atomic<uint64_t> totalMissed_{0};

    /// Lifetime count of validations this node sent.
    std::atomic<uint64_t> totalValidationsSent_{0};

    /// Lifetime count of network validations observed.
    std::atomic<uint64_t> totalValidationsChecked_{0};

    /**
     * Remove entries older than their respective window durations.
     * @param now Current time point.
     */
    void
    evictStaleWindows(TimePoint now);

    /**
     * Remove reconciled pending entries older than the late-repair window.
     * Also trims the map if it exceeds kMaxPendingEvents.
     * @param now Current time point.
     */
    void
    evictOldPending(TimePoint now);

    /**
     * Scan a window deque and flip the first non-agreed entry matching
     * the given ledger hash to agreed.
     * @param window  The sliding-window deque to repair.
     * @param hash    Ledger hash to match.
     */
    static void
    repairWindowEntry(std::deque<WindowEvent>& window, uint256 const& hash);
};

}  // namespace telemetry
}  // namespace xrpl
