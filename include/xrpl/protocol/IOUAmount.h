#pragma once

#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>

#include <boost/operators.hpp>

#include <cstdint>
#include <string>

namespace xrpl {

/** Fixed-precision decimal floating-point type for IOU (non-native) balances.
 *
 *  Encodes a value as `mantissa × 10^exponent` using a 64-bit signed mantissa
 *  and an integer exponent. Canonical form requires the absolute value of the
 *  mantissa to lie in `[10^15, 10^16−1]` (i.e., `[1000000000000000,
 *  9999999999999999]`) and the exponent to lie in `[-96, 80]`. These bounds
 *  match the on-wire limits in `STAmount`, so a normalized `IOUAmount` is
 *  always serializable.
 *
 *  Zero is the sentinel `{mantissa=0, exponent=-100}`. The exponent `-100` is
 *  chosen to be below the minimum representable non-zero exponent (`-96`), so
 *  that numeric ordering via the exponent field correctly places zero below
 *  the smallest positive amount.
 *
 *  Arithmetic operations can throw `std::overflow_error` during normalization
 *  if the result exceeds the largest representable amount; underflows silently
 *  truncate to zero. This asymmetry is intentional: overflow indicates a
 *  programming error, while sub-minimum amounts arise naturally from interest
 *  calculations and must degrade gracefully.
 *
 *  The class privately inherits `boost::totally_ordered` and
 *  `boost::additive` to derive the full set of comparison and binary
 *  arithmetic operators from the handful of hand-written primitives
 *  (`operator==`, `operator<`, `operator+=`, `operator-`).
 *
 *  @note Normalization has two code paths selected by `getSTNumberSwitchover()`:
 *      the legacy in-place loop and the modern path delegating to
 *      `Number::normalizeToRange`. Use `NumberSO` to scope either path.
 */
class IOUAmount : private boost::totally_ordered<IOUAmount>, private boost::additive<IOUAmount>
{
private:
    using mantissa_type = std::int64_t;
    using exponent_type = int;
    mantissa_type mantissa_{};
    exponent_type exponent_{};

    /** Adjusts the mantissa and exponent to the proper range.
     *
     *  Scales the mantissa up (multiply by 10, decrement exponent) or down
     *  (divide by 10, increment exponent) until the absolute value of the
     *  mantissa is in `[10^15, 10^16−1]` and the exponent is in `[-96, 80]`.
     *
     *  Which algorithm is used depends on `getSTNumberSwitchover()`: when
     *  false, a legacy digit-by-digit loop; when true (the default), delegates
     *  to `Number::normalizeToRange`.
     *
     *  @throws std::overflow_error if the value is too large to be represented.
     *  @note Underflow silently rounds to zero rather than throwing.
     */
    void
    normalize();

    /** Convert a `Number` to an `IOUAmount` by fitting its mantissa into
     *  the IOU `10^15` precision range via `Number::normalizeToRange`.
     *
     *  @param number The `Number` value to convert.
     *  @return The nearest representable `IOUAmount`.
     */
    static IOUAmount
    fromNumber(Number const& number);

public:
    /** Default-constructs a zero amount (`{mantissa=0, exponent=0}`).
     *
     *  @note The raw fields are zero-initialized but `normalize()` is not
     *      called; use `IOUAmount{beast::kZERO}` to get the canonical zero
     *      sentinel `{mantissa=0, exponent=-100}`.
     */
    IOUAmount() = default;

    /** Construct from a `Number`, fitting its mantissa into IOU precision.
     *
     *  @param other The `Number` to convert. Delegates to `fromNumber()`.
     */
    explicit IOUAmount(Number const& other);

    /** Construct the canonical zero sentinel `{mantissa=0, exponent=-100}`. */
    IOUAmount(beast::Zero);

    /** Construct from raw mantissa and exponent, then normalize.
     *
     *  @param mantissa  The signed mantissa; sign determines the amount's sign.
     *  @param exponent  The power-of-ten exponent.
     *  @throws std::overflow_error if the value cannot be normalized to the
     *      representable range after scaling.
     */
    IOUAmount(mantissa_type mantissa, exponent_type exponent);

