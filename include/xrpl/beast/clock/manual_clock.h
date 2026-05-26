#pragma once

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <chrono>

namespace beast {

/** Manual clock implementation.

    This concrete class implements the @ref abstract_clock interface and
    allows the time to be advanced manually, mainly for the purpose of
    providing a clock in unit tests.

    @tparam Clock A type meeting these requirements:
        http://en.cppreference.com/w/cpp/concept/Clock
*/
template <class Clock>
class ManualClock : public AbstractClock<Clock>
{
public:
    using typename AbstractClock<Clock>::rep;
    using typename AbstractClock<Clock>::duration;
    using typename AbstractClock<Clock>::time_point;

private:
    time_point now_;

public:
    explicit ManualClock(time_point const& now = time_point(duration(0))) : now_(now)
    {
    }

    [[nodiscard]] time_point
    now() const override
    {
        return now_;
    }

    /** Set the current time of the manual clock. */
    void
    set(time_point const& when)
    {
        XRPL_ASSERT(
            !Clock::is_steady || when >= now_,
            "beast::ManualClock::set(time_point) : forward input");
        now_ = when;
    }

    /** Convenience for setting the time in seconds from epoch. */
    template <class Integer>
    void
    set(Integer secondsFromEpoch)
    {
        set(time_point(duration(std::chrono::seconds(secondsFromEpoch))));
    }

    /** Advance the clock by a duration. */
    template <class Rep, class Period>
    void
    advance(std::chrono::duration<Rep, Period> const& elapsed)
    {
        XRPL_ASSERT(
            !Clock::is_steady || (now_ + elapsed) >= now_,
            "beast::ManualClock::advance(duration) : forward input");
        now_ += elapsed;
    }

    /** Convenience for advancing the clock by one second. */
    ManualClock&
    operator++()
    {
        advance(std::chrono::seconds(1));
        return *this;
    }
};

}  // namespace beast
