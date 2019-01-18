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

#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/clock/basic_seconds_clock.h>
#include <ripple/beast/clock/manual_clock.h>
#include <chrono>
#include <cstdint>
#include <string>

namespace ripple {

// A few handy aliases

using days = std::chrono::duration<
    int, std::ratio_multiply<std::chrono::hours::period, std::ratio<24>>>;

using weeks = std::chrono::duration<
    int, std::ratio_multiply<days::period, std::ratio<7>>>;

/** Clock for measuring Ripple Network Time.

    The epoch is January 1, 2000
    epoch_offset = days(10957);  // 2000-01-01
*/
class NetClock
{
public:
    explicit NetClock() = default;

    using rep        = std::uint32_t;
    using period     = std::ratio<1>;
    using duration   = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<NetClock>;

    static bool const is_steady = false;
};

template <class Duration>
std::string
to_string(date::sys_time<Duration> tp)
{
    return date::format("%Y-%b-%d %T", tp);
}

inline
std::string
to_string(NetClock::time_point tp)
{
    // 2000-01-01 00:00:00 UTC is 946684800s from 1970-01-01 00:00:00 UTC
    using namespace std::chrono;
    return to_string(
        system_clock::time_point{tp.time_since_epoch() + 946684800s});
}

/** A clock for measuring elapsed time.

    The epoch is unspecified.
*/
using Stopwatch = beast::abstract_clock<std::chrono::steady_clock>;

/** A manual Stopwatch for unit tests. */
using TestStopwatch = beast::manual_clock<std::chrono::steady_clock>;

/** Returns an instance of a wall clock. */
inline
Stopwatch&
stopwatch()
{
    return beast::get_abstract_clock<
        std::chrono::steady_clock,
        beast::basic_seconds_clock<std::chrono::steady_clock>>();
}

} // ripple

#endif
