/** @file
 *  Defines `Quality` and `TAmounts`, the core exchange-rate abstractions
 *  used by XRPL's on-ledger decentralized exchange (DEX).
 *
 *  `Quality` is the sortable representation of a currency exchange rate.
 *  The offer-crossing engine — ranking offers, scaling partial fills, and
 *  composing multi-hop paths — is expressed entirely in terms of these types.
 *
 *  @see QualityFunction.h for the continuous AMM price-function extension.
 */

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

/** A typed pair of input and output amounts representing one side of a trade.
 *
 *  For offers on the DEX, `in` is always `TakerPays` and `out` is always
 *  `TakerGets`.  The template parameters allow instantiation over
 *  `STAmount`, `IOUAmount`, `XRPAmount`, and `MPTAmount`.
 *
 *  @tparam In   Type of the input (paying) amount.
 *  @tparam Out  Type of the output (receiving) amount.
 */
template <class In, class Out>
struct TAmounts
{
    TAmounts() = default;

    /** Construct a zero-valued pair. */
    TAmounts(beast::Zero, beast::Zero) : in(beast::kZERO), out(beast::kZERO)
    {
    }

    /** Construct from explicit in and out amounts.
     *
     *  @param in   The input (TakerPays) amount.
     *  @param out  The output (TakerGets) amount.
     */
    TAmounts(In in, Out out) : in(std::move(in)), out(std::move(out))
    {
    }

    /** Returns `true` if either quantity is not positive.
     *
     *  Used by the offer-crossing engine to skip exhausted or invalid offers
     *  without further computation.
     */
    [[nodiscard]] bool
    empty() const noexcept
    {
        return in <= beast::kZERO || out <= beast::kZERO;
    }

    /** Adds `rhs` component-wise to this pair.
     *
     *  @param rhs  The amounts to add.
     *  @return Reference to `*this`.
     */
    TAmounts&
    operator+=(TAmounts const& rhs)
    {
        in += rhs.in;
        out += rhs.out;
        return *this;
    }

    /** Subtracts `rhs` component-wise from this pair.
     *
     *  @param rhs  The amounts to subtract.
     *  @return Reference to `*this`.
     */
    TAmounts&
    operator-=(TAmounts const& rhs)
    {
        in -= rhs.in;
        out -= rhs.out;
        return *this;
    }

    In in{};   /**< Input (TakerPays) amount. */
    Out out{}; /**< Output (TakerGets) amount. */
};

/** Canonical `TAmounts` alias used by the `STAmount`-based offer-crossing path. */
using Amounts = TAmounts<STAmount, STAmount>;

/** Returns `true` when both sides of two `TAmounts` pairs are equal. */
template <class In, class Out>
bool
operator==(TAmounts<In, Out> const& lhs, TAmounts<In, Out> const& rhs) noexcept
{
    return lhs.in == rhs.in && lhs.out == rhs.out;
}

/** Returns `true` when either side of two `TAmounts` pairs differs. */
template <class In, class Out>
bool
operator!=(TAmounts<In, Out> const& lhs, TAmounts<In, Out> const& rhs) noexcept
{
    return !(lhs == rhs);
}

//------------------------------------------------------------------------------

/** Unity exchange rate (1:1), scaled to XRPL's 9-decimal fixed-point precision.
 *
 *  Appears throughout offer parsing and fee calculations wherever a 1:1
 *  exchange rate must be expressed as a raw integer.
 */
#define QUALITY_ONE 1'000'000'000

/** The exchange rate of an offer, stored as an inverted packed floating-point
 *  integer so that higher-quality offers sort first under plain integer comparison.
 *
 *  A `Quality` encodes the ratio `out / in` (TakerGets / TakerPays): how much
 *  output the taker receives per unit of input.  Higher quality is better for
 *  the taker (more output per unit of input).
 *
 *  The internal `uint64_t` uses the same bit layout as `STAmount` IOU encoding:
 *  the top 8 bits hold a biased exponent (stored value = actual exponent + 100)
 *  and the lower 56 bits hold an unsigned mantissa.  Critically, the integer
 *  value is **inverted** relative to the economic concept — a *higher* quality
 *  corresponds to a *lower* `uint64_t` — so that ascending integer order in the
 *  ledger's offer directories corresponds to descending quality, allowing the
 *  best offers to be processed first.
 *
 *  @note The increment/decrement operators navigate the discrete floating-point
 *      grid by modifying `value_` by one ULP.  The representation may become
 *      non-canonical after such operations.
 *
 *  @see composedQuality() for two-hop path composition.
 *  @see QualityFunction.h for the continuous AMM extension of this type.
 */
