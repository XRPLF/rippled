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

#ifndef RIPPLE_ALGORITHM_DECAYINGSAMPLE_H_INCLUDED
#define RIPPLE_ALGORITHM_DECAYINGSAMPLE_H_INCLUDED

namespace ripple {

/** Sampling function using exponential decay to provide a continuous value. */
template <int Window,
          typename Value = int,
          typename Elapsed = int>
class DecayingSample
{
public:
    typedef Value   value_type;
    typedef Elapsed elapsed_type;

    /** Create a default constructed sample. */
    DecayingSample ()
        : m_value (value_type())
        , m_when (elapsed_type())
    {
    }

    /** Add a new sample.
        The value is first aged according to the specified time.
    */
    Value add (value_type value, elapsed_type now)
    {
        decay (now);
        m_value += value;
        return m_value / Window;
    }

    /** Retrieve the current value in normalized units.
        The samples are first aged according to the specified time.
    */
    Value value (elapsed_type now)
    {
        decay (now);
        return m_value / Window;
    }

private:
    // Apply exponential decay based on the specified time.
    void decay (elapsed_type now)
    {
        if (now == m_when)
            return;

        if (m_value != value_type())
        {
            elapsed_type n (now - m_when);

            // A span larger than four times the window decays the
            // value to an insignificant amount so just reset it.
            //
            if (n > 4 * Window)
                m_value = value_type();
            else
            {
                while (n--)
                    m_value -= (m_value + Window - 1) / Window;
            }
        }

        m_when = now;
    }

    // Current value in exponential units
    value_type m_value;

    // Last time the aging function was applied
    elapsed_type m_when;
};

}

#endif
