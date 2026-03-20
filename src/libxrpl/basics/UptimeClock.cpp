#include <xrpl/basics/UptimeClock.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace xrpl {

std::atomic<UptimeClock::rep> UptimeClock::now_{0};  // seconds since start
std::atomic<bool> UptimeClock::stop_{false};         // stop update thread

// On rippled shutdown, cancel and wait for the update thread
UptimeClock::UpdateThread::~UpdateThread()
{
    if (joinable())
    {
        stop_ = true;
        // This join() may take up to a 1s, but happens only
        // once at rippled shutdown.
        join();
    }
}

// Launch the update thread
UptimeClock::UpdateThread
UptimeClock::startClock()
{
    return UpdateThread{[] {
        using namespace std;
        using namespace std::chrono;

        // Wake up every second and update now_
        auto next = system_clock::now() + 1s;
        while (!stop_)
        {
            this_thread::sleep_until(next);
            next += 1s;
            ++now_;
        }
    }};
}

// This actually measures time since first use, instead of since rippled start.
// However the difference between these two epochs is a small fraction of a
// second and unimportant.

UptimeClock::time_point
UptimeClock::now()
{
    // start the update thread on first use
    static auto const kINIT = startClock();

    // Return the number of seconds since rippled start
    return time_point{duration{now_}};
}

}  // namespace xrpl