    /** Reset to the canonical zero sentinel `{mantissa=0, exponent=-100}`.
     *
     *  The exponent `-100` ensures zero sorts below the smallest positive
     *  amount whose minimum exponent is `-96`.
     */
    IOUAmount& operator=(beast::Zero);

    /** Implicit conversion to `Number`.
     *
     *  Constructs `Number{mantissa_, exponent_}`, bridging the legacy IOU
     *  type into the modern arithmetic layer. Conversion to `Number` is
     *  intentionally implicit; the reverse (from `Number`) is explicit.
     */
    operator Number() const;

    /** Add another amount in-place.
     *
     *  When `getSTNumberSwitchover()` is true, routes through
     *  `Number` arithmetic for correct handling across the two normalization
     *  regimes. Otherwise, performs manual exponent alignment.
     *
     *  @param other The amount to add.
     *  @return Reference to `*this` after normalization.
     *  @throws std::overflow_error if the result exceeds the representable range.
     */
    IOUAmount&
    operator+=(IOUAmount const& other);

    /** Subtract another amount in-place.
     *
     *  Implemented as `*this += -other`.
     *
     *  @param other The amount to subtract.
     *  @return Reference to `*this` after normalization.
     *  @throws std::overflow_error if the result exceeds the representable range.
     */
    IOUAmount&
    operator-=(IOUAmount const& other);

    /** Negate the amount without calling `normalize()`.
     *
     *  Flips the sign of the mantissa. Safe because the negation of a
     *  normalized value is also normalized; negating zero leaves the
     *  `{0, -100}` sentinel unchanged.
     *
     *  @return A new `IOUAmount` with the same magnitude and opposite sign.
     */
    IOUAmount
    operator-() const;

    /** Returns true if both amounts have identical mantissa and exponent.
     *
     *  Valid because every non-zero value has a unique canonical
     *  representation after normalization, and zero is always `{0, -100}`.
     *
     *  @param other The amount to compare.
     */
    bool
    operator==(IOUAmount const& other) const;

    /** Returns true if this amount is strictly less than `other`.
     *
     *  Delegates to `Number` comparison, which handles the zero sentinel and
     *  cross-regime comparisons correctly.
     *
     *  @param other The amount to compare against.
     */
    bool
    operator<(IOUAmount const& other) const;

    /** Returns true if the amount is not zero */
    explicit
    operator bool() const noexcept;

    /** Return the sign of the amount */
    [[nodiscard]] int
    signum() const noexcept;

    /** Return the raw (normalized) exponent.
     *
     *  The value is in `[-96, 80]` for non-zero amounts, or `-100` for zero.
     */
    [[nodiscard]] exponent_type
    exponent() const noexcept;

    /** Return the raw (normalized) signed mantissa.
     *
     *  For non-zero amounts, the absolute value is in `[10^15, 10^16−1]`.
     *  Zero returns `0`.
     */
    [[nodiscard]] mantissa_type
    mantissa() const noexcept;

    /** Return the smallest representable positive `IOUAmount`.
     *
     *  Corresponds to `{mantissa = 10^15, exponent = -96}`, the lower-left
     *  corner of the normalized canonical range.
     */
    static IOUAmount
    minPositiveAmount();

