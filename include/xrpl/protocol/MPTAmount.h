/** @file
 *  Defines MPTAmount, the canonical signed-integer amount type for
 *  Multi-Purpose Tokens (MPTs) on the XRP Ledger.
 */

#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Zero.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/operators.hpp>

#include <cstdint>
#include <string>

namespace xrpl {

/** Typed signed-integer quantity for Multi-Purpose Tokens (MPTs).
 *
 *  MPT balances are plain whole-unit integers — no mantissa/exponent pair,
 *  no sub-unit naming — capped at `maxMPTokenAmount` (INT64_MAX) by the
 *  protocol.  The class sits alongside `XRPAmount` and `IOUAmount` as one
 *  of the three concrete amount types that satisfy the `StepAmount` concept
 *  used by the payment-path and DEX engines.
 *
 *  Arithmetic operators are composed via Boost.Operators (CRTP):
 *  - `boost::totally_ordered<MPTAmount>` — synthesizes `!=`, `>`, `>=`,
 *    `<=` from the declared `==` and `<`.
 *  - `boost::additive<MPTAmount>` — synthesizes binary `+`/`-` from
 *    `+=`/`-=`.
 *  - `boost::equality_comparable<MPTAmount, int64_t>` — heterogeneous `!=`
 *    from `operator==(value_type)`.
 *  - `boost::additive<MPTAmount, int64_t>` — heterogeneous `+`/`-` with
 *    raw integers.
 *
 *  Out-of-line `+=`, `-=`, `operator-()`, `==`, and `<` perform no overflow
 *  detection; callers are responsible for keeping balances in range through
 *  the ledger constraint machinery.  The safe multiplication path
 *  (`mulRatio`) uses 128-bit intermediates and throws on overflow.
 *
 *  @note `value_` is `protected` (not `private`) to allow subclassing
 *      without exposing the raw integer to unrelated code.  No subclasses
 *      exist in the current codebase.
 */
class MPTAmount : private boost::totally_ordered<MPTAmount>,
                  private boost::additive<MPTAmount>,
                  private boost::equality_comparable<MPTAmount, std::int64_t>,
                  private boost::additive<MPTAmount, std::int64_t>
{
public:
    /** Underlying integer type; matches `XRPAmount::value_type`. */
    using value_type = std::int64_t;

protected:
    value_type value_{};

public:
    MPTAmount() = default;
    constexpr MPTAmount(MPTAmount const& other) = default;

    /** Construct a zero amount from the `beast::Zero` sentinel.
     *
     *  Allows idiomatic zero-initialization via `beast::zero` in generic
     *  code that is templated on amount type.
     */
    constexpr MPTAmount(beast::Zero);
    constexpr MPTAmount&
    operator=(MPTAmount const& other) = default;

    /** Construct from a `Number`, rounding to nearest with ties to even.
     *
     *  Provides implicit compatibility with XRPL's high-precision arithmetic
     *  type.  The rounding mode matches IEEE 754 default (round-half-to-even).
     *
     *  @param x The `Number` value to convert.
     */
    explicit MPTAmount(Number const& x) : MPTAmount(static_cast<value_type>(x))
    {
    }

    /** Construct from a raw `int64_t` value.
     *
     *  Explicit to prevent accidental implicit conversion from integers.
     *  The caller is responsible for ensuring `value` does not exceed
     *  `maxMPTokenAmount` (INT64_MAX).
     *
     *  @param value The integer amount in whole MPT units.
     */
    constexpr explicit MPTAmount(value_type value);

    /** Assign the `beast::Zero` sentinel, setting the amount to zero. */
    constexpr MPTAmount& operator=(beast::Zero);

    /** Add `other` to this amount in place.
     *
     *  No overflow detection is performed; callers must ensure the result
     *  remains within `int64_t` range.
     *
     *  @param other The amount to add.
     *  @return Reference to `*this` after addition.
     */
    MPTAmount&
    operator+=(MPTAmount const& other);

    /** Subtract `other` from this amount in place.
     *
     *  No overflow detection is performed; callers must ensure the result
     *  remains within `int64_t` range.
     *
     *  @param other The amount to subtract.
     *  @return Reference to `*this` after subtraction.
     */
    MPTAmount&
    operator-=(MPTAmount const& other);

    /** Return the arithmetic negation of this amount.
     *
     *  Used where a credit and a debit are expressed as equal-magnitude
     *  amounts of opposite sign before being applied to the ledger.
     *  Negating `INT64_MIN` is undefined behavior; callers must avoid it.
     *
     *  @return A new `MPTAmount` equal to `-value_`.
     */
    MPTAmount
    operator-() const;

    /** Test equality with another `MPTAmount`.
     *
     *  Together with `operator<`, satisfies `boost::totally_ordered`,
     *  from which `!=`, `>`, `<=`, and `>=` are synthesized.
     *
     *  @param other The amount to compare against.
     *  @return `true` if both amounts hold the same integer value.
     */
    bool
    operator==(MPTAmount const& other) const;

    /** Test equality with a raw `int64_t` value.
     *
     *  Allows expressions like `amt == 0` without constructing a temporary.
     *  `boost::equality_comparable<MPTAmount, int64_t>` synthesizes the
     *  mixed-type `!=` from this overload.
     *
     *  @param other The raw integer value to compare against.
     *  @return `true` if `value_` equals `other`.
     */
    bool
    operator==(value_type other) const;

    /** Return `true` if this amount is strictly less than `other`.
     *
     *  The single total-order primitive from which `boost::totally_ordered`
     *  derives `>`, `<=`, and `>=`.  Signed comparison gives correct
     *  semantics for negative balances.
     *
     *  @param other The amount to compare against.
     *  @return `true` if `value_` is strictly less than `other.value_`.
     */
    bool
    operator<(MPTAmount const& other) const;

    /** Returns true if the amount is not zero. */
    explicit constexpr
    operator bool() const noexcept;

    /** Implicit conversion to `Number` for use in high-precision arithmetic.
     *
     *  Allows `MPTAmount` to be passed anywhere a `Number` is expected —
     *  arithmetic operations, rounding, and comparisons — without an explicit
     *  cast.  The reverse direction (construction from `Number`) is explicit.
     */
    operator Number() const noexcept
    {
        return value();
    }

    /** Return the sign of the amount.
     *
     *  @return `-1` if negative, `0` if zero, `1` if positive.
     */
    [[nodiscard]] constexpr int
    signum() const noexcept;

    /** Return the underlying integer value.
     *
     *  Code SHOULD NOT call this function unless the type has been abstracted
     *  away, e.g. in a templated function.  Prefer operating on `MPTAmount`
     *  directly to keep arithmetic in the typed domain.
     *
     *  @return The raw `int64_t` balance in whole MPT units.
     */
    [[nodiscard]] constexpr value_type
    value() const;

    /** Return the smallest positive MPT amount (one indivisible unit).
     *
     *  Provides a uniform factory interface shared with `XRPAmount` and
     *  `IOUAmount` so generic payment-path code can obtain the minimum
     *  step size without knowing the concrete amount type.
     *
     *  @return `MPTAmount{1}`.
     */
    static MPTAmount
    minPositiveAmount();
};

constexpr MPTAmount::MPTAmount(value_type value) : value_(value)
{
}

constexpr MPTAmount::MPTAmount(beast::Zero)
{
    *this = beast::kZERO;
}

constexpr MPTAmount&
MPTAmount::operator=(beast::Zero)
{
    value_ = 0;
    return *this;
}

constexpr MPTAmount::
operator bool() const noexcept
{
    return value_ != 0;
}

constexpr int
MPTAmount::signum() const noexcept
{
    if (value_ < 0)
        return -1;
    return (value_ != 0) ? 1 : 0;
}

constexpr MPTAmount::value_type
MPTAmount::value() const
{
    return value_;
}

/** Stream an `MPTAmount` as its raw integer value. */
template <class Char, class Traits>
std::basic_ostream<Char, Traits>&
operator<<(std::basic_ostream<Char, Traits>& os, MPTAmount const& q)
{
    return os << q.value();
}

/** Return the decimal string representation of an `MPTAmount`. */
inline std::string
to_string(MPTAmount const& amount)
{
    return std::to_string(amount.value());
}

/** Compute `amt * num / den` with configurable rounding direction.
 *
 *  The intermediate product is computed in 128-bit arithmetic to avoid
 *  overflow when multiplying a 63-bit MPT balance by a 32-bit numerator
 *  (up to 95 bits required).  After division, any remainder is resolved
 *  based on the sign of `amt` and `roundUp`:
 *  - Positive amounts round up when `roundUp` is `true`.
 *  - Negative amounts round away from zero (more negative) when `roundUp`
 *    is `false`.
 *
 *  Used for fee and reserve calculations that apply percentage-style ratios
 *  to MPT amounts.
 *
 *  @param amt     The base amount to scale.
 *  @param num     Numerator of the ratio (32-bit unsigned).
 *  @param den     Denominator of the ratio (32-bit unsigned, must be > 0).
 *  @param roundUp If `true`, round the result toward positive infinity;
 *      if `false`, round toward negative infinity.
 *  @return The scaled `MPTAmount`.
 *  @throws std::runtime_error  If `den` is zero.
 *  @throws std::overflow_error If the result exceeds `INT64_MAX`.
 */
inline MPTAmount
mulRatio(MPTAmount const& amt, std::uint32_t num, std::uint32_t den, bool roundUp)
{
    using namespace boost::multiprecision;

    if (den == 0u)
        Throw<std::runtime_error>("division by zero");

    int128_t const amt128(amt.value());
    auto const neg = amt.value() < 0;
    auto const m = amt128 * num;
    auto r = m / den;
    if (m % den)
    {
        if (!neg && roundUp)
            r += 1;
        if (neg && !roundUp)
            r -= 1;
    }
    if (r > std::numeric_limits<MPTAmount::value_type>::max())
        Throw<std::overflow_error>("MPT mulRatio overflow");
    return MPTAmount(r.convert_to<MPTAmount::value_type>());
}

}  // namespace xrpl
