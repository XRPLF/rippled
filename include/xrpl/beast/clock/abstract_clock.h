#pragma once

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
class AbstractClock
{
public:
    using rep = typename Clock::rep;
    using period = typename Clock::period;
    using duration = typename Clock::duration;
    using time_point = typename Clock::time_point;
    using clock_type = Clock;

    static bool const is_steady = Clock::is_steady;  // NOLINT(readability-identifier-naming)

    virtual ~AbstractClock() = default;
    AbstractClock() = default;
    AbstractClock(AbstractClock const&) = default;

    /** Returns the current time. */
    [[nodiscard]] virtual time_point
    now() const = 0;
};

//------------------------------------------------------------------------------

namespace detail {

template <class Facade, class Clock>
struct AbstractClockWrapper : public AbstractClock<Facade>
{
    explicit AbstractClockWrapper() = default;

    using typename AbstractClock<Facade>::duration;
    using typename AbstractClock<Facade>::time_point;

    [[nodiscard]] time_point
    now() const override
    {
        return Clock::now();
    }
};

}  // namespace detail

//------------------------------------------------------------------------------

/** Returns a global instance of an abstract clock.
    @tparam Facade A type meeting these requirements:
        http://en.cppreference.com/w/cpp/concept/Clock
    @tparam Clock The actual concrete clock to use.
*/
template <class Facade, class Clock = Facade>
AbstractClock<Facade>&
getAbstractClock()
{
    static detail::AbstractClockWrapper<Facade, Clock> kClock;
    return kClock;
}

}  // namespace beast
