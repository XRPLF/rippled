#pragma once

#include <xrpl/basics/Number.h>

#include <boost/decimal.hpp>

#include <benchmarks/libxrpl/number/NumberLike.h>

#include <array>
#include <cstdint>
#include <string>

namespace xrpl::number_bench {

namespace detail {

constexpr boost::decimal::rounding_mode
toBoost(Number::RoundingMode mode)
{
    using enum Number::RoundingMode;
    using boost::decimal::rounding_mode;
    switch (mode)
    {
        case ToNearest:
            return rounding_mode::fe_dec_to_nearest;
        case TowardsZero:
            return rounding_mode::fe_dec_toward_zero;
        case Downward:
            return rounding_mode::fe_dec_downward;
        case Upward:
            return rounding_mode::fe_dec_upward;
    }
    return rounding_mode::fe_dec_to_nearest;
}

constexpr Number::RoundingMode
fromBoost(boost::decimal::rounding_mode mode)
{
    using enum Number::RoundingMode;
    using boost::decimal::rounding_mode;
    switch (mode)
    {
        case rounding_mode::fe_dec_toward_zero:
            return TowardsZero;
        case rounding_mode::fe_dec_downward:
            return Downward;
        case rounding_mode::fe_dec_upward:
            return Upward;
        default:
            return ToNearest;
    }
}

}  // namespace detail

/**
 * Wraps a Boost.Decimal type behind Number's interface.
 *
 * Every member is a one-line inline forward, so at -O2 and above the wrapper
 * adds no cost over using the Boost type directly.
 *
 * Semantic differences from Number that the wrapper deliberately does NOT
 * hide (they are part of what we are measuring):
 *   - Overflow produces infinity and division by zero produces inf/NaN
 *     instead of throwing.
 *   - The rounding mode is a process-wide global in Boost.Decimal, not
 *     thread-local like Number's.
 *   - decimal64_t / decimal_fast64_t carry 16 digits, so constructing from a
 *     19-digit int64 mantissa rounds.
 */
template <class D>
class BoostDecimal final
{
    D value_{};

    explicit constexpr BoostDecimal(D value) noexcept : value_{value}
    {
    }

public:
    using rep = std::int64_t;
    using value_type = D;

    constexpr BoostDecimal() = default;

    // Implicit, like Number(rep).
    constexpr BoostDecimal(rep mantissa) noexcept : value_{mantissa, 0}  // NOLINT
    {
    }

    explicit constexpr BoostDecimal(rep mantissa, int exponent) noexcept
        : value_{mantissa, exponent}
    {
    }

    [[nodiscard]] constexpr D
    value() const noexcept
    {
        return value_;
    }

    constexpr BoostDecimal
    operator-() const noexcept
    {
        return BoostDecimal{-value_};
    }

    constexpr BoostDecimal&
    operator+=(BoostDecimal const& x) noexcept
    {
        value_ += x.value_;
        return *this;
    }

    constexpr BoostDecimal&
    operator-=(BoostDecimal const& x) noexcept
    {
        value_ -= x.value_;
        return *this;
    }

    constexpr BoostDecimal&
    operator*=(BoostDecimal const& x) noexcept
    {
        value_ *= x.value_;
        return *this;
    }

    constexpr BoostDecimal&
    operator/=(BoostDecimal const& x) noexcept
    {
        value_ /= x.value_;
        return *this;
    }

    friend constexpr BoostDecimal
    operator+(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return BoostDecimal{x.value_ + y.value_};
    }

    friend constexpr BoostDecimal
    operator-(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return BoostDecimal{x.value_ - y.value_};
    }

    friend constexpr BoostDecimal
    operator*(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return BoostDecimal{x.value_ * y.value_};
    }

    friend constexpr BoostDecimal
    operator/(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return BoostDecimal{x.value_ / y.value_};
    }

    friend constexpr bool
    operator==(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return x.value_ == y.value_;
    }

    friend constexpr bool
    operator<(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return x.value_ < y.value_;
    }

    friend constexpr bool
    operator>(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return y < x;
    }

    friend constexpr bool
    operator<=(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return !(y < x);
    }

    friend constexpr bool
    operator>=(BoostDecimal const& x, BoostDecimal const& y) noexcept
    {
        return !(x < y);
    }

    // Round to an integer using the current rounding mode, like Number.
    explicit
    operator rep() const noexcept
    {
        // Boost 1.91's llrint mishandles values that are already integers at full
        // precision (significand exponent >= 0): it returns a wrong result, or
        // divides by zero when the exponent is exactly 0. Fixed upstream (the
        // same early return as here); remove this once Boost includes the fix.
        int exponent = 0;
        boost::decimal::frexp10(value_, &exponent);
        if (exponent >= 0)
            return static_cast<rep>(value_);
        return static_cast<rep>(boost::decimal::llrint(value_));
    }

    static Number::RoundingMode
    getround() noexcept
    {
        return detail::fromBoost(boost::decimal::fegetround());
    }

    // Returns the previous mode, like Number::setround.
    static Number::RoundingMode
    setround(Number::RoundingMode mode) noexcept
    {
        auto const old = getround();
        boost::decimal::fesetround(detail::toBoost(mode));
        return old;
    }

    friend std::string
    to_string(BoostDecimal const& x)
    {
        std::array<char, 64> buffer{};
        auto const result =
            boost::decimal::to_chars(buffer.data(), buffer.data() + buffer.size(), x.value_);
        return {buffer.data(), result.ptr};
    }

    friend constexpr BoostDecimal
    abs(BoostDecimal const& x) noexcept
    {
        return BoostDecimal{boost::decimal::abs(x.value_)};
    }

    friend constexpr BoostDecimal
    power(BoostDecimal const& x, unsigned n) noexcept
    {
        return BoostDecimal{boost::decimal::pow(x.value_, n)};
    }

    friend constexpr BoostDecimal
    root2(BoostDecimal const& x) noexcept
    {
        return BoostDecimal{boost::decimal::sqrt(x.value_)};
    }

    friend constexpr Decomposed
    decompose(BoostDecimal const& x) noexcept
    {
        int exponent = 0;
        auto const coefficient = boost::decimal::frexp10(x.value_, &exponent);
        return {
            .negative = boost::decimal::signbit(x.value_),
            .coefficient = static_cast<unsigned __int128>(coefficient),
            .exponent = exponent};
    }
};

using BoostDecimal64 = BoostDecimal<boost::decimal::decimal64_t>;
using BoostDecimal128 = BoostDecimal<boost::decimal::decimal128_t>;
using BoostDecimalFast64 = BoostDecimal<boost::decimal::decimal_fast64_t>;
using BoostDecimalFast128 = BoostDecimal<boost::decimal::decimal_fast128_t>;

static_assert(DecomposableNumber<BoostDecimal64>);
static_assert(DecomposableNumber<BoostDecimal128>);
static_assert(DecomposableNumber<BoostDecimalFast64>);
static_assert(DecomposableNumber<BoostDecimalFast128>);

}  // namespace xrpl::number_bench
