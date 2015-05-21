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

#ifndef RIPPLE_BASICS_DECAYINGSAMPLE_H_INCLUDED
#define RIPPLE_BASICS_DECAYINGSAMPLE_H_INCLUDED

#include <chrono>
#include <cmath>

namespace ripple {

/** Sampling function using exponential decay to provide a continuous value.
    @tparam The number of seconds in the decay window.
*/
template <int Window, typename Clock>
class DecayingSample
{
public:
    using value_type = typename Clock::duration::rep;
    using time_point = typename Clock::time_point;

    DecayingSample () = delete;

    /**
        @param now Start time of DecayingSample.
    */
    explicit DecayingSample (time_point now)
        : m_value (value_type())
        , m_when (now)
    {
    }

    /** Add a new sample.
        The value is first aged according to the specified time.
    */
    value_type add (value_type value, time_point now)
    {
        decay (now);
        m_value += value;
        return m_value / Window;
    }

    /** Retrieve the current value in normalized units.
        The samples are first aged according to the specified time.
    */
    value_type value (time_point now)
    {
        decay (now);
        return m_value / Window;
    }

private:
    // Apply exponential decay based on the specified time.
    void decay (time_point now)
    {
        if (now == m_when)
            return;

        if (m_value != value_type())
        {
            std::size_t elapsed = std::chrono::duration_cast<
                std::chrono::seconds>(now - m_when).count();

            // A span larger than four times the window decays the
            // value to an insignificant amount so just reset it.
            //
            if (elapsed > 4 * Window)
            {
                m_value = value_type();
            }
            else
            {
                while (elapsed--)
                    m_value -= (m_value + Window - 1) / Window;
            }
        }

        m_when = now;
    }

    // Current value in exponential units
    value_type m_value;

    // Last time the aging function was applied
    time_point m_when;
};

//------------------------------------------------------------------------------

/** Sampling function using exponential decay to provide a continuous value.
    @tparam HalfLife The half life of a sample, in seconds.
*/
template <int HalfLife, class Clock>
class DecayWindow
{
public:
    using time_point = typename Clock::time_point;

    explicit
    DecayWindow (time_point now)
        : value_(0)
        , when_(now)
    {
    }

    void
    add (double value, time_point now)
    {
        decay(now);
        value_ += value;
    }

    double
    value (time_point now)
    {
        decay(now);
        return value_ / HalfLife;
    }

private:
    static_assert(HalfLife > 0,
        "half life must be positive");

    void
    decay (time_point now)
    {
        if (now <= when_)
            return;
        using namespace std::chrono;
        auto const elapsed =
            duration<double>(now - when_).count();
        value_ *= std::pow(2.0, - elapsed / HalfLife);
        when_ = now;
    }

    double value_;
    time_point when_;
};

}

#endif
