#pragma once

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/clock/basic_seconds_clock.h>
#include <xrpl/beast/clock/manual_clock.h>

#include <date/date.h>

#include <chrono>
#include <cstdint>
#include <ratio>
#include <string>

namespace xrpl {

// A few handy aliases

using days =
    std::chrono::duration<int, std::ratio_multiply<std::chrono::hours::period, std::ratio<24>>>;

using weeks = std::chrono::duration<int, std::ratio_multiply<days::period, std::ratio<7>>>;

/** Clock for measuring the network time.

    The epoch is January 1, 2000

    epoch_offset
    = date(2000-01-01) - date(1970-0-01)
    = days(10957)
    = seconds(946684800)
*/

static constexpr std::chrono::seconds kEpochOffset =
    date::sys_days{date::year{2000} / 1 / 1} - date::sys_days{date::year{1970} / 1 / 1};

static_assert(kEpochOffset.count() == 946684800);

class NetClock
{
public:
    explicit NetClock() = default;

    using rep = std::uint32_t;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<NetClock>;

    static bool const is_steady = false;  // NOLINT(readability-identifier-naming)
};

template <class Duration>
std::string
to_string(date::sys_time<Duration> tp)
{
    return date::format("%Y-%b-%d %T %Z", tp);
}

inline std::string
to_string(NetClock::time_point tp)
{
    // 2000-01-01 00:00:00 UTC is 946684800s from 1970-01-01 00:00:00 UTC
    using namespace std::chrono;
    return to_string(system_clock::time_point{tp.time_since_epoch() + kEpochOffset});
}

template <class Duration>
std::string
toStringIso(date::sys_time<Duration> tp)
{
    using namespace std::chrono;
    return date::format("%FT%TZ", tp);
}

inline std::string
toStringIso(NetClock::time_point tp)
{
    // 2000-01-01 00:00:00 UTC is 946684800s from 1970-01-01 00:00:00 UTC
    // Note, NetClock::duration is seconds, as checked by static_assert
    static_assert(std::is_same_v<NetClock::duration::period, std::ratio<1>>);
    return toStringIso(date::sys_time<NetClock::duration>{tp.time_since_epoch() + kEpochOffset});
}

/** A clock for measuring elapsed time.

    The epoch is unspecified.
*/
using Stopwatch = beast::AbstractClock<std::chrono::steady_clock>;

/** A manual Stopwatch for unit tests. */
using TestStopwatch = beast::ManualClock<std::chrono::steady_clock>;

/** Returns an instance of a wall clock. */
inline Stopwatch&
stopwatch()
{
    using Clock = beast::BasicSecondsClock;
    using Facade = Clock::Clock;
    return beast::getAbstractClock<Facade, Clock>();
}

}  // namespace xrpl
