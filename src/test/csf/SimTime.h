#pragma once

#include <xrpl/beast/clock/manual_clock.h>

#include <chrono>

namespace xrpl::test::csf {

using RealClock = std::chrono::system_clock;
using RealDuration = RealClock::duration;
using RealTime = RealClock::time_point;

using SimClock = beast::ManualClock<std::chrono::steady_clock>;
using SimDuration = SimClock::duration;
using SimTime = SimClock::time_point;

}  // namespace xrpl::test::csf
