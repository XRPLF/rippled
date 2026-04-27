#pragma once

#include <test/csf/Scheduler.h>
#include <test/csf/SimTime.h>

#include <chrono>
#include <ostream>

namespace xrpl::test::csf {

// Timers are classes that schedule repeated events and are mostly independent
// of simulation-specific details.

/** Gives heartbeat of simulation to signal simulation progression
 */
class HeartbeatTimer
{
    Scheduler& scheduler_;
    SimDuration interval_;
    std::ostream& out_;

    RealTime startRealTime_;
    SimTime startSimTime_;

public:
    HeartbeatTimer(
        Scheduler& sched,
        SimDuration interval = std::chrono::seconds{60},
        std::ostream& out = std::cerr)
        : scheduler_{sched}
        , interval_{interval}
        , out_{out}
        , startRealTime_{RealClock::now()}
        , startSimTime_{sched.now()}
    {
    }

    void
    start()
    {
        scheduler_.in(interval_, [this]() { beat(scheduler_.now()); });
    }

    void
    beat(SimTime when)
    {
        using namespace std::chrono;
        RealTime const realTime = RealClock::now();
        SimTime const simTime = when;

        RealDuration const realDuration = realTime - startRealTime_;
        SimDuration const simDuration = simTime - startSimTime_;
        out_ << "Heartbeat. Time Elapsed: {sim: " << duration_cast<seconds>(simDuration).count()
             << "s | real: " << duration_cast<seconds>(realDuration).count() << "s}\n"
             << std::flush;

        scheduler_.in(interval_, [this]() { beat(scheduler_.now()); });
    }
};

}  // namespace xrpl::test::csf
