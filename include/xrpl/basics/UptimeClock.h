#pragma once

#include <atomic>
#include <chrono>
#include <ratio>
#include <thread>

namespace xrpl {

/** Tracks program uptime to seconds precision.

    The timer caches the current time as a performance optimization.
    This allows clients to query the current time thousands of times
    per second.
*/

class UptimeClock
{
public:
    using rep = int;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<UptimeClock>;
    static constexpr bool is_steady =  // NOLINT(readability-identifier-naming)
        std::chrono::system_clock::is_steady;

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
