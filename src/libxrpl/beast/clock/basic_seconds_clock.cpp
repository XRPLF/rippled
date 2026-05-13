/** @file
 *  Implements `BasicSecondsClock`: a low-cost, second-resolution clock backed
 *  by a background thread that amortises OS time queries across all callers.
 *
 *  Hot paths (message timestamping, cache expiry, ledger timing) may query the
 *  time millions of times per second.  Rather than routing each query through
 *  the OS, a single `SecondsClockThread` samples `std::chrono::steady_clock`
 *  at every second boundary and publishes the result into a lock-free atomic.
 *  Callers then read that atomic with no arithmetic and no kernel transition.
 */
#include <xrpl/beast/clock/basic_seconds_clock.h>

#include <xrpl/beast/utility/instrumentation.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace beast {

namespace {

/** Background thread that maintains a second-resolution time point for
 *  `BasicSecondsClock`.
 *
 *  A single writer thread wakes at each second boundary and stores the
 *  current `Clock::time_point` into an atomic integer (`tp_`). Callers
 *  read `tp_` via a single lock-free atomic load — no OS calls, no
 *  arithmetic.  The maximum staleness of any returned value is one second.
 *
 *  Thread safety: `now()` may be called concurrently by any number of
 *  threads.  Construction and destruction are not thread-safe with respect
 *  to each other, but the singleton pattern in `BasicSecondsClock::now()`
 *  ensures they never overlap.
 */
class SecondsClockThread
{
    using Clock = BasicSecondsClock::Clock;

    bool stop_{false};
    std::mutex mut_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<Clock::time_point::rep> tp_;

public:
    ~SecondsClockThread();
    SecondsClockThread();

    /** Return the most recently sampled time point.
     *
     *  Reduces to a single lock-free atomic load; no kernel involvement.
     *  The returned value may lag the true current time by up to one second.
     *
     *  @return The last time point recorded by the background thread.
     */
    Clock::time_point
    now();

private:
    /** Background loop: sample the clock, sleep to the next second boundary,
     *  repeat until `stop_` is set.
     */
    void
    run();
};

// If this fires, reading tp_ from caller threads would silently acquire an
// internal mutex, negating the entire performance rationale of this class.
static_assert(std::atomic<std::chrono::steady_clock::rep>::is_always_lock_free);

/** Signal the background thread to stop and block until it exits.
 *
 *  Sets `stop_` under the mutex, releases the lock, then notifies the
 *  condition variable.  Releasing before notifying ensures that if the
 *  sleeping thread happens to time out and re-check the predicate before
 *  the notification arrives it will still see `stop_ == true` and exit
 *  without an extra sleep cycle.
 */
SecondsClockThread::~SecondsClockThread()
{
    XRPL_ASSERT(
        thread_.joinable(), "beast::SecondsClockThread::~SecondsClockThread : thread joinable");
    {
        std::scoped_lock const lock(mut_);
        stop_ = true;
    }  // publish stop_ asap so if waiting thread times-out, it will see it
    cv_.notify_one();
    thread_.join();
}

/** Initialise `tp_` to the current time and start the background thread.
 *
 *  Pre-seeding `tp_` before launching the thread guarantees that any call
 *  to `now()` between construction and the first loop iteration returns a
 *  valid, current timestamp rather than a zero-epoch value.
 */
SecondsClockThread::SecondsClockThread() : tp_{Clock::now().time_since_epoch().count()}
{
    thread_ = std::thread(&SecondsClockThread::run, this);
}

SecondsClockThread::Clock::time_point
SecondsClockThread::now()
{
    return Clock::time_point{Clock::duration{tp_.load()}};
}

/** Sample the clock, publish the result, then sleep to the next second
 *  boundary.
 *
 *  Sleeping to `floor<seconds>(now) + 1s` (rather than `sleep_for(1s)`)
 *  anchors wake-ups to wall-clock second boundaries, preventing drift from
 *  accumulated sleep overhead over long uptimes.  The `wait_until` predicate
 *  checks `stop_`, so a shutdown notification wakes the thread immediately
 *  without waiting for the next second.
 */
void
SecondsClockThread::run()
{
    std::unique_lock lock(mut_);
    while (true)
    {
        using namespace std::chrono;

        auto now = Clock::now();
        tp_ = now.time_since_epoch().count();
        auto const when = floor<seconds>(now) + 1s;
        if (cv_.wait_until(lock, when, [this] { return stop_; }))
            return;
    }
}

}  // unnamed namespace

/** Return a second-resolution approximation of the current steady clock time.
 *
 *  On the first call, constructs the `SecondsClockThread` singleton (C++11
 *  guarantees thread-safe initialisation of function-local statics).
 *  Subsequent calls reduce to a single lock-free atomic load with no OS
 *  involvement.  The returned value may lag the true current time by up to
 *  one second, which is sufficient for all ledger-timing and cache-expiry
 *  use cases.
 *
 *  @return A `time_point` whose value was current within the last second.
 *  @see xrpl::stopwatch() which exposes this clock as the node's `Stopwatch`.
 */
BasicSecondsClock::time_point
BasicSecondsClock::now()
{
    static SecondsClockThread kCLK;
    return kCLK.now();
}

}  // namespace beast
