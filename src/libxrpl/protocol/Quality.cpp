/** @file
 *  Implements `Quality` arithmetic for XRPL's offer-matching engine.
 *
 *  A `Quality` is the exchange ratio `out / in` (output per unit of input)
 *  stored as an inverted 64-bit integer so that higher-quality (better for
 *  the taker) offers sort first under plain unsigned integer comparison.
 *  This file provides constructors, increment/decrement operators,
 *  proportional-scaling helpers (`ceilIn` / `ceilOut` and their strict
 *  variants), two-hop quality composition, and tick-size rounding.
 */
#include <xrpl/protocol/Quality.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <limits>

namespace xrpl {

/** Construct from a raw packed integer in STAmount encoding.
 *
 *  The top 8 bits are the biased exponent (actual exponent + 100) and the
 *  bottom 56 bits are the mantissa — identical to the IOU wire format used
 *  by `amountFromQuality()` and `getRate()`.
 *
 *  @param value  Packed 64-bit quality value; higher integers denote lower
 *      (worse) quality because the internal ordering is inverted.
 */
Quality::Quality(std::uint64_t value) : value_(value)
{
}

/** Construct from an in/out amount pair.
 *
 *  Encodes the ratio `amount.out / amount.in` using `getRate()`, which
 *  produces the same packed 64-bit format as the STAmount IOU encoding.
 *  For offers, `in` is TakerPays and `out` is TakerGets.
 *
 *  @param amount  The offer amounts; neither side should be zero.
 */
Quality::Quality(Amounts const& amount) : value_(getRate(amount.out, amount.in))
{
}

/** Advance to the next higher quality level (pre-increment).
 *
 *  Because `value_` is an inverted encoding — lower integer = higher quality
 *  — this decrements `value_` by one. Each step represents the smallest
 *  distinguishable improvement in exchange rate.
 *
 *  @pre `value_ > 0`; underflow is asserted.
 *  @return Reference to `*this` after advancement.
 */
Quality&
Quality::operator++()
{
    XRPL_ASSERT(value_ > 0, "xrpl::Quality::operator++() : minimum value");
    --value_;
    return *this;
}

/** Advance to the next higher quality level (post-increment).
 *
 *  Returns the quality before advancement; see pre-increment for details.
 *
 *  @return Copy of `*this` prior to the increment.
 */
Quality
Quality::operator++(int)
{
    Quality prev(*this);
    ++*this;
    return prev;
}

/** Retreat to the next lower quality level (pre-decrement).
 *
 *  Because `value_` is an inverted encoding — higher integer = lower quality
 *  — this increments `value_` by one.
 *
 *  @pre `value_ < UINT64_MAX`; overflow is asserted.
 *  @return Reference to `*this` after retreat.
 */
Quality&
Quality::operator--()
{
    XRPL_ASSERT(
        value_ < std::numeric_limits<value_type>::max(),
        "xrpl::Quality::operator--() : maximum value");
    ++value_;
    return *this;
}

/** Retreat to the next lower quality level (post-decrement).
 *
 *  Returns the quality before retreat; see pre-decrement for details.
 *
 *  @return Copy of `*this` prior to the decrement.
 */
Quality
Quality::operator--(int)
{
    Quality prev(*this);
    --*this;
    return prev;
}

/** Proportionally scale an offer down so that its input does not exceed a
 *  limit, using a caller-supplied division function.
 *
 *  If `amount.in > limit`, sets `in = limit` and recomputes `out` via
 *  `DivRoundFunc(limit, quality.rate(), ...)`.  A secondary clamp then
 *  ensures `result.out` never exceeds the original `amount.out` — this
 *  is the "no money creation" invariant.  Returns `amount` unchanged when
 *  `amount.in <= limit`.
 *
 *  The template parameter selects between `divRound` (legacy, ignores
 *  low-order bits) and `divRoundStrict` (full-precision).
 *
 *  @tparam DivRoundFunc  Division function with the signature
 *      `STAmount(STAmount const&, STAmount const&, Asset const&, bool)`.
 *  @param amount   The current offer amounts (in = TakerPays, out = TakerGets).
 *  @param limit    Maximum allowed input amount.
 *  @param roundUp  Passed directly to `DivRoundFunc`.
 *  @param quality  The offer's exchange rate used to recompute the output.
 *  @return Scaled amounts with `in <= limit` and `out <= amount.out`.
 */
template <STAmount (*DivRoundFunc)(STAmount const&, STAmount const&, Asset const&, bool)>
static Amounts
ceilInImpl(Amounts const& amount, STAmount const& limit, bool roundUp, Quality const& quality)
{
    if (amount.in > limit)
    {
        Amounts result(limit, DivRoundFunc(limit, quality.rate(), amount.out.asset(), roundUp));
        if (result.out > amount.out)
            result.out = amount.out;
        XRPL_ASSERT(result.in == limit, "xrpl::ceilInImpl : result matches limit");
        return result;
    }
    XRPL_ASSERT(amount.in <= limit, "xrpl::ceilInImpl : result inside limit");
    return amount;
}

/** Cap the input side of an offer at `limit`, scaling output proportionally.
 *
 *  Uses `divRound` (legacy rounding) with `roundUp = true`.  Callers that
 *  require full-precision rounding should use `ceilInStrict` instead.
 *
 *  @param amount  Current offer amounts.
 *  @param limit   Maximum input; unchanged when `amount.in <= limit`.
 *  @return Scaled amounts satisfying `in <= limit` and `out <= amount.out`.
 */
Amounts
Quality::ceilIn(Amounts const& amount, STAmount const& limit) const
{
    return ceilInImpl<divRound>(amount, limit, /* roundUp */ true, *this);
}

/** Cap the input side of an offer at `limit` with full-precision rounding.
 *
 *  Unlike `ceilIn`, this uses `divRoundStrict`, which considers all low-order
 *  bits that `divRound` ignores.  Introduced to fix subtle rounding bugs
 *  without changing the default behavior for existing callers.
 *
 *  @param amount    Current offer amounts.
 *  @param limit     Maximum input; unchanged when `amount.in <= limit`.
 *  @param roundUp   Whether to round the recomputed output up or down.
 *  @return Scaled amounts satisfying `in <= limit` and `out <= amount.out`.
 */
Amounts
Quality::ceilInStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const
{
    return ceilInImpl<divRoundStrict>(amount, limit, roundUp, *this);
}

/** Proportionally scale an offer down so that its output does not exceed a
 *  limit, using a caller-supplied multiplication function.
 *
 *  If `amount.out > limit`, sets `out = limit` and recomputes `in` via
 *  `MulRoundFunc(limit, quality.rate(), ...)`.  A secondary clamp then
 *  ensures `result.in` never exceeds the original `amount.in` — the
 *  "no money creation" invariant for the output direction.  Returns
 *  `amount` unchanged when `amount.out <= limit`.
 *
 *  @tparam MulRoundFunc  Multiplication function with the signature
 *      `STAmount(STAmount const&, STAmount const&, Asset const&, bool)`.
 *  @param amount   The current offer amounts.
 *  @param limit    Maximum allowed output amount.
 *  @param roundUp  Passed directly to `MulRoundFunc`.
 *  @param quality  The offer's exchange rate used to recompute the input.
 *  @return Scaled amounts with `out <= limit` and `in <= amount.in`.
 */
template <STAmount (*MulRoundFunc)(STAmount const&, STAmount const&, Asset const&, bool)>
static Amounts
ceilOutImpl(Amounts const& amount, STAmount const& limit, bool roundUp, Quality const& quality)
{
    if (amount.out > limit)
    {
        Amounts result(MulRoundFunc(limit, quality.rate(), amount.in.asset(), roundUp), limit);
        if (result.in > amount.in)
            result.in = amount.in;
        XRPL_ASSERT(result.out == limit, "xrpl::ceilOutImpl : result matches limit");
        return result;
    }
    XRPL_ASSERT(amount.out <= limit, "xrpl::ceilOutImpl : result inside limit");
    return amount;
}

/** Cap the output side of an offer at `limit`, scaling input proportionally.
 *
 *  Uses `mulRound` (legacy rounding) with `roundUp = true`.  Callers that
 *  require full-precision rounding should use `ceilOutStrict` instead.
 *
 *  @param amount  Current offer amounts.
 *  @param limit   Maximum output; unchanged when `amount.out <= limit`.
 *  @return Scaled amounts satisfying `out <= limit` and `in <= amount.in`.
 */
Amounts
Quality::ceilOut(Amounts const& amount, STAmount const& limit) const
{
    return ceilOutImpl<mulRound>(amount, limit, /* roundUp */ true, *this);
}

/** Cap the output side of an offer at `limit` with full-precision rounding.
 *
 *  Unlike `ceilOut`, this uses `mulRoundStrict`, which considers all
 *  low-order bits that `mulRound` ignores.  Introduced to fix subtle
 *  rounding bugs without changing the default behavior for existing callers.
 *
 *  @param amount    Current offer amounts.
 *  @param limit     Maximum output; unchanged when `amount.out <= limit`.
 *  @param roundUp   Whether to round the recomputed input up or down.
 *  @return Scaled amounts satisfying `out <= limit` and `in <= amount.in`.
 */
Amounts
Quality::ceilOutStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const
{
    return ceilOutImpl<mulRoundStrict>(amount, limit, roundUp, *this);
}

/** Compute the effective end-to-end exchange rate for a two-hop path.
 *
 *  If the first hop converts A→B at rate `lhs` and the second converts B→C
 *  at rate `rhs`, the composed quality is their product, re-encoded into
 *  the packed 64-bit format.  The exponent must fit in 8 bits (1–255); the
 *  assertion fires if the composed rate is astronomically large or small,
 *  indicating a degenerate path.
 *
 *  @param lhs  Quality of the first leg (input → intermediate currency).
 *  @param rhs  Quality of the second leg (intermediate → output currency).
 *  @return     Composed quality representing the overall A→C rate.
 *  @note Both input rates must be non-zero (asserted).
 */
Quality
composedQuality(Quality const& lhs, Quality const& rhs)
{
    STAmount const lhsRate(lhs.rate());
    XRPL_ASSERT(lhsRate != beast::kZERO, "xrpl::composedQuality : nonzero left input");

    STAmount const rhsRate(rhs.rate());
    XRPL_ASSERT(rhsRate != beast::kZERO, "xrpl::composedQuality : nonzero right input");

    STAmount const rate(mulRound(lhsRate, rhsRate, lhsRate.asset(), true));

    std::uint64_t const storedExponent(rate.exponent() + 100);
    std::uint64_t const storedMantissa(rate.mantissa());

    XRPL_ASSERT(
        (storedExponent > 0) && (storedExponent <= 255), "xrpl::composedQuality : valid exponent");

    return Quality((storedExponent << (64 - 8)) | storedMantissa);
}

/** Return this quality rounded up to `digits` significant decimal digits.
 *
 *  Used for tick-size enforcement: truncates mantissa precision so that
 *  offers differing only in low-order digits are treated as equivalent.
 *  Rounding is always upward (ceiling), which makes the encoded rate
 *  slightly higher (worse for the taker) and prevents a rounded quality
 *  from being mistakenly ranked better than the original.
 *
 *  The bias-and-mask technique (`mantissa += mod - 1; mantissa -= mantissa %
 *  mod`) implements ceiling division without branching.
 *
 *  @param digits  Number of significant decimal digits to retain.
 *      Valid range: `kMIN_TICK_SIZE` (3) to `kMAX_TICK_SIZE` (16),
 *      enforced by callers.
 *  @return A new `Quality` with the mantissa rounded up to `digits` digits
 *      and the exponent unchanged.
 */
Quality
Quality::round(int digits) const
{
    // Powers of ten indexed by digit count; kMOD[d] is the modulus that
    // zeroes out all but the top `d` significant digits of a 16-digit mantissa.
    static std::uint64_t const kMOD[17] = {
        /* 0 */ 10000000000000000,
        /* 1 */ 1000000000000000,
        /* 2 */ 100000000000000,
        /* 3 */ 10000000000000,
        /* 4 */ 1000000000000,
        /* 5 */ 100000000000,
        /* 6 */ 10000000000,
        /* 7 */ 1000000000,
        /* 8 */ 100000000,
        /* 9 */ 10000000,
        /* 10 */ 1000000,
        /* 11 */ 100000,
        /* 12 */ 10000,
        /* 13 */ 1000,
        /* 14 */ 100,
        /* 15 */ 10,
        /* 16 */ 1,
    };

    auto exponent = value_ >> (64 - 8);
    auto mantissa = value_ & 0x00ffffffffffffffULL;
    mantissa += kMOD[digits] - 1;
    mantissa -= (mantissa % kMOD[digits]);

    return Quality{(exponent << (64 - 8)) | mantissa};
}

}  // namespace xrpl