class Quality
{
public:
    /** Underlying storage type.  Higher qualities have lower integer values. */
    using value_type = std::uint64_t;

    /** Minimum valid tick size (significant decimal digits) for `round()`. */
    static int const kMIN_TICK_SIZE = 3;

    /** Maximum valid tick size (significant decimal digits) for `round()`. */
    static int const kMAX_TICK_SIZE = 16;

private:
    // Packed 64-bit encoding: bits [63:56] = biased exponent (actual + 100),
    // bits [55:0] = mantissa.  Identical to the STAmount IOU wire format.
    // May be non-canonical after operator++ / operator--.
    value_type value_;

public:
    Quality() = default;

    /** Construct from a raw packed integer in STAmount encoding.
     *
     *  The top 8 bits are the biased exponent (actual exponent + 100) and
     *  the bottom 56 bits are the mantissa.  Higher integers denote lower
     *  (worse) quality because the internal ordering is inverted.
     *
     *  @param value  Packed 64-bit quality value.
     */
    explicit Quality(std::uint64_t value);

    /** Construct from an `STAmount` in/out pair encoding `out / in`.
     *
     *  Calls `getRate(amount.out, amount.in)` to produce the packed value.
     *  Neither side should be zero.
     *
     *  @param amount  Offer amounts: `in` = TakerPays, `out` = TakerGets.
     */
    explicit Quality(Amounts const& amount);

    /** Construct from a typed in/out pair by converting to `STAmount` first.
     *
     *  @tparam In   Input amount type (e.g., `XRPAmount`, `IOUAmount`).
     *  @tparam Out  Output amount type.
     *  @param amount  The typed offer amounts.
     */
    template <class In, class Out>
    explicit Quality(TAmounts<In, Out> const& amount)
        : Quality(Amounts(toSTAmount(amount.in), toSTAmount(amount.out)))
    {
    }

    /** Construct from explicit out and in amounts by converting to `STAmount`.
     *
     *  @tparam In   Input amount type.
     *  @tparam Out  Output amount type.
     *  @param out  The output (TakerGets) amount.
     *  @param in   The input (TakerPays) amount.
     */
    template <class In, class Out>
    Quality(Out const& out, In const& in) : Quality(Amounts(toSTAmount(in), toSTAmount(out)))
    {
    }

    /** Advance to the next higher quality level.
     *
     *  Because the internal encoding is inverted, this decrements the stored
     *  integer by one ULP.  Used during offer-book traversal to step the
     *  crossing price up by the smallest representable increment.
     *
     *  @pre `value_ > 0`; underflow is asserted.
     */
    /** @{ */
    Quality&
    operator++();

    Quality
    operator++(int);
    /** @} */

    /** Retreat to the next lower quality level.
     *
     *  Because the internal encoding is inverted, this increments the stored
     *  integer by one ULP.
     *
     *  @pre `value_ < UINT64_MAX`; overflow is asserted.
     */
    /** @{ */
    Quality&
    operator--();

    Quality
    operator--(int);
    /** @} */

    /** Decode the packed quality value into an `STAmount` exchange rate.
     *
     *  The returned amount represents the rate `out / in` in the IOU
     *  floating-point format.  Callers use this when passing the quality
     *  to `mulRound` / `divRound` for proportional scaling.
     *
     *  @return The exchange rate as an `STAmount`.
     */
    [[nodiscard]] STAmount
    rate() const
    {
        return amountFromQuality(value_);
    }

