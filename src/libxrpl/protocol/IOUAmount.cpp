/** @file
 *  IOU amount arithmetic and normalization for the XRP Ledger.
 *
 *  Implements the normalization engine, `Number`-based construction,
 *  addition with exponent alignment, and the `mulRatio` ratio-multiplication
 *  primitive used by fee calculations, transfer rates, and AMM math.
 *  All non-XRP balances in the ledger (trust lines, offers, AMM pools) are
 *  represented as `IOUAmount`.
 */
#include <xrpl/protocol/IOUAmount.h>

#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/STAmount.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace xrpl {

namespace {

/** Returns the per-coroutine `LocalValue` that backs the STNumber switchover flag.
 *
 *  Using a function-local static avoids C++ static initialization order
 *  problems that would corrupt the flag if it were a namespace-scope variable.
 *  The default value of `true` means the `Number`-based arithmetic path is
 *  active unless explicitly disabled.
 */
LocalValue<bool>&
getStaticSTNumberSwitchover()
{
    static LocalValue<bool> kR{true};
    return kR;
}
}  // namespace

/** Returns the current STNumber arithmetic switchover state for this coroutine.
 *
 *  When `true`, `IOUAmount` normalization and addition delegate to the
 *  `Number` class (higher-precision, correct rounding). When `false`, the
 *  legacy base-10 digit-shifting algorithm is used instead.
 *
 *  The value is stored in a `LocalValue<bool>` — the XRPL coroutine-aware
 *  thread-local mechanism — so concurrent transactions cannot interfere with
 *  each other's arithmetic mode.
 *
 *  @return `true` if the `Number`-based path is active, `false` for legacy.
 *  @see setSTNumberSwitchover, NumberSO
 */
bool
getSTNumberSwitchover()
{
    return *getStaticSTNumberSwitchover();
}

/** Sets the STNumber arithmetic switchover state for this coroutine.
 *
 *  Prefer the `NumberSO` RAII guard over calling this function directly; it
 *  saves and restores the previous value automatically.
 *
 *  @param v `true` to enable the `Number`-based path; `false` for legacy.
 *  @see getSTNumberSwitchover, NumberSO
 */
void
setSTNumberSwitchover(bool v)
{
    *getStaticSTNumberSwitchover() = v;
}

// --- Canonical range constants (imported from STAmount to lock IOUAmount
//     precision to the on-wire serialization format) ---
// log(2^63,10) ~ 18.96; the 15-digit mantissa range fits comfortably in int64.
static std::int64_t constexpr kMIN_MANTISSA = STAmount::kMIN_VALUE;  /**< 10^15 */
static std::int64_t constexpr kMAX_MANTISSA = STAmount::kMAX_VALUE;  /**< 10^16 - 1 */
static int constexpr kMIN_EXPONENT = STAmount::kMIN_OFFSET;          /**< -96 */
static int constexpr kMAX_EXPONENT = STAmount::kMAX_OFFSET;          /**< 80 */

/** Constructs an `IOUAmount` from a `Number` without invoking `normalize()`.
 *
 *  The `(mantissa, exponent)` constructor always calls `normalize()`, and the
 *  switchover path inside `normalize()` calls this function — using the public
 *  constructor here would cause infinite recursion. This factory bypasses
 *  `normalize()` entirely by writing directly to the private fields, then
 *  delegates to `Number::normalizeToRange` to enforce the canonical mantissa
 *  and exponent bounds.
 *
 *  @param number The high-precision value to convert.
 *  @return An `IOUAmount` whose fields are set by `Number::normalizeToRange`.
 *  @note Callers are responsible for checking the resulting exponent against
 *      `kMIN_EXPONENT` / `kMAX_EXPONENT` and handling overflow or underflow.
 */
IOUAmount
IOUAmount::fromNumber(Number const& number)
{
    IOUAmount result{};
    std::tie(result.mantissa_, result.exponent_) =
        number.normalizeToRange(kMIN_MANTISSA, kMAX_MANTISSA);
    return result;
}

/** Returns the smallest representable positive IOU amount (10^15 × 10^-96 = 10^-81).
 *
 *  Used by `mulRatio` as the result when `roundUp` is `true` and the
 *  computed value is positive but too small to normalize — ensuring a
 *  non-zero fee is never silently dropped to zero during rounding.
 *
 *  @return `IOUAmount(kMIN_MANTISSA, kMIN_EXPONENT)`.
 */
IOUAmount
IOUAmount::minPositiveAmount()
{
    return IOUAmount(kMIN_MANTISSA, kMIN_EXPONENT);
}

/** Adjusts mantissa and exponent to canonical form.
 *
 *  When the STNumber switchover is active, delegates to `fromNumber()` /
 *  `Number::normalizeToRange` for a single precision-preserving step.
 *  Under the legacy path, uses a base-10 digit-shifting loop: multiplies
 *  the mantissa by 10 (decrementing the exponent) while below `kMIN_MANTISSA`,
 *  and divides by 10 (incrementing the exponent) while above `kMAX_MANTISSA`.
 *
 *  Overflow policy: throws `std::overflow_error` if the exponent would exceed
 *  `kMAX_EXPONENT` during scale-down.
 *  Underflow policy: silently becomes zero when the mantissa cannot be scaled
 *  to `kMIN_MANTISSA` without pushing the exponent below `kMIN_EXPONENT`.
 *
 *  @throw std::overflow_error if the normalized value exceeds the maximum
 *      representable IOU amount.
 */
