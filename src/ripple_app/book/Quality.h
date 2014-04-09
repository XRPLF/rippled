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

#ifndef RIPPLE_CORE_QUALITY_H_INCLUDED
#define RIPPLE_CORE_QUALITY_H_INCLUDED

#include "Amount.h"
#include "Amounts.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <ostream>

namespace ripple {
namespace core {

/** Represents the logical ratio of output currency to input currency.
    Internally this is stored using a custom floating point representation,
    as the inverse of the ratio, so that quality will be descending in
    a sequence of actual values that represent qualities.
*/
class Quality
{
public:
    // Type of the internal representation. Higher qualities
    // have lower unsigned integer representations.
    typedef std::uint64_t value_type;

private:
    value_type m_value;

public:
    Quality() = default;

    explicit
    Quality (std::uint64_t value)
        : m_value (value)
    {
    }

    /** Create a quality from the ratio of two amounts. */
    explicit
    Quality (Amounts const& amount)
        : m_value (Amount::getRate (amount.out, amount.in))
    {
    }

    /** Advances to the next higher quality level. */
    /** @{ */
    Quality&
    operator++()
    {
        assert (m_value > 0);
        --m_value;
        return *this;
    }

    Quality
    operator++ (int)
    {
        Quality prev (*this);
        --*this;
        return prev;
    }
    /** @} */

    /** Advances to the next lower quality level. */
    /** @{ */
    Quality&
    operator--()
    {
        assert (m_value < std::numeric_limits<value_type>::max());
        ++m_value;
        return *this;
    }

    Quality
    operator-- (int)
    {
        Quality prev (*this);
        ++*this;
        return prev;
    }
    /** @} */

    /** Returns `true` if lhs is lower quality than `rhs`.
        Lower quality means the taker receives a worse deal.
        Higher quality is better for the taker.
    */
    friend
    bool
    operator< (Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.m_value > rhs.m_value;
    }

    friend
    bool
    operator== (Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.m_value == rhs.m_value;
    }

    friend
    std::ostream&
    operator<< (std::ostream& os, Quality const& quality)
    {
        os << quality.m_value;
        return os;
    }

    /** Returns the quality as Amount. */
    Amount
    rate() const
    {
        return Amount::setRate (m_value);
    }

    /** Returns the scaled amount with in capped.
        Math is avoided if the result is exact. The output is clamped
        to prevent money creation.
    */
    Amounts
    ceil_in (Amounts const& amount, Amount const& limit) const
    {
        if (amount.in > limit)
        {
            // VFALCO TODO make mulRound avoid math when v2==one
#if 0
            Amounts result (limit, Amount::mulRound (
                limit, rate(), amount.out, true));
#else
            Amounts result (limit, Amount::divRound (
                limit, rate(), amount.out, true));
#endif
            // Clamp out
            if (result.out > amount.out)
                result.out = amount.out;
            return result;
        }

        return amount;
    }

    /** Returns the scaled amount with out capped.
        Math is avoided if the result is exact. The input is clamped
        to prevent money creation.
    */
    Amounts
    ceil_out (Amounts const& amount, Amount const& limit) const
    {
        if (amount.out > limit)
        {
            // VFALCO TODO make divRound avoid math when v2==one
#if 0
            Amounts result (Amount::divRound (
                limit, rate(), amount.in, true), limit);
#else
            Amounts result (Amount::mulRound (
                limit, rate(), amount.in, true), limit);
#endif
            // Clamp in
            if (result.in > amount.in)
                result.in = amount.in;
            return result;
        }
        return amount;
    }
};

inline
bool
operator!= (Quality const& lhs, Quality const& rhs) noexcept
{
    return ! (lhs == rhs);
}

}
}

#endif
