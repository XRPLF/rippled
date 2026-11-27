#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>

namespace xrpl {

/**
 * Sampling function using exponential decay to provide a continuous value.
 * @tparam The number of seconds in the decay window.
 */
template <int Window, typename Clock>
class DecayingSample
{
public:
    using ValueType = Clock::duration::rep;
    using TimePoint = Clock::time_point;

    DecayingSample() = delete;

    /**
     * @param now Start time of DecayingSample.
     */
    explicit DecayingSample(TimePoint now) : value_(ValueType()), when_(now)
    {
    }

    /**
     * Add a new sample.
     * The value is first aged according to the specified time.
     */
    ValueType
    add(ValueType value, TimePoint now)
    {
        decay(now);
        value_ += value;
        return value_ / Window;
    }

    /**
     * Retrieve the current value in normalized units.
     * The samples are first aged according to the specified time.
     */
    ValueType
    value(TimePoint now)
    {
        decay(now);
        return value_ / Window;
    }

private:
    // Apply exponential decay based on the specified time.
    void
    decay(TimePoint now)
    {
        if (now == when_)
            return;

        if (value_ != ValueType())
        {
            std::size_t elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - when_).count();

            // A span larger than four times the window decays the
            // value to an insignificant amount so just reset it.
            //
            if (elapsed > 4 * Window)
            {
                value_ = ValueType();
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
    ValueType value_;

    // Last time the aging function was applied
    TimePoint when_;
};

//------------------------------------------------------------------------------

/**
 * Sampling function using exponential decay to provide a continuous value.
 * @tparam HalfLife The half life of a sample, in seconds.
 */
template <int HalfLife, class Clock>
class DecayWindow
{
public:
    using TimePoint = Clock::time_point;

    explicit DecayWindow(TimePoint now) : when_(now)
    {
    }

    void
    add(double value, TimePoint now)
    {
        decay(now);
        value_ += value;
    }

    double
    value(TimePoint now)
    {
        decay(now);
        return value_ / HalfLife;
    }

private:
    static_assert(HalfLife > 0, "half life must be positive");

    void
    decay(TimePoint now)
    {
        if (now <= when_)
            return;
        using namespace std::chrono;
        auto const elapsed = duration<double>(now - when_).count();
        value_ *= std::pow(2.0, -elapsed / HalfLife);
        when_ = now;
    }

    double value_{0};
    TimePoint when_;
};

}  // namespace xrpl