void
IOUAmount::normalize()
{
    if (mantissa_ == 0)
    {
        *this = beast::kZERO;
        return;
    }

    if (getSTNumberSwitchover())
    {
        Number const v{mantissa_, exponent_};
        *this = fromNumber(v);
        if (exponent_ > kMAX_EXPONENT)
            Throw<std::overflow_error>("value overflow");
        if (exponent_ < kMIN_EXPONENT)
            *this = beast::kZERO;
        return;
    }

    bool const negative = (mantissa_ < 0);

    if (negative)
        mantissa_ = -mantissa_;

    while ((mantissa_ < kMIN_MANTISSA) && (exponent_ > kMIN_EXPONENT))
    {
        mantissa_ *= 10;
        --exponent_;
    }

    while (mantissa_ > kMAX_MANTISSA)
    {
        if (exponent_ >= kMAX_EXPONENT)
            Throw<std::overflow_error>("IOUAmount::normalize");

        mantissa_ /= 10;
        ++exponent_;
    }

    if ((exponent_ < kMIN_EXPONENT) || (mantissa_ < kMIN_MANTISSA))
    {
        *this = beast::kZERO;
        return;
    }

    if (exponent_ > kMAX_EXPONENT)
        Throw<std::overflow_error>("value overflow");

    if (negative)
        mantissa_ = -mantissa_;
}

/** Constructs an `IOUAmount` from a high-precision `Number`.
 *
 *  Delegates to `fromNumber()` to avoid the normalization recursion, then
 *  validates the resulting exponent: throws on overflow, silently zeroes on
 *  underflow (sub-minimum amounts round to nothing).
 *
 *  @param other The `Number` value to convert.
 *  @throw std::overflow_error if `other` exceeds the maximum representable
 *      IOU amount.
 */
IOUAmount::IOUAmount(Number const& other) : IOUAmount(fromNumber(other))
{
    if (exponent_ > kMAX_EXPONENT)
        Throw<std::overflow_error>("value overflow");
    if (exponent_ < kMIN_EXPONENT)
        *this = beast::kZERO;
}

/** Adds `other` to this amount in place.
 *
 *  Under the STNumber switchover path, converts both operands to `Number`,
 *  adds with full precision, and converts back. Under the legacy path,
 *  aligns the two operands to a common exponent by truncating digits from
 *  the smaller-magnitude value, then adds mantissas. A near-cancellation
 *  guard zeros the result when the raw sum falls in [-10, 10], since values
 *  that small cannot be re-normalized to the required mantissa range.
 *
 *  @param other The amount to add.
 *  @return `*this` after addition.
 *  @throw std::overflow_error (via `normalize()`) if the sum exceeds the
 *      maximum representable IOU amount.
 *  @note The legacy mantissa addition cannot overflow `int64_t` because
 *      both operands are pre-aligned and individually within the mantissa
 *      range, but the subsequent `normalize()` call can throw.
 */
IOUAmount&
IOUAmount::operator+=(IOUAmount const& other)
{
    if (other == beast::kZERO)
        return *this;

    if (*this == beast::kZERO)
    {
        *this = other;
        return *this;
    }

    if (getSTNumberSwitchover())
    {
        *this = IOUAmount{Number{*this} + Number{other}};
        return *this;
    }
    auto m = other.mantissa_;
    auto e = other.exponent_;

    while (exponent_ < e)
    {
        mantissa_ /= 10;
        ++exponent_;
    }

    while (e < exponent_)
    {
        m /= 10;
        ++e;
    }

    // This addition cannot overflow an std::int64_t but we may throw from
    // normalize if the result isn't representable.
    mantissa_ += m;

    if (mantissa_ >= -10 && mantissa_ <= 10)
    {
        *this = beast::kZERO;
        return *this;
    }

    normalize();
    return *this;
}

/** Returns a decimal string representation of `amount`.
 *
 *  Delegates to `to_string(Number)`, which formats the mantissa and
 *  exponent as a human-readable decimal (e.g. `"1.5e-10"`).
 *
 *  @param amount The IOU amount to format.
 *  @return A decimal string representation.
 */
std::string
to_string(IOUAmount const& amount)
{
    return to_string(Number{amount});
}

