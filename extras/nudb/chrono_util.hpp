//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef CHRONO_UTIL_HPP
#define CHRONO_UTIL_HPP

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
