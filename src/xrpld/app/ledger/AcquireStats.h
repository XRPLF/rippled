#pragma once

#include <xrpl/telemetry/Recording.h>

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
 *                  |--- recordCompletion() ---->|  (9 counters)   |
 *                  \--- recordAbort() --------->|                 |
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
 * @note Thread-safe. Every counter is independent, so any number of threads
 *       may record concurrently without losing an increment. Because the
 *       counters are independent, no read across two of them is a consistent
 *       snapshot. Compare rates over an interval rather than instantaneous
 *       values.
 * @note All counters are monotonic for the life of the process and are never
 *       reset, so a reader differences successive samples to get a rate. They
 *       saturate only on 64-bit wraparound, which is unreachable in practice.
 * @note The counts exist only to be reported, so they are held in
 *       telemetry::Counter. In a build with telemetry compiled out the
 *       counters carry no storage, recording is a no-op, and every accessor
 *       returns 0. The public API is the same in both builds.
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
    recordDeferral(bool ledgerAcquisition = false)
    {
        deferrals_.add();
        if (ledgerAcquisition)
            ledgerDeferrals_.add();
    }

    /**
     * Record that a timer body ran and advanced the retry count.
     *
     * This is the counter that give-up is measured against, so it is kept
     * separate from deferrals.
     */
    void
    recordTimeout(bool ledgerAcquisition = false)
    {
        timeouts_.add();
        if (ledgerAcquisition)
            ledgerTimeouts_.add();
    }

    /**
     * Record that an acquisition exceeded its retry budget and failed
     * cleanly.
     */
    void
    recordGiveUp()
    {
        giveUps_.add();
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
        aborts_.add();
        if (hadPartialWork)
            abortsWithPartialWork_.add();
    }

    /**
     * Record that an acquisition finished successfully.
     */
    void
    recordCompletion()
    {
        completions_.add();
    }

    /**
     * Record that an idle acquisition was evicted by the sweep.
     */
    void
    recordSweepEviction()
    {
        sweepEvictions_.add();
    }

    /**
     * Return the number of timer jobs skipped because a lane was full.
     */
    [[nodiscard]] std::uint64_t
    getDeferrals() const
    {
        return deferrals_.load();
    }

    /**
     * Return the number of timer bodies that ran without progress.
     */
    [[nodiscard]] std::uint64_t
    getTimeouts() const
    {
        return timeouts_.load();
    }

    /**
     * Deferrals that belong to ledger acquisition only.
     *
     * @ref getDeferrals covers every TimeoutCounter subclass, so a busy
     * replay or transaction-set lane inflates it. Compare this against
     * @ref getLedgerTimeouts to judge ledger acquisition on its own.
     */
    [[nodiscard]] std::uint64_t
    getLedgerDeferrals() const
    {
        return ledgerDeferrals_.load();
    }

    /**
     * Timeouts that belong to ledger acquisition only.
     *
     * The partner of @ref getLedgerDeferrals: rising deferrals with flat
     * timeouts here means ledger acquisition's give-up path is disarmed,
     * which the all-lane counters cannot show.
     */
    [[nodiscard]] std::uint64_t
    getLedgerTimeouts() const
    {
        return ledgerTimeouts_.load();
    }

    /**
     * Return the number of acquisitions that exhausted their retry budget.
     */
    [[nodiscard]] std::uint64_t
    getGiveUps() const
    {
        return giveUps_.load();
    }

    /**
     * Return the number of acquisitions destroyed before finishing.
     */
    [[nodiscard]] std::uint64_t
    getAborts() const
    {
        return aborts_.load();
    }

    /**
     * Return the number of aborts that discarded a partly built map.
     *
     * This is a subset of getAborts().
     */
    [[nodiscard]] std::uint64_t
    getAbortsWithPartialWork() const
    {
        return abortsWithPartialWork_.load();
    }

    /**
     * Return the number of acquisitions that finished successfully.
     */
    [[nodiscard]] std::uint64_t
    getCompletions() const
    {
        return completions_.load();
    }

    /**
     * Return the number of idle acquisitions evicted by the sweep.
     */
    [[nodiscard]] std::uint64_t
    getSweepEvictions() const
    {
        return sweepEvictions_.load();
    }

private:
    /**
     * Timer jobs skipped because the job lane was at its limit.
     */
    telemetry::Counter<> deferrals_;

    /**
     * Deferrals attributable to ledger acquisition alone.
     */
    telemetry::Counter<> ledgerDeferrals_;

    /**
     * Timeouts attributable to ledger acquisition alone.
     */
    telemetry::Counter<> ledgerTimeouts_;

    /**
     * Timer bodies that ran and advanced the retry count.
     */
    telemetry::Counter<> timeouts_;

    /**
     * Acquisitions that exhausted their retry budget.
     */
    telemetry::Counter<> giveUps_;

    /**
     * Acquisitions destroyed before finishing.
     */
    telemetry::Counter<> aborts_;

    /**
     * Aborts that discarded a partly built map.
     */
    telemetry::Counter<> abortsWithPartialWork_;

    /**
     * Acquisitions that finished successfully.
     */
    telemetry::Counter<> completions_;

    /**
     * Idle acquisitions evicted by the sweep.
     */
    telemetry::Counter<> sweepEvictions_;
};

}  // namespace xrpl
