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

#ifndef RIPPLE_PROTOCOL_QUALITY_H_INCLUDED
#define RIPPLE_PROTOCOL_QUALITY_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/AmountConversions.h>
#include <ripple/protocol/STAmount.h>

#include <algorithm>
#include <cstdint>
#include <ostream>

namespace ripple {

/** Represents a pair of input and output currencies.

    The input currency can be converted to the output
    currency by multiplying by the rate, represented by
    Quality.

    For offers, "in" is always TakerPays and "out" is
    always TakerGets.
*/
template <class In, class Out>
struct TAmounts
{
    TAmounts() = default;

    TAmounts(beast::Zero, beast::Zero) : in(beast::zero), out(beast::zero)
    {
    }

    TAmounts(In const& in_, Out const& out_) : in(in_), out(out_)
    {
    }

    /** Returns `true` if either quantity is not positive. */
    bool
    empty() const noexcept
    {
        return in <= beast::zero || out <= beast::zero;
    }

    TAmounts&
    operator+=(TAmounts const& rhs)
    {
        in += rhs.in;
        out += rhs.out;
        return *this;
    }

    TAmounts&
    operator-=(TAmounts const& rhs)
    {
        in -= rhs.in;
        out -= rhs.out;
        return *this;
    }

    In in;
    Out out;
};

using Amounts = TAmounts<STAmount, STAmount>;

template <class In, class Out>
bool
operator==(TAmounts<In, Out> const& lhs, TAmounts<In, Out> const& rhs) noexcept
{
    return lhs.in == rhs.in && lhs.out == rhs.out;
}

template <class In, class Out>
bool
operator!=(TAmounts<In, Out> const& lhs, TAmounts<In, Out> const& rhs) noexcept
{
    return !(lhs == rhs);
}

//------------------------------------------------------------------------------

// Ripple specific constant used for parsing qualities and other things
#define QUALITY_ONE 1'000'000'000

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
    using value_type = std::uint64_t;

    static const int minTickSize = 3;
    static const int maxTickSize = 16;

private:
    // This has the same representation as STAmount, see the comment on the
    // STAmount. However, this class does not alway use the canonical
    // representation. In particular, the increment and decrement operators may
    // cause a non-canonical representation.
    value_type m_value;

public:
    Quality() = default;

    /** Create a quality from the integer encoding of an STAmount */
    explicit Quality(std::uint64_t value);

    /** Create a quality from the ratio of two amounts. */
    explicit Quality(Amounts const& amount);

    /** Create a quality from the ratio of two amounts. */
    template <class In, class Out>
    Quality(Out const& out, In const& in)
        : Quality(Amounts(toSTAmount(in), toSTAmount(out)))
    {
    }

    /** Advances to the next higher quality level. */
    /** @{ */
    Quality&
    operator++();

    Quality
    operator++(int);
    /** @} */

    /** Advances to the next lower quality level. */
    /** @{ */
    Quality&
    operator--();

    Quality
    operator--(int);
    /** @} */

    /** Returns the quality as STAmount. */
    STAmount
    rate() const
    {
        return amountFromQuality(m_value);
    }

    /** Returns the quality rounded up to the specified number
        of decimal digits.
    */
    Quality
    round(int tickSize) const;

    /** Returns the scaled amount with in capped.
        Math is avoided if the result is exact. The output is clamped
        to prevent money creation.
    */
    Amounts
    ceil_in(Amounts const& amount, STAmount const& limit) const;

    template <class In, class Out>
    TAmounts<In, Out>
    ceil_in(TAmounts<In, Out> const& amount, In const& limit) const
    {
        if (amount.in <= limit)
            return amount;

        // Use the existing STAmount implementation for now, but consider
        // replacing with code specific to IOUAMount and XRPAmount
        Amounts stAmt(toSTAmount(amount.in), toSTAmount(amount.out));
        STAmount stLim(toSTAmount(limit));
        auto const stRes = ceil_in(stAmt, stLim);
        return TAmounts<In, Out>(
            toAmount<In>(stRes.in), toAmount<Out>(stRes.out));
    }

    /** Returns the scaled amount with out capped.
        Math is avoided if the result is exact. The input is clamped
        to prevent money creation.
    */
    Amounts
    ceil_out(Amounts const& amount, STAmount const& limit) const;

    template <class In, class Out>
    TAmounts<In, Out>
    ceil_out(TAmounts<In, Out> const& amount, Out const& limit) const
    {
        if (amount.out <= limit)
            return amount;

        // Use the existing STAmount implementation for now, but consider
        // replacing with code specific to IOUAMount and XRPAmount
        Amounts stAmt(toSTAmount(amount.in), toSTAmount(amount.out));
        STAmount stLim(toSTAmount(limit));
        auto const stRes = ceil_out(stAmt, stLim);
        return TAmounts<In, Out>(
            toAmount<In>(stRes.in), toAmount<Out>(stRes.out));
    }

    /** Returns `true` if lhs is lower quality than `rhs`.
        Lower quality means the taker receives a worse deal.
        Higher quality is better for the taker.
    */
    friend bool
    operator<(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.m_value > rhs.m_value;
    }

    friend bool
    operator>(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.m_value < rhs.m_value;
    }

    friend bool
    operator<=(Quality const& lhs, Quality const& rhs) noexcept
    {
        return !(lhs > rhs);
    }

    friend bool
    operator>=(Quality const& lhs, Quality const& rhs) noexcept
    {
        return !(lhs < rhs);
    }

    friend bool
    operator==(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.m_value == rhs.m_value;
    }

    friend bool
    operator!=(Quality const& lhs, Quality const& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend std::ostream&
    operator<<(std::ostream& os, Quality const& quality)
    {
        os << quality.m_value;
        return os;
    }

    // return the relative distance (relative error) between two qualities. This
    // is used for testing only. relative distance is abs(a-b)/min(a,b)
    friend double
    relativeDistance(Quality const& q1, Quality const& q2)
    {
        assert(q1.m_value > 0 && q2.m_value > 0);

        if (q1.m_value == q2.m_value)  // make expected common case fast
            return 0;

        auto const [minV, maxV] = std::minmax(q1.m_value, q2.m_value);

        auto mantissa = [](std::uint64_t rate) {
            return rate & ~(255ull << (64 - 8));
        };
        auto exponent = [](std::uint64_t rate) {
            return static_cast<int>(rate >> (64 - 8)) - 100;
        };

        auto const minVMantissa = mantissa(minV);
        auto const maxVMantissa = mantissa(maxV);
        auto const expDiff = exponent(maxV) - exponent(minV);

        double const minVD = static_cast<double>(minVMantissa);
        double const maxVD = expDiff ? maxVMantissa * pow(10, expDiff)
                                     : static_cast<double>(maxVMantissa);

        // maxVD and minVD are scaled so they have the same exponents. Dividing
        // cancels out the exponents, so we only need to deal with the (scaled)
        // mantissas
        return (maxVD - minVD) / minVD;
    }
};

/** Calculate the quality of a two-hop path given the two hops.
    @param lhs  The first leg of the path: input to intermediate.
    @param rhs  The second leg of the path: intermediate to output.
*/
Quality
composed_quality(Quality const& lhs, Quality const& rhs);

}  // namespace ripple

#endif