    friend std::ostream&
    operator<<(std::ostream& os, IOUAmount const& x)
    {
        return os << to_string(x);
    }
};

inline IOUAmount::IOUAmount(beast::Zero)
{
    *this = beast::kZERO;
}

inline IOUAmount::IOUAmount(mantissa_type mantissa, exponent_type exponent)
    : mantissa_(mantissa), exponent_(exponent)
{
    normalize();
}

inline IOUAmount&
IOUAmount::operator=(beast::Zero)
{
    mantissa_ = 0;
    exponent_ = -100;
    return *this;
}

inline IOUAmount::
operator Number() const
{
    return Number{mantissa_, exponent_};
}

inline IOUAmount&
IOUAmount::operator-=(IOUAmount const& other)
{
    *this += -other;
    return *this;
}

inline IOUAmount
IOUAmount::operator-() const
{
    return {-mantissa_, exponent_};
}

inline bool
IOUAmount::operator==(IOUAmount const& other) const
{
    return exponent_ == other.exponent_ && mantissa_ == other.mantissa_;
}

inline bool
IOUAmount::operator<(IOUAmount const& other) const
{
    return Number{*this} < Number{other};
}

inline IOUAmount::
operator bool() const noexcept
{
    return mantissa_ != 0;
}

inline int
IOUAmount::signum() const noexcept
{
    if (mantissa_ < 0)
        return -1;
    return (mantissa_ != 0) ? 1 : 0;
}

inline IOUAmount::exponent_type
IOUAmount::exponent() const noexcept
{
    return exponent_;
}

inline IOUAmount::mantissa_type
IOUAmount::mantissa() const noexcept
{
    return mantissa_;
}

/** Format an `IOUAmount` as a human-readable decimal string.
 *
 *  Produces integer notation when the exponent is non-negative (e.g. `"2e20"`
 *  for very large values), or decimal notation for fractional amounts (e.g.
 *  `"0.025"`). Scientific notation is used when the decimal form would be
 *  impractical.
 *
 *  @param amount The amount to format.
 *  @return A decimal string representation of `amount`.
 */
std::string
to_string(IOUAmount const& amount);

/** Compute `amt × num / den` with higher precision than sequential operations.
 *
 *  Intermediate products are held in a 128-bit unsigned integer to avoid
 *  overflow when multiplying a 64-bit mantissa by a 32-bit numerator. The
 *  quotient and remainder are then rescaled to fit the 64-bit IOU mantissa
 *  range, with the rounding direction determined by `roundUp`.
 *
 *  @param amt     The base amount to scale.
 *  @param num     Numerator of the scaling ratio; may be zero to produce zero.
 *  @param den     Denominator of the scaling ratio; must not be zero.
 *  @param roundUp When true, rounds toward positive infinity for positive
 *      results and toward negative infinity for negative results (directed
 *      rounding semantics). When false, rounds toward zero.
 *  @return The scaled amount `amt × num / den`.
 *  @throws std::overflow_error if the result exceeds the representable range.
 *  @throws std::domain_error (or similar) if `den` is zero.
 */
IOUAmount
mulRatio(IOUAmount const& amt, std::uint32_t num, std::uint32_t den, bool roundUp);

/** Return the current coroutine-local STNumber switchover flag.
 *
 *  When true, `IOUAmount::normalize()` and `operator+=` use the modern
 *  `Number`-based code path; when false, the legacy in-place loop is used.
 *  The flag is stored in a `LocalValue<bool>` so each coroutine has its own
 *  independent copy. Globally accessible because most callers do not have
 *  access to a ledger/rules context.
 *
 *  @return true if the `Number` normalization path is active.
 *  @see setSTNumberSwitchover, NumberSO
 */
bool
getSTNumberSwitchover();

/** Set the coroutine-local STNumber switchover flag.
 *
 *  Prefer the `NumberSO` RAII guard for scoped changes.
 *
 *  @param v true to enable the `Number` normalization path; false for legacy.
 *  @see getSTNumberSwitchover, NumberSO
 */
void
setSTNumberSwitchover(bool v);

/** RAII guard that temporarily overrides the coroutine-local STNumber
 *  switchover flag and restores the previous value on destruction.
 *
 *  Construct with `true` to force the `Number`-based normalization path, or
 *  `false` to force the legacy loop. Useful in tests and ledger-replay code
 *  that must exercise a specific path without global side effects.
 *
 *  @note Non-copyable; intended for stack-only use.
 *  @see getSTNumberSwitchover, setSTNumberSwitchover
 */
class NumberSO
{
    bool saved_;

public:
    ~NumberSO()
    {
        setSTNumberSwitchover(saved_);
    }

    NumberSO(NumberSO const&) = delete;
    NumberSO&
    operator=(NumberSO const&) = delete;

    explicit NumberSO(bool v) : saved_(getSTNumberSwitchover())
    {
        setSTNumberSwitchover(v);
    }
};

}  // namespace xrpl
