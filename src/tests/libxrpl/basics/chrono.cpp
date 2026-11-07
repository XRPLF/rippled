#include <xrpl/basics/chrono.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace xrpl::test {

using namespace std::chrono;
using namespace std::chrono_literals;

// The first and last instants network time can represent, reached through
// the public surface: the epoch is network time zero, and time_point::max()
// is 2^32 - 1 seconds later.
constexpr sys_seconds kMinTime = NetClock::to_sys(NetClock::time_point{});
constexpr sys_seconds kMaxTime = NetClock::to_sys(NetClock::time_point::max());
constexpr auto kMaxCount = std::numeric_limits<NetClock::rep>::max();

static_assert(kMinTime.time_since_epoch().count() == 946684800);
static_assert((kMaxTime - kMinTime).count() == kMaxCount);

// A calendar-resolution time point cannot be passed to from_sys: converting
// it to seconds could overflow before the range check. The caller widens it
// to seconds first.
template <class TimePoint>
concept ConvertibleToNetClock = requires(TimePoint tp) { NetClock::from_sys(tp); };
static_assert(!ConvertibleToNetClock<sys_days>);
static_assert(!ConvertibleToNetClock<sys_time<weeks>>);
static_assert(!ConvertibleToNetClock<sys_time<minutes>>);
static_assert(ConvertibleToNetClock<sys_seconds>);
static_assert(ConvertibleToNetClock<sys_time<nanoseconds>>);
static_assert(ConvertibleToNetClock<system_clock::time_point>);

// Both conversions are usable in constant expressions.
static_assert(NetClock::from_sys(kMinTime).time_since_epoch().count() == 0);
static_assert(NetClock::from_sys(kMaxTime).time_since_epoch().count() == kMaxCount);

// from_sys is [[nodiscard]]; this wrapper exists so that EXPECT_THROW can
// call it for its exception alone.
template <class Duration>
void
fromSys(sys_time<Duration> tp)
{
    [[maybe_unused]] auto const ignored = NetClock::from_sys(tp);
}

TEST(NetClock, fromSysBounds)
{
    EXPECT_EQ(NetClock::from_sys(kMinTime).time_since_epoch().count(), 0u);
    EXPECT_EQ(NetClock::from_sys(kMaxTime).time_since_epoch().count(), kMaxCount);

    // Sub-second precision is discarded, never rounded up.
    EXPECT_EQ(
        NetClock::from_sys(sys_time<milliseconds>{kMaxTime} + 999ms).time_since_epoch().count(),
        kMaxCount);
    EXPECT_EQ(
        NetClock::from_sys(sys_time<nanoseconds>{kMinTime} + 999'999'999ns)
            .time_since_epoch()
            .count(),
        0u);

    EXPECT_THROW(fromSys(kMinTime - 1s), std::domain_error);
    EXPECT_THROW(fromSys(sys_time<nanoseconds>{kMinTime} - 1ns), std::domain_error);
    EXPECT_THROW(fromSys(kMaxTime + 1s), std::domain_error);
    EXPECT_THROW(fromSys(sys_time<milliseconds>{kMaxTime} + 1000ms), std::domain_error);
}

TEST(NetClock, fromSysExtremes)
{
    // Inputs whose difference from the epoch, or whose conversion to another
    // unit, would overflow if it were computed before the range check. The
    // sanitizer build is what makes these regression tests.
    EXPECT_THROW(fromSys(sys_seconds::min()), std::domain_error);
    EXPECT_THROW(fromSys(sys_seconds::max()), std::domain_error);
    EXPECT_THROW(fromSys(sys_time<milliseconds>::min()), std::domain_error);
    EXPECT_THROW(fromSys(sys_time<milliseconds>::max()), std::domain_error);
    EXPECT_THROW(fromSys(sys_time<nanoseconds>::min()), std::domain_error);
    EXPECT_THROW(fromSys(sys_time<nanoseconds>::max()), std::domain_error);
}

TEST(NetClock, roundTrip)
{
    for (NetClock::rep const c : {0u, 1u, 826189445u, kMaxCount - 1, kMaxCount})
    {
        NetClock::time_point const tp{NetClock::duration{c}};
        EXPECT_EQ(NetClock::from_sys(NetClock::to_sys(tp)), tp);
        EXPECT_EQ(NetClock::to_sys(tp).time_since_epoch().count(), 946684800 + std::int64_t{c});
    }
}

TEST(NetClock, formatting)
{
    NetClock::time_point const tp{NetClock::duration{826189445}};  // 2026-03-07T09:04:05Z
    EXPECT_EQ(toStringIso(tp), "2026-03-07T09:04:05Z");
    // to_string widens to system_clock::time_point, so the sub-second field
    // is printed at that clock's resolution; only the prefix is portable.
    EXPECT_EQ(to_string(tp).substr(0, 20), "2026-Mar-07 09:04:05");
    EXPECT_TRUE(to_string(tp).ends_with(" UTC"));
}

}  // namespace xrpl::test
