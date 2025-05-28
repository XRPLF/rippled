//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_OVERLAY_CLOCK_H_INCLUDED
#define RIPPLE_TEST_OVERLAY_CLOCK_H_INCLUDED

#include <xrpl/basics/random.h>

#include <chrono>
#include <cstdint>
#include <ratio>

namespace ripple {

namespace test {

using namespace std::chrono;

/** Manually advanced clock. */
class ManualClock
{
public:
    typedef uint64_t rep;
    typedef std::milli period;
    typedef std::chrono::duration<std::uint32_t, period> duration;
    typedef std::chrono::time_point<ManualClock> time_point;
    inline static bool const is_steady = false;

    static void
    advance(duration d) noexcept
    {
        now_ += d;
    }

    static void
    randAdvance(milliseconds min, milliseconds max)
    {
        now_ += randDuration(min, max);
    }

    static void
    reset() noexcept
    {
        now_ = time_point(seconds(0));
    }

    static time_point
    now() noexcept
    {
        return now_;
    }

    static duration
    randDuration(milliseconds min, milliseconds max)
    {
        return duration(milliseconds(rand_int(min.count(), max.count())));
    }

    explicit ManualClock() = default;

private:
    inline static time_point now_ = time_point(seconds(0));
};
}  // namespace test
}  // namespace ripple

#endif