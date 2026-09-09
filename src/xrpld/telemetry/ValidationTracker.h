#pragma once

/**
 * @file ValidationTracker.h
 * Standalone validation agreement tracker for telemetry.
 */

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Protocol.h>

#include <boost/smart_ptr/atomic_shared_ptr.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace xrpl::telemetry {

/**
 * Tracks whether this validator's validations agree with network consensus,
 * over rolling 1-hour, 24-hour and 7-day windows plus lifetime totals.
 *
 * Two independent events are recorded per ledger:
 *   1. "We validated" -- our node published a validation for a ledger hash.
 *   2. "Network validated" -- the network reached consensus on that hash.
 *
 * reconcile() compares the two flags once the grace period has passed. Both
 * flags set is an agreement, anything else is a miss. A miss becomes an
 * agreement if its other half arrives inside the late-repair window.
 *
 * The writer paths take no lock at all: each writer owns one ring, so the two
 * never touch shared state. Everything a decision needs belongs to whichever
 * thread is inside reconcile(), and a second caller returns instead of waiting.
 * Readers take a copy of the snapshot the reducer published.
 *
 * Data flow:
 * @code
 *   RCLConsensus::Adaptor::validate      LedgerMaster::setValidLedger
 *              |                                     |
 *      recordOurValidation()               recordNetworkValidation()
 *              v                                     v
 *        +------------+                       +--------------+
 *        |  ourRing_  |                       | networkRing_ |
 *        +------------+                       +--------------+
 *               \                                    /
 *                \--------- reconcile() ------------/
 *                                |
 *      pending_ --> one-minute buckets --> 1h / 24h / 7d counters
 *                                |
 *                        published_ snapshot
 *                                |
 *          agreementPct1h() / agreements24h() / missed7d() / ...
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
 *     // Periodically (e.g. every ten seconds):
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
 *     tracker.reconcile();  // counted as a miss
 *
 *     // Our validation arrives inside the repair window:
 *     tracker.recordOurValidation(hash, seq);
 *     tracker.reconcile();  // repaired to an agreement
 * @endcode
 *
 * Usage -- a test drives the clock so a window edge is reachable at once:
 * @code
 *     // A lambda with no captures converts to the function pointer NowFn wants.
 *     static xrpl::telemetry::ValidationTracker::TimePoint fakeNow{};
 *     xrpl::telemetry::ValidationTracker tracker([] { return fakeNow; });
 *
 *     tracker.recordOurValidation(hash, seq);
 *     fakeNow += xrpl::telemetry::ValidationTracker::gracePeriod();
 *     tracker.reconcile();  // decides the event without any waiting
 * @endcode
 *
 * @note Thread-safety: every public method may be called concurrently. The
 * two record methods each need a single writer thread, which is how consensus
 * and the ledger master call them. reconcile() and the getters may be called
 * from any thread and any number of threads.
 * @note reconcile() and the getters share the published snapshot through an
 * atomic shared_ptr, which every implementation guards with a short internal
 * spin. No writer path touches it, so nothing a consensus thread calls can
 * spin. Boost's is used because Apple's libc++ has no std::atomic for a
 * shared_ptr, so the std spelling does not compile there.
 * @note A writer whose ring is full discards the event and bumps
 * droppedEvents(). Counts are then low but never wrong.
 * @note Window edges are rounded to whole minutes, because counts are kept in
 * one-minute buckets.
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
     * Time source. A test supplies its own so it can reach a window edge
     * without waiting for one.
     */
    using NowFn = TimePoint (*)();

    /**
     * Construct a tracker reading time from the given source.
     * @param now Function returning the current time. Defaults to the
     * monotonic clock, so default construction works.
     */
    explicit ValidationTracker(NowFn now = &ValidationTracker::steadyNow) : now_(now)
    {
    }

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
     * Drain both rings, decide every event past the grace period, retire
     * expired buckets and publish a fresh snapshot, in that order.
     *
     * Call periodically, for example every ten seconds. Returns without doing
     * anything if another thread is already inside, so a losing caller reads
     * data at most one cycle old rather than blocking.
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
    [[nodiscard]] std::uint64_t
    agreements1h() const;

    /**
     * Number of misses in the 1-hour window.
     */
    [[nodiscard]] std::uint64_t
    missed1h() const;

    /**
     * Number of agreements in the 24-hour window.
     */
    [[nodiscard]] std::uint64_t
    agreements24h() const;

    /**
     * Number of misses in the 24-hour window.
     */
    [[nodiscard]] std::uint64_t
    missed24h() const;

    /**
     * Number of agreements in the 7-day window.
     */
    [[nodiscard]] std::uint64_t
    agreements7d() const;

    /**
     * Number of misses in the 7-day window.
     */
    [[nodiscard]] std::uint64_t
    missed7d() const;

    /** @} */

    /**
     * @name Lifetime totals (atomic, lock-free reads)
     */
    /** @{ */

    /**
     * Total agreements since process start.
     */
    [[nodiscard]] std::uint64_t
    totalAgreements() const;

    /**
     * Total misses since process start.
     */
    [[nodiscard]] std::uint64_t
    totalMissed() const;

    /**
     * Total validations this node sent.
     */
    [[nodiscard]] std::uint64_t
    totalValidationsSent() const;

    /**
     * Total network validations observed for comparison.
     */
    [[nodiscard]] std::uint64_t
    totalValidationsChecked() const;

    /**
     * Events a writer discarded because its ring was full.
     * @return Lifetime count of discards across both rings. Non-zero means
     * reconcile() is not being called often enough.
     */
    [[nodiscard]] std::uint64_t
    droppedEvents() const;

    /** @} */

    /**
     * @name Bounds, exposed so a test states the same bound as the code
     */
    /** @{ */

    /**
     * Slots in each writer's ring.
     */
    static constexpr std::size_t
    ringCapacity()
    {
        return kRingCapacity;
    }

    /**
     * Delay before an event is decided, so both sides can arrive first.
     */
    static constexpr std::chrono::seconds
    gracePeriod()
    {
        return kGracePeriod;
    }

    /**
     * How long after a decision a miss can still become an agreement.
     */
    static constexpr std::chrono::minutes
    lateRepairWindow()
    {
        return kLateRepairWindow;
    }

    /** @} */

