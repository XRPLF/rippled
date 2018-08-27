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

    This makes now() a member function instead of a static member, so
    an instance of the class can be dependency injected, facilitating
    unit tests where time may be controlled.

    An abstract_clock inherits all the nested types of the Clock
    template parameter.

    Example:

    @code

    struct Implementation
    {
        using clock_type = abstract_clock <std::chrono::steady_clock>;
        clock_type& clock_;
        explicit Implementation (clock_type& clock)
            : clock_(clock)
        {
        }
    };

    @endcode

    @tparam Clock A type meeting these requirements:
        http://en.cppreference.com/w/cpp/concept/Clock
*/
template <class Clock>
class abstract_clock
{
public:
    using rep = typename Clock::rep;
    using period = typename Clock::period;
    using duration = typename Clock::duration;
    using time_point = typename Clock::time_point;
    using clock_type = Clock;

    static bool const is_steady = Clock::is_steady;

    virtual ~abstract_clock() = default;
    abstract_clock() = default;
    abstract_clock(abstract_clock const&) = default;

    /** Returns the current time. */
    virtual time_point now() const = 0;
};

//------------------------------------------------------------------------------

namespace detail {

template <class Facade, class Clock>
struct abstract_clock_wrapper
    : public abstract_clock<Facade>
{
    explicit abstract_clock_wrapper() = default;

    using typename abstract_clock<Facade>::duration;
    using typename abstract_clock<Facade>::time_point;

    time_point
    now() const override
    {
        return Clock::now();
    }
};

}

//------------------------------------------------------------------------------

/** Returns a global instance of an abstract clock.
    @tparam Facade A type meeting these requirements:
        http://en.cppreference.com/w/cpp/concept/Clock
    @tparam Clock The actual concrete clock to use.
*/
template<class Facade, class Clock = Facade>
abstract_clock<Facade>&
get_abstract_clock()
{
    static detail::abstract_clock_wrapper<Facade, Clock> clock;
    return clock;
}

}

#endif