    /** Round the quality's mantissa up to `tickSize` significant decimal digits.
     *
     *  Used for tick-size enforcement: coarsens the price grid so that offers
     *  differing only in low-order digits are treated as equivalent.  Rounding
     *  is always upward (ceiling), which makes the encoded rate slightly higher
     *  (worse for the taker) and prevents a rounded quality from being mistakenly
     *  ranked better than the original.
     *
     *  @param tickSize  Number of significant digits to retain.  Must be in
     *      `[kMIN_TICK_SIZE, kMAX_TICK_SIZE]`; enforcement is the caller's
     *      responsibility.
     *  @return A new `Quality` with a rounded-up mantissa and unchanged exponent.
     */
    [[nodiscard]] Quality
    round(int tickSize) const;

    /** Scale an offer's amounts down so that the input does not exceed `limit`.
     *
     *  If `amount.in > limit`, sets `in = limit` and recomputes `out`
     *  proportionally via `divRound`.  The computed output is clamped to
     *  `amount.out` if arithmetic would produce a larger value, preventing
     *  money creation due to rounding.  Returns `amount` unchanged when
     *  `amount.in <= limit`.
     *
     *  @param amount  Current offer amounts (`in` = TakerPays, `out` = TakerGets).
     *  @param limit   Maximum allowed input amount.
     *  @return Scaled amounts satisfying `in <= limit` and `out <= amount.out`.
     *  @note Uses `divRound` (legacy rounding that ignores low-order bits).
     *      Use `ceilInStrict` when full-precision rounding is required.
     */
    [[nodiscard]] Amounts
    ceilIn(Amounts const& amount, STAmount const& limit) const;

    /** Scale a typed offer's amounts down so that the input does not exceed `limit`.
     *
     *  Converts both sides to `STAmount`, delegates to the `STAmount` overload,
     *  then converts the result back to the typed amounts.
     *
     *  @tparam In   Input amount type.
     *  @tparam Out  Output amount type.
     *  @param amount  Current offer amounts.
     *  @param limit   Maximum allowed input amount.
     *  @return Scaled amounts satisfying `in <= limit` and `out <= amount.out`.
     */
    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilIn(TAmounts<In, Out> const& amount, In const& limit) const;

    /** Scale an offer's amounts down so that the input does not exceed `limit`,
     *  using full-precision rounding.
     *
     *  Identical to `ceilIn` except it delegates to `divRoundStrict`, which
     *  considers all low-order bits that `divRound` ignores.  Introduced to
     *  fix subtle rounding bugs where a borderline result could influence
     *  whether an offer crosses.
     *
     *  @param amount    Current offer amounts.
     *  @param limit     Maximum allowed input amount.
     *  @param roundUp   Whether to round the recomputed output up (`true`) or
     *      down (`false`).
     *  @return Scaled amounts satisfying `in <= limit` and `out <= amount.out`.
     */
    [[nodiscard]] Amounts
    ceilInStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const;

    /** Scale a typed offer's amounts down so that the input does not exceed `limit`,
     *  using full-precision rounding.
     *
     *  @tparam In   Input amount type.
     *  @tparam Out  Output amount type.
     *  @param amount    Current offer amounts.
     *  @param limit     Maximum allowed input amount.
     *  @param roundUp   Whether to round the recomputed output up or down.
     *  @return Scaled amounts satisfying `in <= limit` and `out <= amount.out`.
     */
    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilInStrict(TAmounts<In, Out> const& amount, In const& limit, bool roundUp) const;

    /** Scale an offer's amounts down so that the output does not exceed `limit`.
     *
     *  If `amount.out > limit`, sets `out = limit` and recomputes `in`
     *  proportionally via `mulRound`.  The computed input is clamped to
     *  `amount.in` if arithmetic would produce a larger value, preventing
     *  money creation due to rounding.  Returns `amount` unchanged when
     *  `amount.out <= limit`.
     *
     *  @param amount  Current offer amounts.
     *  @param limit   Maximum allowed output amount.
     *  @return Scaled amounts satisfying `out <= limit` and `in <= amount.in`.
     *  @note Uses `mulRound` (legacy rounding that ignores low-order bits).
     *      Use `ceilOutStrict` when full-precision rounding is required.
     */
    [[nodiscard]] Amounts
    ceilOut(Amounts const& amount, STAmount const& limit) const;

