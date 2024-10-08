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

#include <xrpl/basics/IOUAmount.h>
#include <xrpl/basics/XRPAmount.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/STAmount.h>

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
    // STAmount. However, this class does not always use the canonical
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
    explicit Quality(TAmounts<In, Out> const& amount)
        : Quality(Amounts(toSTAmount(amount.in), toSTAmount(amount.out)))
    {
    }

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
    [[nodiscard]] Amounts
    ceil_in(Amounts const& amount, STAmount const& limit) const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceil_in(TAmounts<In, Out> const& amount, In const& limit) const;

    // Some of the underlying rounding functions called by ceil_in() ignored
    // low order bits that could influence rounding decisions.  This "strict"
    // method uses underlying functions that pay attention to all the bits.
    [[nodiscard]] Amounts
    ceil_in_strict(Amounts const& amount, STAmount const& limit, bool roundUp)
        const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceil_in_strict(
        TAmounts<In, Out> const& amount,
        In const& limit,
        bool roundUp) const;

    /** Returns the scaled amount with out capped.
        Math is avoided if the result is exact. The input is clamped
        to prevent money creation.
    */
    [[nodiscard]] Amounts
    ceil_out(Amounts const& amount, STAmount const& limit) const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceil_out(TAmounts<In, Out> const& amount, Out const& limit) const;

    // Some of the underlying rounding functions called by ceil_out() ignored
    // low order bits that could influence rounding decisions.  This "strict"
    // method uses underlying functions that pay attention to all the bits.
    [[nodiscard]] Amounts
    ceil_out_strict(Amounts const& amount, STAmount const& limit, bool roundUp)
        const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceil_out_strict(
        TAmounts<In, Out> const& amount,
        Out const& limit,
        bool roundUp) const;

private:
    // The ceil_in and ceil_out methods that deal in TAmount all convert
    // their arguments to STAoumout and convert the result back to TAmount.
    // This helper function takes care of all the conversion operations.
    template <
        class In,
        class Out,
        class Lim,
        typename FnPtr,
        std::same_as<bool>... Round>
    [[nodiscard]] TAmounts<In, Out>
    ceil_TAmounts_helper(
        TAmounts<In, Out> const& amount,
        Lim const& limit,
        Lim const& limit_cmp,
        FnPtr ceil_function,
        Round... round) const;

public:
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
        ASSERT(
            q1.m_value > 0 && q2.m_value > 0,
            "ripple::Quality::relativeDistance : minimum inputs");

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

template <
    class In,
    class Out,
    class Lim,
    typename FnPtr,
    std::same_as<bool>... Round>
TAmounts<In, Out>
Quality::ceil_TAmounts_helper(
    TAmounts<In, Out> const& amount,
    Lim const& limit,
    Lim const& limit_cmp,
    FnPtr ceil_function,
    Round... roundUp) const
{
    if (limit_cmp <= limit)
        return amount;

    // Use the existing STAmount implementation for now, but consider
    // replacing with code specific to IOUAMount and XRPAmount
    Amounts stAmt(toSTAmount(amount.in), toSTAmount(amount.out));
    STAmount stLim(toSTAmount(limit));
    Amounts const stRes = ((*this).*ceil_function)(stAmt, stLim, roundUp...);
    return TAmounts<In, Out>(toAmount<In>(stRes.in), toAmount<Out>(stRes.out));
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceil_in(TAmounts<In, Out> const& amount, In const& limit) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*ceil_in_fn_ptr)(
        Amounts const&, STAmount const&) const = &Quality::ceil_in;

    return ceil_TAmounts_helper(amount, limit, amount.in, ceil_in_fn_ptr);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceil_in_strict(
    TAmounts<In, Out> const& amount,
    In const& limit,
    bool roundUp) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*ceil_in_fn_ptr)(
        Amounts const&, STAmount const&, bool) const = &Quality::ceil_in_strict;

    return ceil_TAmounts_helper(
        amount, limit, amount.in, ceil_in_fn_ptr, roundUp);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceil_out(TAmounts<In, Out> const& amount, Out const& limit) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*ceil_out_fn_ptr)(
        Amounts const&, STAmount const&) const = &Quality::ceil_out;

    return ceil_TAmounts_helper(amount, limit, amount.out, ceil_out_fn_ptr);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceil_out_strict(
    TAmounts<In, Out> const& amount,
    Out const& limit,
    bool roundUp) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*ceil_out_fn_ptr)(
        Amounts const&, STAmount const&, bool) const =
        &Quality::ceil_out_strict;

    return ceil_TAmounts_helper(
        amount, limit, amount.out, ceil_out_fn_ptr, roundUp);
}

/** Calculate the quality of a two-hop path given the two hops.
    @param lhs  The first leg of the path: input to intermediate.
    @param rhs  The second leg of the path: intermediate to output.
*/
Quality
composed_quality(Quality const& lhs, Quality const& rhs);

}  // namespace ripple

#endif
