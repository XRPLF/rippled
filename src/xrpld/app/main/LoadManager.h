#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace xrpl {

class Application;

/**
 * Manages load sources.
 *
 * This object creates an associated thread to maintain a clock.
 *
 * When the server is overloaded by a particular peer it issues a warning
 * first. This allows friendly peers to reduce their consumption of resources,
 * or disconnect from the server.
 *
 * The warning system is used instead of merely dropping, because hostile
 * peers can just reconnect anyway.
 *
 * Besides warning peers, the monitor thread is the only place that knows how
 * long the server's main loop has been unresponsive. That duration used to
 * exist only inside a log line; it is now also published through the two
 * stall accessors below so telemetry can chart it.
 *
 *     +-------------+  heartbeat()   +--------------------+
 *     |  main loop  |--------------->|    LoadManager     |
 *     +-------------+                |  (monitor thread)  |
 *                                    +---------+----------+
 *                        stall state |         | fee changes
 *                                    v         v
 *                        currentStallSeconds_  LoadFeeTrack
 *                        stallEventCount_
 *                                    ^
 *                                    | getCurrentStallSeconds()
 *                                    | getStallEventCount()
 *                              telemetry::MetricsRegistry
 *                              (sync_state gauge callback)
 */
class LoadManager
{
    LoadManager(Application& app, beast::Journal journal);

public:
    LoadManager() = delete;
    LoadManager(LoadManager const&) = delete;
    LoadManager&
    operator=(LoadManager const&) = delete;

    /**
     * Destroy the manager.
     *
     * The destructor returns only after the thread has stopped.
     */
    ~LoadManager();

    /**
     * Turn on stall detection.
     *
     * The stall detector begins in a disabled state. After this function
     * is called, it will report stalls using a separate thread whenever
     * the reset function is not called at least once per 10 seconds.
     *
     * @see resetStallDetector
     */
    // VFALCO NOTE it seems that the stall detector has an "armed" state
    //             to prevent it from going off during program startup if
    //             there's a lengthy initialization operation taking place?
    //
    void
    activateStallDetector();

    /**
     * Reset the stall detection timer.
     *
     * A dedicated thread monitors the stall timer, and if too much
     * time passes it will produce log warnings.
     */
    void
    heartbeat();

    /**
     * Seconds the server's main loop has currently been unresponsive.
     *
     * Zero means healthy: either the heartbeat is current or the stall
     * detector is not armed yet. A non-zero value is the same duration the
     * monitor thread logs as "Server stalled for N seconds", refreshed on
     * its one-second tick.
     *
     * @return Current stall duration in seconds, 0 when not stalled.
     *
     * @note Safe to call from any thread, including a telemetry
     * observable-gauge callback: one relaxed atomic load, no lock.
     */
    [[nodiscard]] std::uint32_t
    getCurrentStallSeconds() const
    {
        return currentStallSeconds_.load(std::memory_order_relaxed);
    }

    /**
     * Number of distinct stall episodes seen since process start.
     *
     * Counts once per episode, on the tick the stall is first reported, not
     * once per second of stalling. So a rising count means new stalls keep
     * happening, which is a different fault from one long stall (that shows
     * up as a large getCurrentStallSeconds() with a flat count).
     *
     * @return Monotonic stall-episode count.
     *
     * @note Safe to call from any thread; one relaxed atomic load.
     */
    [[nodiscard]] std::uint64_t
    getStallEventCount() const
    {
        return stallEventCount_.load(std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------

    void
    start();

    void
    stop();

    /**
     * What one monitor tick concludes about the stall state.
     *
     * @see evaluateStall
     */
    struct StallState
    {
        /**
         * Stall seconds to publish for this tick; 0 when healthy.
         */
        std::uint32_t seconds;
        /**
         * True only on the tick that begins a new stall episode.
         */
        bool newEpisode;
    };

    /**
     * Decide the stall state for one monitor tick.
     *
     * A stall counts as reportable at the same threshold the monitor's log line
     * uses, so the gauge and the log never disagree about whether the server is
     * stalled. Below the threshold the tick reports healthy (0 seconds), which
     * is why a brief scheduling hiccup does not register as a stall.
     *
     * An episode begins on the healthy -> reportable transition only, so one
     * continuous stall increments the episode count exactly once no matter how
     * many ticks it spans. That is what lets an operator tell one long stall
     * (large `seconds`, flat count) from repeated short ones (rising count).
     *
     * Pure and side-effect free: it is the whole decision rule for the stall
     * signals, kept separate from the atomics it feeds so the rule can be
     * asserted directly without a test-only mutator on LoadManager.
     *
     * @param previousSeconds  Value published by the previous tick.
     * @param stalled          Stall duration measured on this tick.
     * @param reportThreshold  Duration at which a stall becomes reportable.
     * @return The seconds to publish and whether a new episode started.
     *
     * Example -- a stall crossing the threshold, then persisting:
     * @code
     * // First tick over the 10 s threshold: publishes 10, starts an episode.
     * auto first = LoadManager::evaluateStall(0, 10s, 10s);   // {10, true}
     * // Still stalled 20 s later: publishes 20, but the SAME episode.
     * auto next = LoadManager::evaluateStall(10, 30s, 10s);   // {30, false}
     * @endcode
     *
     * Example -- edge case: a sub-threshold blip never becomes an episode.
     * @code
     * auto blip = LoadManager::evaluateStall(0, 9s, 10s);     // {0, false}
     * @endcode
     */
    [[nodiscard]] static constexpr StallState
    evaluateStall(
        std::uint32_t const previousSeconds,
        std::chrono::seconds const stalled,
        std::chrono::seconds const reportThreshold) noexcept
    {
        bool const reportable = stalled >= reportThreshold;
        bool const wasReportable = previousSeconds >= reportThreshold.count();
        return StallState{
            .seconds = reportable ? static_cast<std::uint32_t>(stalled.count()) : 0U,
            .newEpisode = reportable && !wasReportable};
    }

private:
    void
    run();

    /**
     * Publish the stall state read by the telemetry gauge.
     *
     * Applies evaluateStall() to this tick and stores the result. Called once
     * per monitor tick, only from the monitor thread.
     *
     * @param stalled          Stall duration measured on this tick.
     * @param reportThreshold  Duration at which a stall becomes reportable.
     */
    void
    updateStallState(std::chrono::seconds stalled, std::chrono::seconds reportThreshold);

private:
    Application& app_;
    beast::Journal const journal_;

    std::thread thread_;
    std::mutex mutex_;  // Guards lastHeartbeat_, armed_, cv_
    std::condition_variable cv_;
    bool stop_ = false;

    // Detect server stalls
    std::chrono::steady_clock::time_point lastHeartbeat_;
    bool armed_;

    /**
     * Seconds the main loop has been unresponsive as of the last monitor
     * tick; 0 when healthy. Written only by the monitor thread, read by
     * telemetry, hence atomic rather than mutex-guarded.
     */
    std::atomic<std::uint32_t> currentStallSeconds_{0};

    /**
     * Monotonic count of stall episodes since process start. Written only by
     * the monitor thread; never reset.
     */
    std::atomic<std::uint64_t> stallEventCount_{0};

    friend std::unique_ptr<LoadManager>
    makeLoadManager(Application& app, beast::Journal journal);
};

std::unique_ptr<LoadManager>
makeLoadManager(Application& app, beast::Journal journal);

}  // namespace xrpl
