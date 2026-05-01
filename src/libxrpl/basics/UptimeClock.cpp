
#include <xrpl/basics/UptimeClock.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace xrpl {

std::atomic<UptimeClock::rep> UptimeClock::kNOW{0};  // seconds since start
std::atomic<bool> UptimeClock::kSTOP{false};         // stop update thread

// On xrpld shutdown, cancel and wait for the update thread
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

// Launch the update thread
UptimeClock::UpdateThread
UptimeClock::startClock()
{
    return UpdateThread{[] {
        using namespace std;
        using namespace std::chrono;

        // Wake up every second and update kNOW
        auto next = system_clock::now() + 1s;
        while (!kSTOP)
        {
            this_thread::sleep_until(next);
            next += 1s;
            ++kNOW;
        }
    }};
}

// This actually measures time since first use, instead of since xrpld start.
// However the difference between these two epochs is a small fraction of a
// second and unimportant.

UptimeClock::time_point
UptimeClock::now()
{
    // start the update thread on first use
    static auto const kINIT = startClock();

    // Return the number of seconds since xrpld start
    return time_point{duration{kNOW}};
}

}  // namespace xrpl