/** Multiplies an IOU amount by a rational fraction with controlled rounding.
 *
 *  Computes `amt × num / den` using 128-bit intermediate arithmetic to
 *  avoid overflow of the 64-bit mantissa. A static lookup table of powers
 *  of ten (`kPOWER_TABLE`) drives exact integer log₁₀ computations, avoiding
 *  the rounding ambiguities of `std::log10`.
 *
 *  Precision-maximization: when the integer quotient `mul / den` leaves a
 *  remainder, the code scales both the quotient and the remainder up by as
 *  many powers of ten as the mantissa headroom allows, recovering fractional
 *  digits that would otherwise be lost.
 *
 *  Rounding rules (only applied when a remainder exists after all scaling):
 *  - Positive result, `roundUp == true`: mantissa incremented by one ULP.
 *    If the unrounded result is zero, `minPositiveAmount()` is returned so
 *    that a non-zero fee is never silently dropped.
 *  - Negative result, `roundUp == false` (round toward −∞): mantissa
 *    decremented by one ULP. Same zero guard applies.
 *  - All other combinations: remainder is truncated.
 *
 *  @param amt     The IOU amount to scale.
 *  @param num     32-bit unsigned numerator.
 *  @param den     32-bit unsigned denominator; must be non-zero.
 *  @param roundUp When `true`, round away from zero for positive results
 *      (and toward zero for negative); when `false`, truncate toward zero
 *      for positive results.
 *  @return `amt × num / den`, normalized and rounded per the rules above.
 *  @throw std::runtime_error if `den == 0`.
 *  @throw std::overflow_error (via `IOUAmount` constructor) if the result
 *      exceeds the maximum representable IOU amount.
 *  @note `num` and `den` are typically quality-encoding constants such as
 *      `QUALITY_ONE` (10^9) in path-finding, or transfer-rate values in
 *      fee calculations.
 */
IOUAmount
mulRatio(IOUAmount const& amt, std::uint32_t num, std::uint32_t den, bool roundUp)
{
    using namespace boost::multiprecision;

    if (den == 0u)
        Throw<std::runtime_error>("division by zero");

    // Powers of ten from 10^0 to 10^29; covers the largest 128-bit intermediate
    // (~2^96 < 10^29). Table lookup is used instead of std::log10 to guarantee
    // exact integer boundaries without floating-point rounding error.
    static auto const kPOWER_TABLE = [] {
        std::vector<uint128_t> result;
        result.reserve(30);  // 2^96 is largest intermediate result size
        uint128_t cur(1);
        for (int i = 0; i < 30; ++i)
        {
            result.push_back(cur);
            cur *= 10;
        };
        return result;
    }();

    // Returns floor(log10(v)); returns -1 for v == 0.
    static auto kLOG10_FLOOR = [](uint128_t const& v) {
        auto const l = std::ranges::lower_bound(kPOWER_TABLE, v);
        int index = std::distance(kPOWER_TABLE.begin(), l);
        if (*l != v)
            --index;
        return index;
    };

    // Returns ceil(log10(v)).
    static auto kLOG10_CEIL = [](uint128_t const& v) {
        auto const l = std::ranges::lower_bound(kPOWER_TABLE, v);
        return int(std::distance(kPOWER_TABLE.begin(), l));
    };

    static auto const kFL64 = kLOG10_FLOOR(std::numeric_limits<std::int64_t>::max());

    bool const neg = amt.mantissa() < 0;
    uint128_t const den128(den);
    // 64-bit mantissa × 32-bit num fits in 128 bits; cannot overflow.
    uint128_t const mul = uint128_t(neg ? -amt.mantissa() : amt.mantissa()) * uint128_t(num);

    auto low = mul / den128;
    uint128_t rem(mul - low * den128);

    int exponent = amt.exponent();

    if (rem)
    {
        // `rem / den128` is zero under integer division. Instead, scale both
        // `low` and `rem` up by as many powers of ten as the 64-bit mantissa
        // headroom allows, then recompute `rem / den128`. This recovers
        // fractional digits that would otherwise be discarded.
        auto const roomToGrow = kFL64 - kLOG10_CEIL(low);
        if (roomToGrow > 0)
        {
            exponent -= roomToGrow;
            low *= kPOWER_TABLE[roomToGrow];
            rem *= kPOWER_TABLE[roomToGrow];
        }
        auto const addRem = rem / den128;
        low += addRem;
        rem = rem - addRem * den128;
    }

    // If `low` still overflows 64 bits (~2^95 worst case), divide down and
    // track whether any set bits were lost (needed for rounding).
    bool hasRem = bool(rem);
    auto const mustShrink = kLOG10_CEIL(low) - kFL64;
    if (mustShrink > 0)
    {
        uint128_t const sav(low);
        exponent += mustShrink;
        low /= kPOWER_TABLE[mustShrink];
        if (!hasRem)
            hasRem = bool(sav - low * kPOWER_TABLE[mustShrink]);
    }

    std::int64_t mantissa = low.convert_to<std::int64_t>();

    if (neg)
        mantissa *= -1;

    IOUAmount result(mantissa, exponent);

    if (hasRem)
    {
        if (roundUp && !neg)
        {
            if (!result)
                return IOUAmount::minPositiveAmount();
            // Safe: mantissa is already normalized and cannot overflow the range.
            return IOUAmount(result.mantissa() + 1, result.exponent());
        }

        if (!roundUp && neg)
        {
            if (!result)
                return IOUAmount(-kMIN_MANTISSA, kMIN_EXPONENT);
            // Safe: `result` is non-zero so the mantissa will not underflow.
            return IOUAmount(result.mantissa() - 1, result.exponent());
        }
    }

    return result;
}

}  // namespace xrpl
