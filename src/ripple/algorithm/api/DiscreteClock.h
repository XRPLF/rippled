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

#ifndef RIPPLE_ALGORITHM_DISCRETECLOCK_H_INCLUDED
#define RIPPLE_ALGORITHM_DISCRETECLOCK_H_INCLUDED

#include "beast/beast/StaticAssert.h"
#include "beast/beast/type_traits/IsIntegral.h"
#include "beast/beast/chrono/RelativeTime.h"

namespace ripple {

/** Interface for an elapsed time clock that uses integral units. */
template <typename ElapsedType>
class DiscreteClock
{
public:
    /** Source of time for the discrete clock. */
    class Source
    {
    public:
        typedef ElapsedType elapsed_type;
        typedef DiscreteClock <elapsed_type> DiscreteClockType;
        virtual ElapsedType operator() () = 0;
    };

    /** Integral elapsed time type relative to an unspecified past event. */
    typedef ElapsedType elapsed_type;

    DiscreteClock (Source& source)
        : m_source (source)
    {
        static_bassert (IsIntegral <elapsed_type>::value);
    }

    /** Returns the elapsed time in discrete units.
        The elapsed time is relative to an unspecified event.
    */
    elapsed_type operator() () const
    {
        return m_source();
    }

private:
    Source& m_source;
};

//------------------------------------------------------------------------------

/** Seconds-based clock that uses elapsed time from startup as a time source. */
class SimpleMonotonicClock : public DiscreteClock <int>::Source
{
public:
    elapsed_type operator() ()
    {
        return static_cast <elapsed_type> (
            RelativeTime::fromStartup().inSeconds());
    }
};

//------------------------------------------------------------------------------

/** A manually-operated clock.
    This can be useful for unit tests.
*/
class ManualClock : public DiscreteClock <int>::Source
{
private:
    elapsed_type m_now;

public:
    ManualClock ()
        : m_now (elapsed_type())
    {
    }

    elapsed_type& now()
    {
        return m_now;
    }

    elapsed_type now() const
    {
        return m_now;
    }

    elapsed_type operator() ()
    {
        return now();
    }
};

}

#endif
