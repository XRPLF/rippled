#pragma once

#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <utility>

namespace xrpl {

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

    TAmounts(beast::Zero, beast::Zero) : in(beast::kZero), out(beast::kZero)
    {
    }

    TAmounts(In in, Out out) : in(std::move(in)), out(std::move(out))
    {
    }

    /** Returns `true` if either quantity is not positive. */
    [[nodiscard]] bool
    empty() const noexcept
    {
        return in <= beast::kZero || out <= beast::kZero;
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

    In in{};
    Out out{};
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

// XRPL specific constant used for parsing qualities and other things
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

    static int const kMinTickSize = 3;
    static int const kMaxTickSize = 16;

private:
    // This has the same representation as STAmount, see the comment on the
    // STAmount. However, this class does not always use the canonical
    // representation. In particular, the increment and decrement operators may
    // cause a non-canonical representation.
    value_type value_;

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
    Quality(Out const& out, In const& in) : Quality(Amounts(toSTAmount(in), toSTAmount(out)))
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
    [[nodiscard]] STAmount
    rate() const
    {
        return amountFromQuality(value_);
    }

    /** Returns the quality rounded up to the specified number
        of decimal digits.
    */
    [[nodiscard]] Quality
    round(int tickSize) const;

    /** Returns the scaled amount with in capped.
        Math is avoided if the result is exact. The output is clamped
        to prevent money creation.
    */
    [[nodiscard]] Amounts
    ceilIn(Amounts const& amount, STAmount const& limit) const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilIn(TAmounts<In, Out> const& amount, In const& limit) const;

    // Some of the underlying rounding functions called by ceil_in() ignored
    // low order bits that could influence rounding decisions.  This "strict"
    // method uses underlying functions that pay attention to all the bits.
    [[nodiscard]] Amounts
    ceilInStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilInStrict(TAmounts<In, Out> const& amount, In const& limit, bool roundUp) const;

    /** Returns the scaled amount with out capped.
        Math is avoided if the result is exact. The input is clamped
        to prevent money creation.
    */
    [[nodiscard]] Amounts
    ceilOut(Amounts const& amount, STAmount const& limit) const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilOut(TAmounts<In, Out> const& amount, Out const& limit) const;

    // Some of the underlying rounding functions called by ceil_out() ignored
    // low order bits that could influence rounding decisions.  This "strict"
    // method uses underlying functions that pay attention to all the bits.
    [[nodiscard]] Amounts
    ceilOutStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const;

    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilOutStrict(TAmounts<In, Out> const& amount, Out const& limit, bool roundUp) const;

private:
    // The ceil_in and ceil_out methods that deal in TAmount all convert
    // their arguments to STAmount and convert the result back to TAmount.
    // This helper function takes care of all the conversion operations.
    template <class In, class Out, class Lim, typename FnPtr, std::same_as<bool>... Round>
    [[nodiscard]] TAmounts<In, Out>
    ceilTAmountsHelper(
        TAmounts<In, Out> const& amount,
        Lim const& limit,
        Lim const& limitCmp,
        FnPtr ceilFunction,
        Round... round) const;

public:
    /** Returns `true` if lhs is lower quality than `rhs`.
        Lower quality means the taker receives a worse deal.
        Higher quality is better for the taker.
    */
    friend bool
    operator<(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.value_ > rhs.value_;
    }

    friend bool
    operator>(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.value_ < rhs.value_;
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
        return lhs.value_ == rhs.value_;
    }

    friend bool
    operator!=(Quality const& lhs, Quality const& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend std::ostream&
    operator<<(std::ostream& os, Quality const& quality)
    {
        os << quality.value_;
        return os;
    }

    // return the relative distance (relative error) between two qualities. This
    // is used for testing only. relative distance is abs(a-b)/min(a,b)
    friend double
    relativeDistance(Quality const& q1, Quality const& q2)
    {
        XRPL_ASSERT(
            q1.value_ > 0 && q2.value_ > 0, "xrpl::Quality::relativeDistance : minimum inputs");

        if (q1.value_ == q2.value_)  // make expected common case fast
            return 0;

        auto const [minV, maxV] = std::minmax(q1.value_, q2.value_);

        auto mantissa = [](std::uint64_t rate) { return rate & ~(255ull << (64 - 8)); };
        auto exponent = [](std::uint64_t rate) { return static_cast<int>(rate >> (64 - 8)) - 100; };

        auto const minVMantissa = mantissa(minV);
        auto const maxVMantissa = mantissa(maxV);
        auto const expDiff = exponent(maxV) - exponent(minV);

        double const minVD = static_cast<double>(minVMantissa);
        double const maxVD =
            (expDiff != 0) ? maxVMantissa * pow(10, expDiff) : static_cast<double>(maxVMantissa);

        // maxVD and minVD are scaled so they have the same exponents. Dividing
        // cancels out the exponents, so we only need to deal with the (scaled)
        // mantissas
        return (maxVD - minVD) / minVD;
    }
};

template <class In, class Out, class Lim, typename FnPtr, std::same_as<bool>... Round>
TAmounts<In, Out>
Quality::ceilTAmountsHelper(
    TAmounts<In, Out> const& amount,
    Lim const& limit,
    Lim const& limitCmp,
    FnPtr ceilFunction,
    Round... roundUp) const
{
    if (limitCmp <= limit)
        return amount;

    // Use the existing STAmount implementation for now, but consider
    // replacing with code specific to IOUAMount and XRPAmount
    Amounts const stAmt(toSTAmount(amount.in), toSTAmount(amount.out));
    STAmount const stLim(toSTAmount(limit));
    Amounts const stRes = ((*this).*ceilFunction)(stAmt, stLim, roundUp...);
    return TAmounts<In, Out>(toAmount<In>(stRes.in), toAmount<Out>(stRes.out));
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceilIn(TAmounts<In, Out> const& amount, In const& limit) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*kCeilInFnPtr)(Amounts const&, STAmount const&) const =
        &Quality::ceilIn;

    return ceilTAmountsHelper(amount, limit, amount.in, kCeilInFnPtr);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceilInStrict(TAmounts<In, Out> const& amount, In const& limit, bool roundUp) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*kCeilInFnPtr)(Amounts const&, STAmount const&, bool) const =
        &Quality::ceilInStrict;

    return ceilTAmountsHelper(amount, limit, amount.in, kCeilInFnPtr, roundUp);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceilOut(TAmounts<In, Out> const& amount, Out const& limit) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*kCeilOutFnPtr)(Amounts const&, STAmount const&) const =
        &Quality::ceilOut;

    return ceil_TAmounts_helper(amount, limit, amount.out, kCeilOutFnPtr);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceilOutStrict(TAmounts<In, Out> const& amount, Out const& limit, bool roundUp) const
{
    // Construct a function pointer to the function we want to call.
    static constexpr Amounts (Quality::*kCeilOutFnPtr)(Amounts const&, STAmount const&, bool)
        const = &Quality::ceilOutStrict;

    return ceilTAmountsHelper(amount, limit, amount.out, kCeilOutFnPtr, roundUp);
}

/** Calculate the quality of a two-hop path given the two hops.
    @param lhs  The first leg of the path: input to intermediate.
    @param rhs  The second leg of the path: intermediate to output.
*/
Quality
composedQuality(Quality const& lhs, Quality const& rhs);

}  // namespace xrpl
