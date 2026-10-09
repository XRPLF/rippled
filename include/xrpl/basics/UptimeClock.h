#pragma once

#include <atomic>
#include <chrono>
#include <ratio>
#include <thread>

namespace xrpl {

/**
 * Tracks program uptime to seconds precision.
 *
 * The timer caches the current time as a performance optimization.
 * This allows clients to query the current time thousands of times
 * per second.
 */

class UptimeClock
{
public:
    using Rep = int;
    using Period = std::ratio<1>;
    using Duration = std::chrono::duration<Rep, Period>;
    using TimePoint = std::chrono::time_point<UptimeClock, Duration>;

    // Required by the std Clock contract.
    // NOLINTBEGIN(readability-identifier-naming)
    using rep = Rep;
    using period = Period;
    using duration = Duration;
    using time_point = TimePoint;

    static constexpr bool is_steady = std::chrono::system_clock::is_steady;
    // NOLINTEND(readability-identifier-naming)

    explicit UptimeClock() = default;

    static time_point
    now();  // seconds since xrpld program start

private:
    static std::atomic<rep> kNow;
    static std::atomic<bool> kStop;

    struct UpdateThread : private std::thread
    {
        ~UpdateThread();
        UpdateThread(UpdateThread&&) = default;

        using std::thread::thread;
    };

    static UpdateThread
    startClock();
};

}  // namespace xrpl
