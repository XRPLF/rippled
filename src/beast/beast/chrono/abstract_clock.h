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

#ifndef BEAST_CHRONO_ABSTRACT_CLOCK_H_INCLUDED
#define BEAST_CHRONO_ABSTRACT_CLOCK_H_INCLUDED

#include <chrono>
#include <string>

namespace beast {

/** Abstract interface to a clock.

    The abstract clock interface allows a dependency injection to take
    place so that the choice of implementation can be made at run-time
    instead of compile time. The trade-off is that the Duration used to
    represent the clock must be chosen at compile time and cannot be
    changed. This includes both the choice of representation (integers
    for example) and the period in ticks corresponding to one second.

    Example:

    @code

    struct Implementation
    {
        abstract_clock <std::chrono::seconds>& m_clock;

        // Dependency injection
        //
        explicit Implementation (
            abstract_clock <std::chrono::seconds>& clock)
            : m_clock (clock)
        {
        }
    };

    @endcode

    @tparam The length of time, in seconds, corresponding to one tick.
*/
template <class Duration>
class abstract_clock
{
public:
    typedef typename Duration::rep rep;
    typedef typename Duration::period period;
    typedef Duration duration;
    typedef std::chrono::time_point <
        abstract_clock, duration> time_point;

    virtual ~abstract_clock () { }

    /** Returns `true` if this is a steady clock. */
    virtual bool is_steady () const = 0;

    /** Returns the current time. */
    virtual time_point now () const = 0;

#if 0
    /** Convert the specified time point to a string. */
    /** @{ */
    //virtual std::string to_string (time_point const& tp) const = 0;

    template <class Duration2>
    std::string to_string (
        std::chrono::time_point <abstract_clock, Duration2> const& tp) const
    {
        return to_string (
            std::chrono::time_point_cast <Duration> (tp));
    }
    /** @} */
#endif

    /** Returning elapsed ticks since the epoch. */
    rep elapsed () const
    {
        return now().time_since_epoch().count();
    }
};

//------------------------------------------------------------------------------

namespace detail {

template <class TrivialClock, class Duration>
struct basic_abstract_clock_wrapper : public abstract_clock <Duration>
{
    using typename abstract_clock <Duration>::duration;
    using typename abstract_clock <Duration>::time_point;

    bool is_steady () const
    {
        return TrivialClock::is_steady;
    }

    time_point now () const
    {
        return time_point (duration (
            std::chrono::duration_cast <duration> (
                TrivialClock::now().time_since_epoch ()).count ()));
    }
};

template <class TrivialClock, class Duration>
struct abstract_clock_wrapper
    : public basic_abstract_clock_wrapper <TrivialClock, Duration>
{
    // generic conversion displays the duration
    /*
    std::string to_string (typename basic_abstract_clock_wrapper <
        TrivialClock, Duration>::time_point const& tp) const
    {
        std::stringstream ss;
        ss << tp.time_since_epoch();
        return ss.str ();
    }
    */
};

/*
template <class Duration>
struct abstract_clock_wrapper <std::chrono::system_clock, Duration>
    : public basic_abstract_clock_wrapper <std::chrono::system_clock, Duration>
{
    typedef std::chrono::system_clock clock_type;
    std::string to_string (time_point const& tp)
    {
        std::stringstream ss;
        ss << clock_type::time_point (tp.time_since_epoch ());
        return ss.str ();
    }
};
*/

}

//------------------------------------------------------------------------------

/** Retrieve a discrete clock for a type implementing the Clock concept.
    The interface is created as an object with static storage duration.
*/
template <class TrivialClock, class Duration>
abstract_clock <Duration>& get_abstract_clock ()
{
    static detail::abstract_clock_wrapper <
        TrivialClock, Duration> clock;
    return clock;
}

}

#endif
