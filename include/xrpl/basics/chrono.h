//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_CHRONO_H_INCLUDED
#define RIPPLE_BASICS_CHRONO_H_INCLUDED

#include <date/date.h>

#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/clock/basic_seconds_clock.h>
#include <ripple/beast/clock/manual_clock.h>

#include <chrono>
#include <cstdint>
#include <ratio>
#include <string>
#include <type_traits>

namespace ripple {

// A few handy aliases

using days = std::chrono::duration<
    int,
    std::ratio_multiply<std::chrono::hours::period, std::ratio<24>>>;

using weeks = std::chrono::
    duration<int, std::ratio_multiply<days::period, std::ratio<7>>>;

/** Clock for measuring the network time.

    The epoch is January 1, 2000

    epoch_offset
    = date(2000-01-01) - date(1970-0-01)
    = days(10957)
    = seconds(946684800)
*/

constexpr static std::chrono::seconds epoch_offset =
    date::sys_days{date::year{2000} / 1 / 1} -
    date::sys_days{date::year{1970} / 1 / 1};

static_assert(epoch_offset.count() == 946684800);

class NetClock
{
public:
    explicit NetClock() = default;

    using rep = std::uint32_t;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<NetClock>;

    static bool const is_steady = false;
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
    return to_string(
        system_clock::time_point{tp.time_since_epoch() + epoch_offset});
}

template <class Duration>
std::string
to_string_iso(date::sys_time<Duration> tp)
{
    using namespace std::chrono;
    return date::format("%FT%TZ", tp);
}

inline std::string
to_string_iso(NetClock::time_point tp)
{
    // 2000-01-01 00:00:00 UTC is 946684800s from 1970-01-01 00:00:00 UTC
    // Note, NetClock::duration is seconds, as checked by static_assert
    static_assert(std::is_same_v<NetClock::duration::period, std::ratio<1>>);
    return to_string_iso(date::sys_time<NetClock::duration>{
        tp.time_since_epoch() + epoch_offset});
}

/** A clock for measuring elapsed time.

    The epoch is unspecified.
*/
using Stopwatch = beast::abstract_clock<std::chrono::steady_clock>;

/** A manual Stopwatch for unit tests. */
using TestStopwatch = beast::manual_clock<std::chrono::steady_clock>;

/** Returns an instance of a wall clock. */
inline Stopwatch&
stopwatch()
{
    using Clock = beast::basic_seconds_clock;
    using Facade = Clock::Clock;
    return beast::get_abstract_clock<Facade, Clock>();
}

}  // namespace ripple

#endif
