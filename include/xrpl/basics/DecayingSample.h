#pragma once

#include <chrono>
#include <cmath>

namespace xrpl {

/** Sampling function using exponential decay to provide a continuous value.
    @tparam The number of seconds in the decay window.
*/
template <int Window, typename Clock>
class DecayingSample
{
public:
    using value_type = Clock::duration::rep;
    using time_point = Clock::time_point;

    DecayingSample() = delete;

    /**
        @param now Start time of DecayingSample.
    */
    explicit DecayingSample(time_point now) : value_(value_type()), when_(now)
    {
    }

    /** Add a new sample.
        The value is first aged according to the specified time.
    */
    value_type
    add(value_type value, time_point now)
    {
        decay(now);
        value_ += value;
        return value_ / Window;
    }

    /** Retrieve the current value in normalized units.
        The samples are first aged according to the specified time.
    */
    value_type
    value(time_point now)
    {
        decay(now);
        return value_ / Window;
    }

private:
    // Apply exponential decay based on the specified time.
    void
    decay(time_point now)
    {
        if (now == when_)
            return;

        if (value_ != value_type())
        {
            std::size_t elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - when_).count();

            // A span larger than four times the window decays the
            // value to an insignificant amount so just reset it.
            //
            if (elapsed > 4 * Window)
            {
                value_ = value_type();
            }
            else
            {
                for (; elapsed > 0; --elapsed)
                {
                    value_ -= (value_ + Window - 1) / Window;
                }
            }
        }

        when_ = now;
    }

    // Current value in exponential units
    value_type value_;

    // Last time the aging function was applied
    time_point when_;
};

//------------------------------------------------------------------------------

/** Sampling function using exponential decay to provide a continuous value.
    @tparam HalfLife The half life of a sample, in seconds.
*/
template <int HalfLife, class Clock>
class DecayWindow
{
public:
    using time_point = Clock::time_point;

    explicit DecayWindow(time_point now) : when_(now)
    {
    }

    void
    add(double value, time_point now)
    {
        decay(now);
        value_ += value;
    }

    double
    value(time_point now)
    {
        decay(now);
        return value_ / HalfLife;
    }

private:
    static_assert(HalfLife > 0, "half life must be positive");

    void
    decay(time_point now)
    {
        if (now <= when_)
            return;
        using namespace std::chrono;
        auto const elapsed = duration<double>(now - when_).count();
        value_ *= std::pow(2.0, -elapsed / HalfLife);
        when_ = now;
    }

    double value_{0};
    time_point when_;
};

}  // namespace xrpl
