#ifndef XRPL_TEST_CSF_SIMTIME_H_INCLUDED
#define XRPL_TEST_CSF_SIMTIME_H_INCLUDED

#include <xrpl/beast/clock/manual_clock.h>

#include <chrono>

namespace ripple {
namespace test {
namespace csf {

using RealClock = std::chrono::system_clock;
using RealDuration = RealClock::duration;
using RealTime = RealClock::time_point;

using SimClock = beast::manual_clock<std::chrono::steady_clock>;
using SimDuration = typename SimClock::duration;
using SimTime = typename SimClock::time_point;

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
