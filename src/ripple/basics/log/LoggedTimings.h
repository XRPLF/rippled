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

#ifndef RIPPLE_BASICS_LOGGEDTIMINGS_H_INCLUDED
#define RIPPLE_BASICS_LOGGEDTIMINGS_H_INCLUDED

#include <beast/module/core/time/Time.h>
#include <beast/module/core/diagnostic/MeasureFunctionCallTime.h>
#include <beast/utility/Debug.h>
#include <ripple/basics/containers/SyncUnorderedMap.h>
    
namespace ripple {

namespace detail {

/** Template class that performs destruction of an object.
    Default implementation simply calls delete
*/
template <typename Object>
struct Destroyer;

/** Specialization for std::shared_ptr.
*/
template <typename Object>
struct Destroyer <std::shared_ptr <Object> >
{
    static void destroy (std::shared_ptr <Object>& p)
    {
        p.reset ();
    }
};

/** Specialization for std::unordered_map
*/
template <typename Key, typename Value, typename Hash, typename Alloc>
struct Destroyer <std::unordered_map <Key, Value, Hash, Alloc> >
{
    static void destroy (std::unordered_map <Key, Value, Hash, Alloc>& v)
    {
        v.clear ();
    }
};

/** Specialization for SyncUnorderedMapType
*/
template <typename Key, typename Value, typename Hash>
struct Destroyer <SyncUnorderedMapType <Key, Value, Hash> >
{
    static void destroy (SyncUnorderedMapType <Key, Value, Hash>& v)
    {
        v.clear ();
    }
};

/** Cleans up an elaspsed time so it prints nicely */
inline double cleanElapsed (double seconds) noexcept
{
    if (seconds >= 10)
        return std::floor (seconds + 0.5);

    return static_cast <int> ((seconds * 10 + 0.5) / 10);
}

} // detail

//------------------------------------------------------------------------------

/** Measure the time required to destroy an object.
*/

template <typename Object>
double timedDestroy (Object& object)
{
    std::int64_t const startTime (beast::Time::getHighResolutionTicks ());

    detail::Destroyer <Object>::destroy (object);

    return beast::Time::highResolutionTicksToSeconds (
            beast::Time::getHighResolutionTicks () - startTime);
}

/** Log the timed destruction of an object if the time exceeds a threshold.
*/
template <typename PartitionKey, typename Object>
void logTimedDestroy (
    Object& object, beast::String objectDescription, double thresholdSeconds = 1)
{
    double const seconds = timedDestroy (object);

    if (seconds > thresholdSeconds)
    {
        LogSeverity const severity = lsWARNING;

        Log (severity, LogPartition::get <PartitionKey> ()) <<
            objectDescription << " took "<<
            beast::String (detail::cleanElapsed (seconds)) <<
            " seconds to destroy";
    }
}

//------------------------------------------------------------------------------

/** Log a timed function call if the time exceeds a threshold. */
template <typename Function>
void logTimedCall (beast::Journal::Stream stream,
                   beast::String description,
                   char const* fileName,
                   int lineNumber,
    Function f, double thresholdSeconds = 1)
{
    double const seconds = beast::measureFunctionCallTime (f);

    if (seconds > thresholdSeconds)
    {
        stream <<
            description << " took "<<
                beast::String (detail::cleanElapsed (seconds)) <<
                " seconds to execute at " <<
                    beast::Debug::getSourceLocation (fileName, lineNumber);
    }
}

} // ripple

#endif