    /** Scale a typed offer's amounts down so that the output does not exceed `limit`.
     *
     *  Converts both sides to `STAmount`, delegates to the `STAmount` overload,
     *  then converts the result back to the typed amounts.
     *
     *  @tparam In   Input amount type.
     *  @tparam Out  Output amount type.
     *  @param amount  Current offer amounts.
     *  @param limit   Maximum allowed output amount.
     *  @return Scaled amounts satisfying `out <= limit` and `in <= amount.in`.
     */
    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilOut(TAmounts<In, Out> const& amount, Out const& limit) const;

    /** Scale an offer's amounts down so that the output does not exceed `limit`,
     *  using full-precision rounding.
     *
     *  Identical to `ceilOut` except it delegates to `mulRoundStrict`, which
     *  considers all low-order bits that `mulRound` ignores.
     *
     *  @param amount    Current offer amounts.
     *  @param limit     Maximum allowed output amount.
     *  @param roundUp   Whether to round the recomputed input up (`true`) or
     *      down (`false`).
     *  @return Scaled amounts satisfying `out <= limit` and `in <= amount.in`.
     */
    [[nodiscard]] Amounts
    ceilOutStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const;

    /** Scale a typed offer's amounts down so that the output does not exceed `limit`,
     *  using full-precision rounding.
     *
     *  @tparam In   Input amount type.
     *  @tparam Out  Output amount type.
     *  @param amount    Current offer amounts.
     *  @param limit     Maximum allowed output amount.
     *  @param roundUp   Whether to round the recomputed input up or down.
     *  @return Scaled amounts satisfying `out <= limit` and `in <= amount.in`.
     */
    template <class In, class Out>
    [[nodiscard]] TAmounts<In, Out>
    ceilOutStrict(TAmounts<In, Out> const& amount, Out const& limit, bool roundUp) const;

private:
    /** Shared implementation for all typed `ceilIn`/`ceilOut` overloads.
     *
     *  Converts `amount` and `limit` to `STAmount`, calls `ceilFunction` (a
     *  member-function pointer to one of the `STAmount`-based overloads), and
     *  converts the result back to `TAmounts<In, Out>`.  Returns `amount`
     *  unchanged when `limitCmp <= limit` (i.e., the limit is not binding).
     *
     *  The variadic `Round...` pack forwards an optional `bool roundUp` argument
     *  to strict variants without requiring separate instantiations.
     *
     *  @tparam In           Input amount type.
     *  @tparam Out          Output amount type.
     *  @tparam Lim          Limit amount type (same as `In` or `Out`).
     *  @tparam FnPtr        Pointer to the `STAmount`-based ceil member function.
     *  @tparam Round        Empty or `{bool}` — forwarded as `roundUp`.
     *  @param amount        Current typed offer amounts.
     *  @param limit         The cap to apply.
     *  @param limitCmp      The side of `amount` to compare against `limit`
     *      (either `amount.in` or `amount.out`).
     *  @param ceilFunction  Member-function pointer to dispatch to.
     *  @param round         Optional rounding direction (strict variants only).
     *  @return Scaled `TAmounts<In, Out>`.
     */
    template <class In, class Out, class Lim, typename FnPtr, std::same_as<bool>... Round>
    [[nodiscard]] TAmounts<In, Out>
    ceilTAmountsHelper(
        TAmounts<In, Out> const& amount,
        Lim const& limit,
        Lim const& limitCmp,
        FnPtr ceilFunction,
        Round... round) const;

public:
    /** Returns `true` if `lhs` is lower quality (worse for the taker) than `rhs`.
     *
     *  Because the internal encoding is inverted, a lower quality corresponds
     *  to a *higher* stored integer, so this compares `lhs.value_ > rhs.value_`.
     */
    friend bool
    operator<(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.value_ > rhs.value_;
    }

