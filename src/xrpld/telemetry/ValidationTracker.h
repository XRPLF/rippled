#pragma once

/**
 * @file ValidationTracker.h
 * Standalone validation agreement tracker for telemetry.
 */

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace xrpl::telemetry {

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
    /**
     * Monotonic clock used for all internal timestamps.
     */
    using Clock = std::chrono::steady_clock;

    /**
     * Time point type from the monotonic clock.
     */
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

    /**
     * @name Rolling-window percentage getters
     */
    /** @{ */

    /**
     * Agreement percentage over the last 1 hour.
     * @return Percentage [0.0, 100.0], or 0.0 if no data.
     */
    [[nodiscard]] double
    agreementPct1h() const;

    /**
     * Agreement percentage over the last 24 hours.
     * @return Percentage [0.0, 100.0], or 0.0 if no data.
     */
    [[nodiscard]] double
    agreementPct24h() const;

    /**
     * Agreement percentage over the last 7 days.
     * @return Percentage [0.0, 100.0], or 0.0 if no data.
     */
    [[nodiscard]] double
    agreementPct7d() const;

    /** @} */

    /**
     * @name Rolling-window count getters
     */
    /** @{ */

    /**
     * Number of agreements in the 1-hour window.
     */
    [[nodiscard]] uint64_t
    agreements1h() const;

    /**
     * Number of misses in the 1-hour window.
     */
    [[nodiscard]] uint64_t
    missed1h() const;

    /**
     * Number of agreements in the 24-hour window.
     */
    [[nodiscard]] uint64_t
    agreements24h() const;

    /**
     * Number of misses in the 24-hour window.
     */
    [[nodiscard]] uint64_t
    missed24h() const;

    /**
     * Number of agreements in the 7-day window.
     */
    [[nodiscard]] uint64_t
    agreements7d() const;

    /**
     * Number of misses in the 7-day window.
     */
    [[nodiscard]] uint64_t
    missed7d() const;

    /** @} */

    /**
     * @name Lifetime totals (atomic, lock-free reads)
     */
    /** @{ */

    /**
     * Total agreements since process start.
     */
    [[nodiscard]] uint64_t
    totalAgreements() const;

    /**
     * Total misses since process start.
     */
    [[nodiscard]] uint64_t
    totalMissed() const;

    /**
     * Lifetime agreements counted at first classification only.
     *
     * @note Unlike totalAgreements(), this is strictly monotonic: it is
     * incremented only when a ledger is first reconciled as an agreement and
     * is never adjusted by a late repair. It backs the monotonic Prometheus
     * counter validation_agreements_total. See the counting-semantics
     * note in detail/ValidationTracker.cpp.
     */
    [[nodiscard]] uint64_t
    totalAgreementsEver() const;

    /**
     * Lifetime misses counted at first classification only.
     *
     * @note Unlike totalMissed(), this is strictly monotonic: it is
     * incremented only when a ledger is first reconciled as a miss and is
     * never decremented by a late repair. It backs the monotonic Prometheus
     * counter validation_missed_total. See the counting-semantics note
     * in detail/ValidationTracker.cpp.
     */
    [[nodiscard]] uint64_t
    totalMissedEver() const;

    /**
     * Total validations this node sent.
     */
    [[nodiscard]] uint64_t
    totalValidationsSent() const;

    /**
     * Total network validations observed for comparison.
     */
    [[nodiscard]] uint64_t
    totalValidationsChecked() const;

    /**
     * Number of ledgers currently held awaiting reconciliation.
     *
     * Never exceeds kMaxPendingEvents: the record methods enforce that bound
     * as they insert, so the map stays bounded whether or not anything ever
     * reconciles or reads it.
     * @return Size of the pending map.
     */
    [[nodiscard]] std::size_t
    pendingCount() const;

    /** @} */

private:
    /**
     * Per-ledger tracking state held in the pending map.
     */
    struct LedgerEvent
    {
        uint256 ledgerHash;             ///< Ledger hash being tracked.
        LedgerIndex seq{0};             ///< Ledger sequence number.
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
        bool agreed{false};  ///< Whether this was an agreement.
    };

    /**
     * Grace period before reconciling a ledger event.
     */
    static constexpr auto kGracePeriod = std::chrono::seconds(8);

    /**
     * Window during which a missed event can be repaired.
     */
    static constexpr auto kLateRepairWindow = std::chrono::minutes(5);

    /**
     * Maximum number of pending (unreconciled + recently reconciled) events.
     */
    static constexpr std::size_t kMaxPendingEvents = 1000;

    /**
     * Duration of the short rolling window.
     */
    static constexpr auto kWindow1h = std::chrono::hours(1);

    /**
     * Duration of the long rolling window.
     */
    static constexpr auto kWindow24h = std::chrono::hours(24);

    /**
     * Duration of the extended rolling window (7 days).
     */
    static constexpr auto kWindow7d = std::chrono::hours(168);

    /**
     * Protects pending_, window1h_, window24h_, and window7d_.
     */
    mutable std::mutex mutex_;

    /**
     * Pending ledger events indexed by ledger hash.
     */
    hash_map<uint256, LedgerEvent> pending_;

    /**
     * Sliding window of reconciled events (last 1 hour).
     */
    std::deque<WindowEvent> window1h_;

    /**
     * Sliding window of reconciled events (last 24 hours).
     */
    std::deque<WindowEvent> window24h_;

    /**
     * Sliding window of reconciled events (last 7 days).
     */
    std::deque<WindowEvent> window7d_;

    /**
     * Lifetime count of agreements (net: incremented on agree, also on
     * repair). May be read via totalAgreements(); feeds the windowed gauge.
     */
    std::atomic<uint64_t> totalAgreements_{0};

    /**
     * Lifetime count of misses (net: incremented on miss, decremented on
     * repair). NON-monotonic. May be read via totalMissed().
     */
    std::atomic<uint64_t> totalMissed_{0};

    /**
     * Monotonic "gross" lifetime tallies for the Prometheus _total counters.
     *
     * Counting decision (initial-classification only): each reconciled
     * ledger is counted exactly once, at its first classification, into
     * exactly one of the two tallies below. A later late-repair
     * (miss -> agreement) does NOT move either tally. This keeps both
     * strictly monotonic (a Prometheus _total must never decrease) and
     * additive: totalAgreementsGross_ + totalMissedGross_ == ledgers
     * reconciled. The repaired/agreement view is still available from the
     * windowed gauge (validation_agreement) and the net totals above.
     */

    /**
     * Monotonic lifetime initial agreements; backs
     * validation_agreements_total. Never adjusted on repair.
     */
    std::atomic<uint64_t> totalAgreementsGross_{0};

    /**
     * Monotonic lifetime initial misses; backs validation_missed_total.
     * Never decremented on repair.
     */
    std::atomic<uint64_t> totalMissedGross_{0};

    /**
     * Lifetime count of validations this node sent.
     */
    std::atomic<uint64_t> totalValidationsSent_{0};

    /**
     * Lifetime count of network validations observed.
     */
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
     * Hold pending_ at kMaxPendingEvents by dropping its oldest entry.
     *
     * Called on the insert path, because that is the only place the bound can
     * be guaranteed. reconcile() also prunes, but it runs only while the gauge
     * callbacks are registered, which needs telemetry both compiled in and
     * enabled -- so a node with telemetry off, or with [telemetry] enabled=0,
     * would otherwise grow this map by one entry per validated ledger forever.
     *
     * Drops the oldest entry rather than the least useful one: the map is
     * unordered, so this is a linear scan, but it runs at most once per
     * recorded validation and only once the map is already full.
     *
     * An entry that has not been classified yet is classified before it is
     * dropped, so the ledger it represents still reaches the agreement or miss
     * totals. See classifyPending() for what that costs.
     *
     * @param justRecorded Hash inserted by the caller, kept even if the scan
     *        finds it oldest (equal timestamps make that possible).
     * @note Caller must hold mutex_.
     */
    void
    boundPending(uint256 const& justRecorded);

    /**
     * Classify one pending event as an agreement or a miss, once.
     *
     * Marks the event reconciled, decides agreed from the two validation
     * flags, moves the net and gross totals, and appends the event to all
     * three sliding windows. Shared by reconcile(), which calls it once the
     * grace period has elapsed, and by boundPending(), which calls it when it
     * has to drop an entry that was never classified.
     *
     * A ledger reaches the totals exactly once, here. Classifying early -- as
     * boundPending() must -- fixes the verdict on whichever flags have arrived,
     * so a validation still in flight cannot complete or repair it.
     *
     * @param evt The pending event to classify; its reconciled and agreed
     *        fields are set.
     * @param now Timestamp recorded on the window entries.
     * @note Caller must hold mutex_.
     */
    void
    classifyPending(LedgerEvent& evt, TimePoint now);

    /**
     * Scan a window deque and flip the first non-agreed entry matching
     * the given ledger hash to agreed.
     * @param window  The sliding-window deque to repair.
     * @param hash    Ledger hash to match.
     */
    static void
    repairWindowEntry(std::deque<WindowEvent>& window, uint256 const& hash);
};

}  // namespace xrpl::telemetry