private:
    /**
     * Slots per ring. A power of two, so the index is a mask rather than a
     * division. 128 slots is about 8.5 minutes of ledgers at one every four
     * seconds, against a normal drain gap of well under a minute.
     */
    static constexpr std::size_t kRingCapacity = 128;

    /**
     * Grace period before deciding a ledger event.
     */
    static constexpr auto kGracePeriod = std::chrono::seconds(8);

    /**
     * Window during which a missed event can be repaired.
     */
    static constexpr auto kLateRepairWindow = std::chrono::minutes(5);

    /**
     * One-minute buckets spanned by the short window.
     */
    static constexpr std::size_t kBuckets1h = 60;

    /**
     * One-minute buckets spanned by the long window.
     */
    static constexpr std::size_t kBuckets24h = 24 * 60;

    /**
     * One-minute buckets spanned by the extended window. Also the length of
     * the bucket grid, so a slot is reused exactly seven days later.
     */
    static constexpr std::size_t kBuckets7d = 7 * 24 * 60;

    /**
     * Maximum number of ledger hashes remembered as already counted.
     * At one ledger every four seconds this spans about eleven hours.
     * A validation arriving for a ledger counted before that is counted
     * again.
     */
    static constexpr std::size_t kMaxTalliedEvents = 10000;

    /**
     * Default time source.
     * @return The monotonic clock's current time point.
     */
    static TimePoint
    steadyNow();

    /**
     * One recorded event as it travels from a writer to the reducer.
     */
    struct Slot
    {
        uint256 hash;  ///< Ledger hash being reported.

        /**
         * Ledger sequence number as the caller gave it. Carried for
         * diagnostics: the counters key on the hash, not the sequence.
         */
        LedgerIndex seq{0};

        TimePoint at;  ///< When the writer recorded it.
    };

    /**
     * Single-producer, single-consumer ring of recorded events.
     *
     * The producer only advances head_ and the consumer only advances tail_,
     * so a release store on one side and an acquire load on the other is
     * enough: no compare-exchange, no retry loop, nothing to block on.
     *
     * @code
     *     Ring r;
     *     if (!r.push(hash, seq, now))
     *         ;  // full, the caller drops the event
     *     r.drain([](Slot const& s) { use(s); });
     * @endcode
     *
     * @note Exactly one thread may push and exactly one may drain. Two
     * pushers corrupt the ring.
     */
    class Ring
    {
    public:
        /**
         * Add one event to the ring.
         * @param hash Ledger hash to record.
         * @param seq  Ledger sequence number.
         * @param at   Time the producer observed the event.
         * @return false when the ring is full, in which case nothing was
         * stored and the caller must drop the event.
         */
        [[nodiscard]] bool
        push(uint256 const& hash, LedgerIndex seq, TimePoint at)
        {
            auto const head = head_.load(std::memory_order_relaxed);
            if (head - tail_.load(std::memory_order_acquire) >= kRingCapacity)
                return false;

            slots_[head & (kRingCapacity - 1)] = Slot{.hash = hash, .seq = seq, .at = at};
            head_.store(head + 1, std::memory_order_release);
            return true;
        }

        /**
         * Hand every stored event to fn, oldest first, and free their slots.
         * @param fn Callable taking Slot const&.
         */
        template <class Fn>
        void
        drain(Fn&& fn)
        {
            auto tail = tail_.load(std::memory_order_relaxed);
            auto const head = head_.load(std::memory_order_acquire);
            for (; tail != head; ++tail)
                fn(slots_[tail & (kRingCapacity - 1)]);
            tail_.store(tail, std::memory_order_release);
        }

    private:
        /**
         * Storage, indexed by head_ or tail_ masked to the capacity.
         */
        std::array<Slot, kRingCapacity> slots_{};

        /**
         * Count of events ever pushed. Only the producer writes it.
         */
        std::atomic<std::uint64_t> head_{0};

        /**
         * Count of events ever drained. Only the consumer writes it.
         */
        std::atomic<std::uint64_t> tail_{0};
    };

    /**
     * Per-ledger tracking state held in the pending map.
     */
    struct LedgerEvent
    {
        TimePoint recordTime;          ///< Time the event was first recorded.
        std::uint64_t minute{0};       ///< Minute bucket the event belongs to.
        bool weValidated{false};       ///< True if we sent a validation.
        bool networkValidated{false};  ///< True if network reached consensus.
        bool decided{false};           ///< True once the grace period elapsed.
        bool agreed{false};            ///< True if both flags were set.
    };

    /**
     * Counts for one minute of the grid.
     */
    struct Bucket
    {
        std::uint32_t agreed{0};  ///< Agreements decided in this minute.
        std::uint32_t total{0};   ///< Events decided in this minute.
    };

    /**
     * Running counts for one rolling window.
     */
    struct WindowCount
    {
        std::uint64_t agreed{0};  ///< Agreements still inside the window.
        std::uint64_t total{0};   ///< Events still inside the window.

        /**
         * Misses still inside the window.
         * @return total minus agreed. Derived, so a repair only has to move
         * agreed.
         */
        [[nodiscard]] std::uint64_t
        missed() const
        {
            return total - agreed;
        }
    };

    /**
     * The nine numbers a reader wants, published as one value.
     */
    struct Snapshot
    {
        WindowCount w1h;   ///< 1-hour window counts.
        WindowCount w24h;  ///< 24-hour window counts.
        WindowCount w7d;   ///< 7-day window counts.
    };

    /**
     * Convert a time point to its minute on the grid.
     * @param t Time point to convert.
     * @return Whole minutes since the clock's epoch.
     */
    static std::uint64_t
    minuteOf(TimePoint t);

    /**
     * Agreement percentage for one window.
     * @param w Window counts to divide.
     * @return Percentage [0.0, 100.0], or 0.0 when the window is empty.
     */
    static double
    pct(WindowCount const& w);

    /**
     * Oldest minute a window of the given length still covers.
     * @param minute Newest minute recorded.
     * @param span   Window length in minutes.
     * @return That window's tail minute, floored at zero.
     */
    static std::uint64_t
    oldestInWindow(std::uint64_t minute, std::size_t span);

    /**
     * The snapshot readers are currently seeing.
     * @return A copy of the published snapshot, so all nine numbers come from
     * one reconcile. All zeroes before the first reconcile() publishes.
     */
    [[nodiscard]] Snapshot
    read() const;

    /**
     * Put the running counters into a fresh snapshot and publish it.
     */
    void
    publish();

    /**
     * Move both rings' contents into pending_.
     */
    void
    drainRings();

    /**
     * Fold one drained event into pending_.
     * @param s    Slot the ring handed over.
     * @param ours True if the event came from our own ring.
     */
    void
    note(Slot const& s, bool ours);

    /**
     * Decide every pending event past the grace period, repair the ones whose
     * other half arrived late, and drop entries too old to repair.
     * @param now Current time point.
     */
    void
    decidePending(TimePoint now);

    /**
     * Remember a ledger hash as counted, dropping the oldest remembered
     * hash once kMaxTalliedEvents is reached.
     * @param ledgerHash Hash of the ledger just counted into the totals.
     */
    void
    noteTallied(uint256 const& ledgerHash);

    /**
     * Count one decided event in its own bucket and in all three windows.
     * @param minute Bucket the event belongs to.
     * @param agreed True to count it as an agreement.
     */
    void
    addToWindows(std::uint64_t minute, bool agreed);

    /**
     * Turn one already-counted event from a miss into an agreement.
     * @param minute Bucket the event was counted in.
     */
    void
    repairInWindows(std::uint64_t minute);

    /**
     * Move each window's tail up to the given minute, subtracting whatever
     * leaves. The 7-day tail also clears the bucket it passes, because that
     * slot is about to be reused.
     * @param minute Newest minute to account for.
     */
    void
    advanceWindows(std::uint64_t minute);

    /**
     * Walk one window's tail forward, taking each passed bucket back out of
     * that window's running counts.
     * @param tail   The window's tail minute, advanced in place.
     * @param target Minute to stop at, the oldest the window still covers.
     * @param count  The window's running counts to subtract from.
     * @param clear  True to zero each passed bucket, which only the 7-day
     * tail does because it is the tail whose slot gets reused.
     */
    void
    retireWindow(std::uint64_t& tail, std::uint64_t target, WindowCount& count, bool clear);

    /**
     * Time source, read on every write and by the reducer.
     */
    NowFn now_;

    /**
     * Events from our own validations. Pushed by the consensus thread.
     */
    Ring ourRing_;

    /**
     * Events from network consensus. Pushed by the ledger master thread.
     */
    Ring networkRing_;

    /**
     * Set while a thread is inside reconcile(). A second caller sees it set
     * and returns rather than waiting.
     */
    std::atomic_flag reducing_;

    /**
     * Pending ledger events indexed by ledger hash. Touched only inside
     * reconcile(), so it needs no synchronisation.
     */
    hash_map<uint256, LedgerEvent> pending_;

    /**
     * Ledger hashes already counted into the agreement and missed totals.
     * Membership survives eviction from pending_, so a ledger reaches the
     * totals once. Holds at most kMaxTalliedEvents hashes.
     */
    hash_set<uint256> tallied_;

    /**
     * The hashes in tallied_ in the order they were counted. The front is
     * the oldest and is dropped first once the bound is reached.
     */
    std::deque<uint256> talliedOrder_;

    /**
     * One-minute counts, indexed by minute modulo kBuckets7d.
     */
    std::array<Bucket, kBuckets7d> buckets_{};

    /**
     * Running counts for the 1-hour window.
     */
    WindowCount c1h_;

    /**
     * Running counts for the 24-hour window.
     */
    WindowCount c24h_;

    /**
     * Running counts for the 7-day window.
     */
    WindowCount c7d_;

    /**
     * Oldest minute the 1-hour window still counts.
     */
    std::uint64_t tail1h_{0};

    /**
     * Oldest minute the 24-hour window still counts.
     */
    std::uint64_t tail24h_{0};

    /**
     * Oldest minute the 7-day window still counts.
     */
    std::uint64_t tail7d_{0};

    /**
     * Newest minute written to the grid.
     */
    std::uint64_t newestMinute_{0};

    /**
     * False until the first minute is recorded, which is when the tails and
     * newestMinute_ get their starting value.
     */
    bool started_{false};

    /**
     * The snapshot readers see. The reducer swaps in a new one each cycle, and
     * a reader that took the old one keeps it alive while it reads. Null until
     * the first reconcile().
     */
    boost::atomic_shared_ptr<Snapshot const> published_;

    /**
     * Lifetime count of agreements.
     */
    std::atomic<std::uint64_t> totalAgreements_{0};

    /**
     * Lifetime count of misses.
     */
    std::atomic<std::uint64_t> totalMissed_{0};

    /**
     * Lifetime count of validations this node sent.
     */
    std::atomic<std::uint64_t> totalValidationsSent_{0};

    /**
     * Lifetime count of network validations observed.
     */
    std::atomic<std::uint64_t> totalValidationsChecked_{0};

    /**
     * Lifetime count of events dropped by a full ring.
     */
    std::atomic<std::uint64_t> droppedEvents_{0};
};

}  // namespace xrpl::telemetry