    /** Returns `true` if `lhs` is higher quality (better for the taker) than `rhs`. */
    friend bool
    operator>(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.value_ < rhs.value_;
    }

    /** Returns `true` if `lhs` is lower or equal quality to `rhs`. */
    friend bool
    operator<=(Quality const& lhs, Quality const& rhs) noexcept
    {
        return !(lhs > rhs);
    }

    /** Returns `true` if `lhs` is higher or equal quality to `rhs`. */
    friend bool
    operator>=(Quality const& lhs, Quality const& rhs) noexcept
    {
        return !(lhs < rhs);
    }

    /** Returns `true` if both qualities encode the same exchange rate. */
    friend bool
    operator==(Quality const& lhs, Quality const& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    /** Returns `true` if the two qualities encode different exchange rates. */
    friend bool
    operator!=(Quality const& lhs, Quality const& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    /** Write the raw packed integer value of the quality to an output stream. */
    friend std::ostream&
    operator<<(std::ostream& os, Quality const& quality)
    {
        os << quality.value_;
        return os;
    }

    /** Return the relative error between two quality values: `|a - b| / min(a, b)`.
     *
     *  Extracts the exponent and mantissa from each packed value, scales them
     *  to a common exponent, and returns the normalized distance.  Used only
     *  in unit tests to verify that two qualities are sufficiently close.
     *
     *  @param q1  First quality; must be non-zero (asserted).
     *  @param q2  Second quality; must be non-zero (asserted).
     *  @return    `|q1 - q2| / min(q1, q2)` as a `double`.
     *  @note For testing only.  Production code should compare with the
     *      relational operators.
     */
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

        // maxVD and minVD are scaled so they have the same exponent; dividing
        // cancels out the exponents, leaving only the normalized mantissa difference.
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
    static constexpr Amounts (Quality::*kCEIL_IN_FN_PTR)(Amounts const&, STAmount const&) const =
        &Quality::ceilIn;

    return ceilTAmountsHelper(amount, limit, amount.in, kCEIL_IN_FN_PTR);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceilInStrict(TAmounts<In, Out> const& amount, In const& limit, bool roundUp) const
{
    static constexpr Amounts (Quality::*kCEIL_IN_FN_PTR)(Amounts const&, STAmount const&, bool)
        const = &Quality::ceilInStrict;

    return ceilTAmountsHelper(amount, limit, amount.in, kCEIL_IN_FN_PTR, roundUp);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceilOut(TAmounts<In, Out> const& amount, Out const& limit) const
{
    static constexpr Amounts (Quality::*kCEIL_OUT_FN_PTR)(Amounts const&, STAmount const&) const =
        &Quality::ceilOut;

    return ceil_TAmounts_helper(amount, limit, amount.out, kCEIL_OUT_FN_PTR);
}

template <class In, class Out>
TAmounts<In, Out>
Quality::ceilOutStrict(TAmounts<In, Out> const& amount, Out const& limit, bool roundUp) const
{
    static constexpr Amounts (Quality::*kCEIL_OUT_FN_PTR)(Amounts const&, STAmount const&, bool)
        const = &Quality::ceilOutStrict;

    return ceilTAmountsHelper(amount, limit, amount.out, kCEIL_OUT_FN_PTR, roundUp);
}

/** Compute the effective end-to-end exchange rate for a two-hop path.
 *
 *  If the first hop converts A→B at rate `lhs` and the second converts B→C
 *  at rate `rhs`, the composed quality is their product, re-encoded into the
 *  packed 64-bit format.  Used by the pathfinding engine to rank multi-hop
 *  routes against single-hop offers on a common scale.
 *
 *  @param lhs  Quality of the first leg (input → intermediate currency).
 *  @param rhs  Quality of the second leg (intermediate → output currency).
 *  @return     Composed quality representing the overall A→C exchange rate.
 *  @note Both input rates must be non-zero (asserted at runtime).  The
 *      composed exponent must fit in 8 bits (i.e., actual exponent in
 *      [-99, 155]); astronomically large or small paths will assert.
 */
Quality
composedQuality(Quality const& lhs, Quality const& rhs);

}  // namespace xrpl
