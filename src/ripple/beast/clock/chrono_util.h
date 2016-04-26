//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CHRONO_CHRONO_UTIL_H_INCLUDED
#define BEAST_CHRONO_CHRONO_UTIL_H_INCLUDED

#include <chrono>

// From Howard Hinnant
// http://home.roadrunner.com/~hinnant/duration_io/chrono_util.html

#if !defined(_MSC_FULL_VER) || (_MSC_FULL_VER <= 190023506)
// round down
template <class To, class Rep, class Period>
To floor(std::chrono::duration <Rep, Period> const& d)
{
    To t = std::chrono::duration_cast<To>(d);
    if (t > d)
        --t;
    return t;
}

// round to nearest, to even on tie
template <class To, class Rep, class Period>
To round (std::chrono::duration <Rep, Period> const& d)
{
    To t0 = std::chrono::duration_cast<To>(d);
    To t1 = t0;
    ++t1;
    auto diff0 = d - t0;
    auto diff1 = t1 - d;
    if (diff0 == diff1)
    {
        if (t0.count() & 1)
            return t1;
        return t0;
    }
    else if (diff0 < diff1)
        return t0;
    return t1;
}

// round up
template <class To, class Rep, class Period>
To ceil (std::chrono::duration <Rep, Period> const& d)
{
    To t = std::chrono::duration_cast<To>(d);
    if (t < d)
        ++t;
    return t;
}
#endif

#endif
