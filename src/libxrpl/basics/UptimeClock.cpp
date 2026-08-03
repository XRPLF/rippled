
#include <xrpl/basics/UptimeClock.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace xrpl {

std::atomic<UptimeClock::rep> UptimeClock::kNow{0};  // seconds since start
std::atomic<bool> UptimeClock::kStop{false};         // stop update thread

// On xrpld shutdown, cancel and wait for the update thread
UptimeClock::UpdateThread::~UpdateThread()
{
    if (joinable())
    {
        kStop = true;
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

        // Wake up every second and update kNow
        auto next = system_clock::now() + 1s;
        while (!kStop)
        {
            this_thread::sleep_until(next);
            next += 1s;
            ++kNow;
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
    static auto const kInit = startClock();

    // Return the number of seconds since xrpld start
    return time_point{duration{kNow}};
}

}  // namespace xrpl
