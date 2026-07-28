#pragma once

#include <atomic>
#include <cstdint>

namespace xrpl {

/**
 * Counters for ledger-acquisition progress and stalls.
 *
 * Each event counted here is otherwise visible only as a debug log line, so a
 * node running at warning level cannot be diagnosed after the fact. The
 * counters exist so the acquisition state machine can be observed rather than
 * inferred.
 *
 * The diagnostic value is in the pairs, not in the individual counts:
 *
 * ```
 *   deferrals rising + timeouts flat  -> give-up path disarmed
 *   sweeps rising + completions zero  -> partial work discarded, then redone
 *   completions rising                -> healthy, whatever else is high
 * ```
 *
 * A deferral happens when the job lane is already at its limit. The timer is
 * re-armed without running the timer body, so the timeout that would
 * eventually trigger give-up never accrues. That divergence is invisible
 * unless deferrals and timeouts are counted separately, which is why they are
 * two counters and not one.
 *
 * Who writes each counter:
 *
 * ```
 *   TimeoutCounter ---- recordDeferral() ------> +-----------------+
 *                  \--- recordTimeout() ------->|                 |
 *                                               |                 |
 *   InboundLedger  ---- recordGiveUp() -------->|  AcquireStats   |
 *                  |--- recordCompletion() ---->|   (7 atomic     |
 *                  \--- recordAbort() --------->|    counters)    |
 *                                               |                 |
 *   InboundLedgers ---- recordSweepEviction() ->+-----------------+
 *                                                       |
 *                                        get*() reads   v
 *                                               metrics exporter
 * ```
 *
 * A single instance is owned by the application and shared by all
 * acquisitions, so the counts are process-wide rather than per-ledger.
 *
 * Example - detect the stall:
 * @code
 * auto const deferred = stats.getDeferrals() - prevDeferrals;
 * auto const timedOut = stats.getTimeouts() - prevTimeouts;
 * if (deferred > 100 && timedOut == 0)
 * {
 *     // The give-up path cannot fire, so acquisitions will never end.
 * }
 * @endcode
 *
 * Example - separate cheap restarts from expensive ones:
 * @code
 * auto const wasted = stats.getAbortsWithPartialWork() - prevAbortsWithWork;
 * if (wasted > 0)
 * {
 *     // Partly built maps were thrown away; that work has to be redone.
 * }
 * @endcode
 *
 * Example - edge case, a quiet node:
 * @code
 * // Every delta being zero means no acquisition was attempted, which is not
 * // the same as healthy. Check completions before concluding anything.
 * @endcode
 *
 * @note Thread-safe. Every counter is an independent relaxed atomic, so any
 *       number of threads may record concurrently without losing an
 *       increment. Because the counters are independent, no read across two
 *       of them is a consistent snapshot. Compare rates over an interval
 *       rather than instantaneous values.
 * @note All counters are monotonic for the life of the process and are never
 *       reset, so a reader differences successive samples to get a rate. They
 *       saturate only on 64-bit wraparound, which is unreachable in practice.
 * @note Completions count acquisitions that ended successfully, whether the
 *       data came from peers or was already present locally. They do not
 *       count an acquisition that is still in flight.
 */
class AcquireStats
{
public:
    /**
     * Record that a timer job was skipped because its job lane was full.
     *
     * The timer is re-armed but its body does not run, so this does not
     * advance the retry count toward give-up.
     */
    void
    recordDeferral()
    {
        deferrals_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Record that a timer body ran and advanced the retry count.
     *
     * This is the counter that give-up is measured against, so it is kept
     * separate from deferrals.
     */
    void
    recordTimeout()
    {
        timeouts_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Record that an acquisition exceeded its retry budget and failed
     * cleanly.
     */
    void
    recordGiveUp()
    {
        giveUps_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Record that an acquisition was destroyed before finishing.
     *
     * @param hadPartialWork True if part of a map had already been built.
     *                       That work is discarded and has to be redone, so
     *                       it is counted separately as the expensive case.
     */
    void
    recordAbort(bool hadPartialWork)
    {
        aborts_.fetch_add(1, std::memory_order_relaxed);
        if (hadPartialWork)
            abortsWithPartialWork_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Record that an acquisition finished successfully.
     */
    void
    recordCompletion()
    {
        completions_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Record that an idle acquisition was evicted by the sweep.
     */
    void
    recordSweepEviction()
    {
        sweepEvictions_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Return the number of timer jobs skipped because a lane was full.
     */
    [[nodiscard]] std::uint64_t
    getDeferrals() const
    {
        return deferrals_.load(std::memory_order_relaxed);
    }

    /**
     * Return the number of timer bodies that ran without progress.
     */
    [[nodiscard]] std::uint64_t
    getTimeouts() const
    {
        return timeouts_.load(std::memory_order_relaxed);
    }

    /**
     * Return the number of acquisitions that exhausted their retry budget.
     */
    [[nodiscard]] std::uint64_t
    getGiveUps() const
    {
        return giveUps_.load(std::memory_order_relaxed);
    }

    /**
     * Return the number of acquisitions destroyed before finishing.
     */
    [[nodiscard]] std::uint64_t
    getAborts() const
    {
        return aborts_.load(std::memory_order_relaxed);
    }

    /**
     * Return the number of aborts that discarded a partly built map.
     *
     * This is a subset of getAborts().
     */
    [[nodiscard]] std::uint64_t
    getAbortsWithPartialWork() const
    {
        return abortsWithPartialWork_.load(std::memory_order_relaxed);
    }

    /**
     * Return the number of acquisitions that finished successfully.
     */
    [[nodiscard]] std::uint64_t
    getCompletions() const
    {
        return completions_.load(std::memory_order_relaxed);
    }

    /**
     * Return the number of idle acquisitions evicted by the sweep.
     */
    [[nodiscard]] std::uint64_t
    getSweepEvictions() const
    {
        return sweepEvictions_.load(std::memory_order_relaxed);
    }

private:
    /**
     * Timer jobs skipped because the job lane was at its limit.
     */
    std::atomic<std::uint64_t> deferrals_{0};

    /**
     * Timer bodies that ran and advanced the retry count.
     */
    std::atomic<std::uint64_t> timeouts_{0};

    /**
     * Acquisitions that exhausted their retry budget.
     */
    std::atomic<std::uint64_t> giveUps_{0};

    /**
     * Acquisitions destroyed before finishing.
     */
    std::atomic<std::uint64_t> aborts_{0};

    /**
     * Aborts that discarded a partly built map.
     */
    std::atomic<std::uint64_t> abortsWithPartialWork_{0};

    /**
     * Acquisitions that finished successfully.
     */
    std::atomic<std::uint64_t> completions_{0};

    /**
     * Idle acquisitions evicted by the sweep.
     */
    std::atomic<std::uint64_t> sweepEvictions_{0};
};

}  // namespace xrpl
