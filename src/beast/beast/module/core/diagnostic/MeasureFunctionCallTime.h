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

#ifndef BEAST_MODULE_CORE_DIAGNOSTIC_MEASUREFUNCTIONCALLTIME_H_INCLUDED
#define BEAST_MODULE_CORE_DIAGNOSTIC_MEASUREFUNCTIONCALLTIME_H_INCLUDED

namespace beast
{

/** Measures the speed of invoking a function. */
/** @{ */
template <typename Function>
double measureFunctionCallTime (Function f)
{
    std::int64_t const startTime (Time::getHighResolutionTicks ());
    f ();
    return Time::highResolutionTicksToSeconds (
            Time::getHighResolutionTicks () - startTime);
}

#if 0
template <typename Function,
    typename P1, typename P2, typename P3, typename P4,
    typename P5, typename P6, typename P7, typename P8>
double measureFunctionCallTime (Function f, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8)
{
    std::int64_t const startTime (Time::getHighResolutionTicks ());
    f (p1, p2, p3, p4, p5 ,p6 ,p7 ,p8);
    return Time::highResolutionTicksToSeconds (
            Time::getHighResolutionTicks () - startTime);
}

template <typename Function,
    typename P1, typename P2, typename P3, typename P4,
    typename P5, typename P6, typename P7, typename P8>
double measureFunctionCallTime (Function f, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8)
{
    std::int64_t const startTime (Time::getHighResolutionTicks ());
    f (p1, p2, p3, p4, p5 ,p6 ,p7 ,p8);
    return Time::highResolutionTicksToSeconds (
            Time::getHighResolutionTicks () - startTime);
}

template <typename Function,
    typename P1, typename P2, typename P3, typename P4,
    typename P5, typename P6, typename P7, typename P8>
double measureFunctionCallTime (Function f, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8)
{
    std::int64_t const startTime (Time::getHighResolutionTicks ());
    f (p1, p2, p3, p4, p5 ,p6 ,p7 ,p8);
    return Time::highResolutionTicksToSeconds (
            Time::getHighResolutionTicks () - startTime);
}

template <typename Function,
    typename P1, typename P2, typename P3, typename P4,
    typename P5, typename P6, typename P7, typename P8>
double measureFunctionCallTime (Function f, P1 p1, P2 p2, P3 p3, P4 p4, P5 p5, P6 p6, P7 p7, P8 p8)
{
    std::int64_t const startTime (Time::getHighResolutionTicks ());
    f (p1, p2, p3, p4, p5 ,p6 ,p7 ,p8);
    return Time::highResolutionTicksToSeconds (
            Time::getHighResolutionTicks () - startTime);
}
#endif

} // beast

#endif
