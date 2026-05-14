/** @file
 *  Defines XRPAmount, the canonical type-safe representation of XRP in drops.
 *
 *  One XRP equals exactly 1,000,000 drops. All XRP quantities in the ledger
 *  are stored and computed as integer drop counts to avoid floating-point
 *  imprecision. The companion constant `kDROPS_PER_XRP` and the
 *  `mulRatio()` helper are defined here as well.
 */
#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Units.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/operators.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

namespace xrpl {

/** Type-safe representation of an XRP quantity in drops.
 *
 *  All XRP values are stored as integer drop counts (1 XRP = 1,000,000 drops).
 *  The class derives comparison, addition, and subtraction operators from
 *  `boost::operators` mixins; only `operator==`, `operator<`, `operator+=`,
 *  and `operator-=` are implemented directly, and the rest are generated.
 *  Mixed arithmetic with raw `std::int64_t` literals is supported for fee
 *  and reserve computations.
 *
 *  Negative drop values are representable and used in internal calculations;
 *  callers are responsible for ensuring ledger amounts are non-negative where
 *  required.
 *
 *  @note Satisfies the `unit::Valid` and `unit::Usable` concepts from
 *      `Units.h` via the `unit_type` / `value_type` member typedefs, but
 *      is not a `ValueUnit<>` specialization. Its value accessor is named
 *      `drops()`, not `value()`.
 *  @see IOUAmount, MPTAmount, STAmount, mulRatio()
 */
class XRPAmount : private boost::totally_ordered<XRPAmount>,
                  private boost::additive<XRPAmount>,
                  private boost::equality_comparable<XRPAmount, std::int64_t>,
                  private boost::additive<XRPAmount, std::int64_t>
{
public:
    /** Unit tag satisfying the `unit::Valid` / `unit::Usable` concepts. */
    using unit_type = unit::dropTag;

    /** Underlying integer type holding the drop count. */
    using value_type = std::int64_t;

private:
    value_type drops_;

public:
    XRPAmount() = default;
    constexpr XRPAmount(XRPAmount const& other) = default;
    constexpr XRPAmount&
    operator=(XRPAmount const& other) = default;

    /** Construct from a `Number`, rounding to nearest, with ties to even.
     *
     *  Used when high-precision `Number` arithmetic produces a result that
     *  must be expressed as an integer drop count, e.g. computed fees and
     *  reserves that may not fall exactly on integer boundaries.
     *
     *  @param x The `Number` value to convert.
     */
    explicit XRPAmount(Number const& x) : XRPAmount(static_cast<value_type>(x))
    {
    }

    /** Construct a zero-drop amount from a `beast::Zero` sentinel.
     *
     *  Enables use in generic contexts that pass `beast::zero` without
     *  knowing the concrete amount type, e.g. `XRPAmount x = beast::zero`.
     */
    constexpr XRPAmount(beast::Zero) : drops_(0)
    {
    }

    /** Assign zero drops from a `beast::Zero` sentinel. */
    constexpr XRPAmount&
    operator=(beast::Zero)
    {
        drops_ = 0;
        return *this;
    }

    /** Construct from a raw drop count.
     *
     *  Explicit to prevent silent integer-to-XRPAmount coercions.
     *
     *  @param drops The amount in drops.
     */
    constexpr explicit XRPAmount(value_type drops) : drops_(drops)
    {
    }

    /** Assign from a raw drop count.
     *
     *  @param drops The amount in drops.
     */
    XRPAmount&
    operator=(value_type drops)
    {
        drops_ = drops;
        return *this;
    }

    /** Scale by a scalar integer, returning a new amount.
     *
     *  Multiplication is exact (no rounding); overflow is unchecked.
     *  Use `mulRatio()` for ratio-based scaling with overflow detection.
     *
     *  @param rhs The integer multiplier.
     *  @return The scaled amount.
     */
    constexpr XRPAmount
    operator*(value_type const& rhs) const
    {
        return XRPAmount{drops_ * rhs};
    }

    /** Scale by a scalar integer (commutative overload). */
    friend constexpr XRPAmount
    operator*(value_type lhs, XRPAmount const& rhs)
    {
        // multiplication is commutative
        return rhs * lhs;
    }

    XRPAmount&
    operator+=(XRPAmount const& other)
    {
        drops_ += other.drops();
        return *this;
    }

    XRPAmount&
    operator-=(XRPAmount const& other)
    {
        drops_ -= other.drops();
        return *this;
    }

    XRPAmount&
    operator+=(value_type const& rhs)
    {
        drops_ += rhs;
        return *this;
    }

    XRPAmount&
    operator-=(value_type const& rhs)
    {
        drops_ -= rhs;
        return *this;
    }

    XRPAmount&
    operator*=(value_type const& rhs)
    {
        drops_ *= rhs;
        return *this;
    }

    /** Negate the amount (unary minus). */
    XRPAmount
    operator-() const
    {
        return XRPAmount{-drops_};
    }

    bool
    operator==(XRPAmount const& other) const
    {
        return drops_ == other.drops_;
    }

    bool
    operator==(value_type other) const
    {
        return drops_ == other;
    }

    bool
    operator<(XRPAmount const& other) const
    {
        return drops_ < other.drops_;
    }

    /** Returns true if the amount is not zero */
    explicit constexpr
    operator bool() const noexcept
    {
        return drops_ != 0;
    }

    /** Implicitly convert to `Number` for use in high-precision arithmetic.
     *
     *  Non-explicit so that `XRPAmount` values participate transparently in
     *  `Number`-based expressions used throughout the payment engine.
     */
    operator Number() const noexcept
    {
        return drops();
    }

    /** Return the sign of the amount as -1, 0, or +1.
     *
     *  Follows the mathematical signum convention. Used widely in path and
     *  transaction validation to detect non-positive amounts without branching
     *  on the raw value.
     *
     *  @return -1 if negative, 0 if zero, +1 if positive.
     */
    [[nodiscard]] constexpr int
    signum() const noexcept
    {
        if (drops_ < 0)
            return -1;
        return (drops_ != 0) ? 1 : 0;
    }

    /** Returns the number of drops */
    [[nodiscard]] constexpr value_type
    drops() const
    {
        return drops_;
    }

    /** Convert the drop count to a decimal XRP quantity.
     *
     *  Divides by `kDROPS_PER_XRP` (1,000,000) using floating-point
     *  arithmetic. The result is an approximation; do not use for
     *  ledger-critical calculations.
     *
     *  @return The equivalent XRP amount as a `double`.
     */
    [[nodiscard]] constexpr double
    decimalXRP() const;

    /** Safely narrow the drop count to a smaller integer type.
     *
     *  Returns `std::nullopt` if the value cannot be represented in `Dest`:
     *  - value exceeds `std::numeric_limits<Dest>::max()`, or
     *  - `Dest` is unsigned and the drop count is negative, or
     *  - `Dest` is signed and the value is below `Dest::lowest()`.
     *
     *  Primarily used when serializing fee fields that use 32-bit wire types.
     *
     *  @tparam Dest The target integer type.
     *  @return The drop count as `Dest`, or `std::nullopt` on range error.
     */
    template <class Dest>
    [[nodiscard]] std::optional<Dest>
    dropsAs() const
    {
        if ((drops_ > std::numeric_limits<Dest>::max()) ||
            (!std::numeric_limits<Dest>::is_signed && drops_ < 0) ||
            (std::numeric_limits<Dest>::is_signed && drops_ < std::numeric_limits<Dest>::lowest()))
        {
            return std::nullopt;
        }
        return static_cast<Dest>(drops_);
    }

    /** Safely narrow the drop count, returning a default on range error.
     *
     *  @tparam Dest The target integer type.
     *  @param defaultValue Value to return when the drop count is out of range.
     *  @return The drop count as `Dest`, or `defaultValue` on range error.
     */
    template <class Dest>
    Dest
    dropsAs(Dest defaultValue) const
    {
        return dropsAs<Dest>().value_or(defaultValue);
    }

    /** Safely narrow the drop count, using another `XRPAmount` as the fallback.
     *
     *  @tparam Dest The target integer type.
     *  @param defaultValue `XRPAmount` whose drop count is returned on range error.
     *  @return The drop count as `Dest`, or `defaultValue.drops()` on range error.
     */
    template <class Dest>
    [[nodiscard]] Dest
    dropsAs(XRPAmount defaultValue) const
    {
        return dropsAs<Dest>().value_or(defaultValue.drops());
    }

    /** Saturate-cast the drop count to a 32-bit JSON integer.
     *
     *  Clamps values outside `[json::Int::min(), json::Int::max()]` to the
     *  respective bound rather than throwing or wrapping. Only valid in
     *  contexts where the value is never expected to approach 32-bit limits,
     *  specifically fees and reserves.
     *
     *  @return A `json::Value` containing the (possibly clamped) drop count.
     */
    [[nodiscard]] json::Value
    jsonClipped() const
    {
        static_assert(
            std::is_signed_v<value_type> && std::is_integral_v<value_type>,
            "Expected XRPAmount to be a signed integral type");

        constexpr auto kMIN = std::numeric_limits<json::Int>::min();
        constexpr auto kMAX = std::numeric_limits<json::Int>::max();

        if (drops_ < kMIN)
            return kMIN;
        if (drops_ > kMAX)
            return kMAX;
        return static_cast<json::Int>(drops_);
    }

    /** Returns the underlying value. Code SHOULD NOT call this
        function unless the type has been abstracted away,
        e.g. in a templated function.
    */
    [[nodiscard]] constexpr value_type
    value() const
    {
        return drops_;
    }

    /** Read a drop count from a stream into this amount. */
    friend std::istream&
    operator>>(std::istream& s, XRPAmount& val)
    {
        s >> val.drops_;
        return s;
    }

    /** Return the smallest representable positive XRP amount (1 drop).
     *
     *  Used as a dust threshold: offers whose effective in- or out-amount
     *  does not exceed this value are considered stale and removed.
     *
     *  @return `XRPAmount{1}`.
     */
    static XRPAmount
    minPositiveAmount()
    {
        return XRPAmount{1};
    }
};

/** Number of drops per 1 XRP (1,000,000).
 *
 *  Declared as `constexpr XRPAmount` rather than a plain integer so it
 *  participates in the type system and prevents inadvertent mixing with
 *  unrelated integer values.
 */
constexpr XRPAmount kDROPS_PER_XRP{1'000'000};

constexpr double
XRPAmount::decimalXRP() const
{
    return static_cast<double>(drops_) / kDROPS_PER_XRP.drops();
}

/** Write the drop count of an `XRPAmount` to an output stream. */
template <class Char, class Traits>
std::basic_ostream<Char, Traits>&
operator<<(std::basic_ostream<Char, Traits>& os, XRPAmount const& q)
{
    return os << q.drops();
}

/** Return a decimal string representation of the drop count. */
inline std::string
to_string(XRPAmount const& amount)
{
    return std::to_string(amount.drops());
}

/** Scale an XRPAmount by the rational factor `num / den` with controlled rounding.
 *
 *  Uses a 128-bit intermediate product to avoid overflow when multiplying a
 *  64-bit drop count by a 32-bit numerator. After division, sign-aware
 *  rounding is applied: for a positive amount with a nonzero remainder,
 *  `roundUp=true` increments the result by one drop; for a negative amount,
 *  `roundUp=false` decrements (moves toward more negative), maintaining
 *  symmetric semantics across signs.
 *
 *  @param amt     The amount to scale.
 *  @param num     Numerator of the scaling ratio (32-bit, treated as unsigned).
 *  @param den     Denominator of the scaling ratio (must be non-zero).
 *  @param roundUp When `true`, round away from zero (positive) or toward
 *      more-negative (negative) on a fractional remainder.
 *  @return The scaled amount.
 *  @throws std::runtime_error  if `den == 0`.
 *  @throws std::overflow_error if the result exceeds `XRPAmount::value_type` range.
 */
inline XRPAmount
mulRatio(XRPAmount const& amt, std::uint32_t num, std::uint32_t den, bool roundUp)
{
    using namespace boost::multiprecision;

    if (den == 0u)
        Throw<std::runtime_error>("division by zero");

    int128_t const amt128(amt.drops());
    auto const neg = amt.drops() < 0;
    auto const m = amt128 * num;
    auto r = m / den;
    if (m % den)
    {
        if (!neg && roundUp)
            r += 1;
        if (neg && !roundUp)
            r -= 1;
    }
    if (r > std::numeric_limits<XRPAmount::value_type>::max())
        Throw<std::overflow_error>("XRP mulRatio overflow");
    return XRPAmount(r.convert_to<XRPAmount::value_type>());
}

}  // namespace xrpl
