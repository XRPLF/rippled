#pragma once

#include <xrpl/basics/Number.h>

#include <benchmarks/libxrpl/number/NumberLike.h>

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

namespace xrpl::number_bench {

/**
 * Binary `double` behind Number's interface: a hardware speed-of-light
 * baseline, not a candidate (base 2, 53-bit significand).
 *
 * It is not DecomposableNumber because a binary value has no exact short
 * decimal coefficient.
 */
class Double final
{
    double value_ = 0.0;

    explicit constexpr Double(double value) noexcept : value_{value}
    {
    }

public:
    using rep = std::int64_t;

    constexpr Double() = default;

    // Implicit, like Number(rep).
    Double(rep mantissa) noexcept : value_{static_cast<double>(mantissa)}  // NOLINT
    {
    }

    explicit Double(rep mantissa, int exponent) noexcept
        : value_{static_cast<double>(mantissa) * std::pow(10.0, exponent)}
    {
    }

    constexpr Double
    operator-() const noexcept
    {
        return Double{-value_};
    }

    constexpr Double&
    operator+=(Double const& x) noexcept
    {
        value_ += x.value_;
        return *this;
    }

    constexpr Double&
    operator-=(Double const& x) noexcept
    {
        value_ -= x.value_;
        return *this;
    }

    constexpr Double&
    operator*=(Double const& x) noexcept
    {
        value_ *= x.value_;
        return *this;
    }

    constexpr Double&
    operator/=(Double const& x) noexcept
    {
        value_ /= x.value_;
        return *this;
    }

    friend constexpr Double
    operator+(Double const& x, Double const& y) noexcept
    {
        return Double{x.value_ + y.value_};
    }

    friend constexpr Double
    operator-(Double const& x, Double const& y) noexcept
    {
        return Double{x.value_ - y.value_};
    }

    friend constexpr Double
    operator*(Double const& x, Double const& y) noexcept
    {
        return Double{x.value_ * y.value_};
    }

    friend constexpr Double
    operator/(Double const& x, Double const& y) noexcept
    {
        return Double{x.value_ / y.value_};
    }

    friend constexpr bool
    operator==(Double const& x, Double const& y) noexcept
    {
        return x.value_ == y.value_;
    }

    friend constexpr auto
    operator<=>(Double const& x, Double const& y) noexcept
    {
        return x.value_ <=> y.value_;
    }

    explicit
    operator rep() const noexcept
    {
        return static_cast<rep>(std::llrint(value_));
    }

    static Number::RoundingMode
    getround() noexcept
    {
        using enum Number::RoundingMode;
        switch (std::fegetround())
        {
            case FE_TOWARDZERO:
                return TowardsZero;
            case FE_DOWNWARD:
                return Downward;
            case FE_UPWARD:
                return Upward;
            default:
                return ToNearest;
        }
    }

    static Number::RoundingMode
    setround(Number::RoundingMode mode) noexcept
    {
        using enum Number::RoundingMode;
        auto const old = getround();
        switch (mode)
        {
            case ToNearest:
                std::fesetround(FE_TONEAREST);
                break;
            case TowardsZero:
                std::fesetround(FE_TOWARDZERO);
                break;
            case Downward:
                std::fesetround(FE_DOWNWARD);
                break;
            case Upward:
                std::fesetround(FE_UPWARD);
                break;
        }
        return old;
    }

    friend std::string
    to_string(Double const& x)
    {
        std::ostringstream os;
        os.precision(17);
        os << x.value_;
        return os.str();
    }

    friend Double
    abs(Double const& x) noexcept
    {
        return Double{std::fabs(x.value_)};
    }

    // Repeated squaring, the same algorithm as xrpl::power(Number, unsigned).
    friend constexpr Double
    power(Double const& x, unsigned n) noexcept
    {
        double result = 1.0;
        double base = x.value_;
        for (; n != 0; n >>= 1)
        {
            if ((n & 1u) != 0)
                result *= base;
            base *= base;
        }
        return Double{result};
    }

    friend Double
    root2(Double const& x) noexcept
    {
        return Double{std::sqrt(x.value_)};
    }
};

static_assert(NumberLike<Double>);

}  // namespace xrpl::number_bench
