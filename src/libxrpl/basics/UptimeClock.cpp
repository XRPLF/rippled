/** @file
 *  Implements UptimeClock: a seconds-precision uptime clock backed by a
 *  single atomic counter incremented by a background thread.
 *
 *  Reading the clock is an atomic load (a few nanoseconds, no syscall),
 *  making it safe to call thousands of times per second from subsystems
 *  such as LoadMonitor, the overlay, and RPC handlers. The background
 *  thread wakes once per second and increments the counter; accuracy is
 *  ±1 second, which is acceptable for uptime reporting and coarse
 *  rate-limiting.
 */

#include <xrpl/basics/UptimeClock.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace xrpl {

std::atomic<UptimeClock::rep> UptimeClock::kNOW{0};  // seconds since start
std::atomic<bool> UptimeClock::kSTOP{false};         // stop update thread

/** Signal the background counter thread to stop and block until it exits.
 *
 *  Sets `kSTOP` to `true` then calls `join()`. The thread checks `kSTOP`
 *  before each sleep, so the wait is at most one sleep cycle (≤ 1 second).
 */
UptimeClock::UpdateThread::~UpdateThread()
{
    if (joinable())
    {
        kSTOP = true;
        // This join() may take up to a 1s, but happens only
        // once at xrpld shutdown.
        join();
    }
}

/** Start the background thread that increments `kNOW` once per second.
 *
 *  Uses `sleep_until` against a fixed `next` timestamp rather than
 *  `sleep_for` so that scheduling jitter does not accumulate into drift
 *  over the lifetime of the process.
 *
 *  @return An `UpdateThread` RAII handle that joins on destruction.
 */
UptimeClock::UpdateThread
UptimeClock::startClock()
{
    return UpdateThread{[] {
        using namespace std;
        using namespace std::chrono;

        auto next = system_clock::now() + 1s;
        while (!kSTOP)
        {
            this_thread::sleep_until(next);
            next += 1s;
            ++kNOW;
        }
    }};
}

/** Return the number of seconds elapsed since first use of this clock.
 *
 *  Lazily starts the background counter thread on the first call via a
 *  function-local `static`; C++11 guarantees this initialisation is
 *  thread-safe and happens exactly once. Subsequent calls are a single
 *  atomic load with no kernel transition.
 *
 *  @return A `time_point` whose value is the contents of `kNOW`.
 *  @note The epoch is the moment of first call rather than true process
 *      start, but the difference is a negligible fraction of a second.
 */
UptimeClock::time_point
UptimeClock::now()
{
    static auto const kINIT = startClock();

    return time_point{duration{kNOW}};
}

}  // namespace xrpl
