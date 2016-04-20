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

#include <ripple/beast/clock/abstract_clock.h>
#include <cassert>

namespace beast {

/** Manual clock implementation.

    This concrete class implements the @ref abstract_clock interface and
    allows the time to be advanced manually, mainly for the purpose of
    providing a clock in unit tests.

    @tparam Clock A type meeting these requirements:
        http://en.cppreference.com/w/cpp/concept/Clock
*/
template <class Clock>
class manual_clock
    : public abstract_clock<Clock>
{
public:
    using typename abstract_clock<Clock>::rep;
    using typename abstract_clock<Clock>::duration;
    using typename abstract_clock<Clock>::time_point;

private:
    time_point now_;

public:
    explicit
    manual_clock (time_point const& now = time_point(duration(0)))
        : now_(now)
    {
    }

    time_point
    now() const override
    {
        return now_;
    }

    /** Set the current time of the manual clock. */
    void
    set (time_point const& when)
    {
        assert(! Clock::is_steady || when >= now_);
        now_ = when;
    }

    /** Convenience for setting the time in seconds from epoch. */
    template <class Integer>
    void
    set(Integer seconds_from_epoch)
    {
        set(time_point(duration(
            std::chrono::seconds(seconds_from_epoch))));
    }

    /** Advance the clock by a duration. */
    template <class Rep, class Period>
    void
    advance(std::chrono::duration<Rep, Period> const& elapsed)
    {
        assert(! Clock::is_steady ||
            (now_ + elapsed) >= now_);
        now_ += elapsed;
    }

    /** Convenience for advancing the clock by one second. */
    manual_clock&
    operator++ ()
    {
        advance(std::chrono::seconds(1));
        return *this;
    }
};

}

#endif
