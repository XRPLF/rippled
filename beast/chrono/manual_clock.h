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

#ifndef BEAST_CHRONO_MANUAL_CLOCK_H_INCLUDED
#define BEAST_CHRONO_MANUAL_CLOCK_H_INCLUDED

#include <beast/chrono/abstract_clock.h>

namespace beast {

/** Manual clock implementation.
    This concrete class implements the @ref abstract_clock interface and
    allows the time to be advanced manually, mainly for the purpose of
    providing a clock in unit tests.
    @tparam The length of time, in seconds, corresponding to one tick.
*/
template <class Duration, bool IsSteady = true>
class manual_clock : public abstract_clock <Duration>
{
public:
    using typename abstract_clock <Duration>::rep;
    using typename abstract_clock <Duration>::duration;
    using typename abstract_clock <Duration>::time_point;

    explicit manual_clock (time_point const& t = time_point (Duration (0)))
        : m_now (t)
    {
    }

    bool is_steady () const
    {
        return IsSteady;
    }

    time_point now () const
    {
        return m_now;
    }

#if 0
    std::string to_string (time_point const& tp) const
    {
        std::stringstream ss;
        ss << tp.time_since_epoch() << " from start";
        return ss.str ();
    }
#endif

    /** Set the current time of the manual clock.
        Precondition:
            ! IsSteady || t > now()
    */
    void set (time_point const& t)
    {
        //if (IsSteady)
        m_now = t;
    }

    /** Convenience for setting the time using a duration in @ref rep units. */
    void set (rep v)
    {
        set (time_point (duration (v)));
    }

    /** Convenience for advancing the clock by one. */
    manual_clock& operator++ ()
    {
        m_now += duration (1);
        return *this;
    }

private:
    time_point m_now;
};

}

#endif
