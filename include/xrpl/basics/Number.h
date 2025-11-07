#ifndef XRPL_BASICS_NUMBER_H_INCLUDED
#define XRPL_BASICS_NUMBER_H_INCLUDED

#include <cstdint>
#include <limits>
#include <ostream>
#include <string>

namespace ripple {

class Number;

std::string
to_string(Number const& amount);

template <typename T>
constexpr bool
isPowerOfTen(T value)
{
    while (value >= 10 && value % 10 == 0)
        value /= 10;
    return value == 1;
}

class Number
{
public:
    /** Describes whether and how to enforce this number as an integer.
     *
     * - none: No enforcement. The value may vary freely. This is the default.
     * - weak: If the absolute value is greater than maxIntValue, valid() will
     *   return false.
     * - strong: Assignment operations will throw if the absolute value is above
     *   maxIntValue.
     */
    enum EnforceInteger { none, weak, strong };

private:
    using rep = std::int64_t;
    rep mantissa_{0};
    int exponent_{std::numeric_limits<int>::lowest()};

    // The enforcement setting is not serialized, and does not affect the
    // ledger. If not "none", the value is checked to be within the valid
    // integer range. With "strong", the checks will be made as automatic as
    // possible.
    EnforceInteger enforceInteger_ = none;

public:
    // The range for the mantissa when normalized
    constexpr static rep minMantissa = 1'000'000'000'000'000LL;
    static_assert(isPowerOfTen(minMantissa));
    constexpr static rep maxMantissa = minMantissa * 10 - 1;
    static_assert(maxMantissa == 9'999'999'999'999'999LL);

    constexpr static rep maxIntValue = maxMantissa / 10;
    static_assert(maxIntValue == 999'999'999'999'999LL);

    // The range for the exponent when normalized
    constexpr static int minExponent = -32768;
    constexpr static int maxExponent = 32768;

    struct unchecked
    {
        explicit unchecked() = default;
    };

    explicit constexpr Number() = default;

    Number(rep mantissa, EnforceInteger enforce = none);
    explicit Number(rep mantissa, int exponent, EnforceInteger enforce = none);
    explicit constexpr Number(rep mantissa, int exponent, unchecked) noexcept;
    constexpr Number(Number const& other) = default;
    constexpr Number(Number&& other) = default;

    ~Number() = default;

    constexpr Number&
    operator=(Number const& other);
    constexpr Number&
    operator=(Number&& other);

    constexpr rep
    mantissa() const noexcept;
    constexpr int
    exponent() const noexcept;

    void
    setIntegerEnforcement(EnforceInteger enforce);

    EnforceInteger
    integerEnforcement() const noexcept;

    bool
    valid() const noexcept;

    constexpr Number
    operator+() const noexcept;
    constexpr Number
    operator-() const noexcept;
    Number&
    operator++();
    Number
    operator++(int);
    Number&
    operator--();
    Number
    operator--(int);

    Number&
    operator+=(Number const& x);
    Number&
    operator-=(Number const& x);

    Number&
    operator*=(Number const& x);
    Number&
    operator/=(Number const& x);

    static constexpr Number
    min() noexcept;
    static constexpr Number
    max() noexcept;
    static constexpr Number
    lowest() noexcept;

    /** Conversions to Number are implicit and conversions away from Number
     *  are explicit. This design encourages and facilitates the use of Number
     *  as the preferred type for floating point arithmetic as it makes
     *  "mixed mode" more convenient, e.g. MPTAmount + Number.
     */
    explicit
    operator rep() const;  // round to nearest, even on tie

    friend constexpr bool
    operator==(Number const& x, Number const& y) noexcept
    {
        return x.mantissa_ == y.mantissa_ && x.exponent_ == y.exponent_;
    }

    friend constexpr bool
    operator!=(Number const& x, Number const& y) noexcept
    {
        return !(x == y);
    }

    friend constexpr bool
    operator<(Number const& x, Number const& y) noexcept
    {
        // If the two amounts have different signs (zero is treated as positive)
        // then the comparison is true iff the left is negative.
        bool const lneg = x.mantissa_ < 0;
        bool const rneg = y.mantissa_ < 0;

        if (lneg != rneg)
            return lneg;

        // Both have same sign and the left is zero: the right must be
        // greater than 0.
        if (x.mantissa_ == 0)
            return y.mantissa_ > 0;

        // Both have same sign, the right is zero and the left is non-zero.
        if (y.mantissa_ == 0)
            return false;

        // Both have the same sign, compare by exponents:
        if (x.exponent_ > y.exponent_)
            return lneg;
        if (x.exponent_ < y.exponent_)
            return !lneg;

        // If equal exponents, compare mantissas
        return x.mantissa_ < y.mantissa_;
    }

    /** Return the sign of the amount */
    constexpr int
    signum() const noexcept
    {
        return (mantissa_ < 0) ? -1 : (mantissa_ ? 1 : 0);
    }

    Number
    truncate() const noexcept;

    friend constexpr bool
    operator>(Number const& x, Number const& y) noexcept
    {
        return y < x;
    }

    friend constexpr bool
    operator<=(Number const& x, Number const& y) noexcept
    {
        return !(y < x);
    }

    friend constexpr bool
    operator>=(Number const& x, Number const& y) noexcept
    {
        return !(x < y);
    }

    friend std::ostream&
    operator<<(std::ostream& os, Number const& x)
    {
        return os << to_string(x);
    }

    // Thread local rounding control.  Default is to_nearest
    enum rounding_mode { to_nearest, towards_zero, downward, upward };
    static rounding_mode
    getround();
    // Returns previously set mode
    static rounding_mode
    setround(rounding_mode mode);

private:
    static thread_local rounding_mode mode_;

    void
    checkInteger(char const* what) const;

    void
    normalize();
    constexpr bool
    isnormal() const noexcept;

    class Guard;
};

constexpr static Number numZero{};

inline constexpr Number::Number(rep mantissa, int exponent, unchecked) noexcept
    : mantissa_{mantissa}, exponent_{exponent}
{
}

inline Number::Number(rep mantissa, int exponent, EnforceInteger enforce)
    : mantissa_{mantissa}, exponent_{exponent}, enforceInteger_(enforce)
{
    normalize();

    checkInteger("Number::Number integer overflow");
}

inline Number::Number(rep mantissa, EnforceInteger enforce)
    : Number{mantissa, 0, enforce}
{
}

constexpr Number&
Number::operator=(Number const& other)
{
    if (this != &other)
    {
        mantissa_ = other.mantissa_;
        exponent_ = other.exponent_;
        enforceInteger_ = std::max(enforceInteger_, other.enforceInteger_);

        checkInteger("Number::operator= integer overflow");
    }

    return *this;
}

constexpr Number&
Number::operator=(Number&& other)
{
    if (this != &other)
    {
        // std::move doesn't really do anything for these types, but
        // this is future-proof in case the types ever change
        mantissa_ = std::move(other.mantissa_);
        exponent_ = std::move(other.exponent_);
        if (other.enforceInteger_ > enforceInteger_)
            enforceInteger_ = std::move(other.enforceInteger_);

        checkInteger("Number::operator= integer overflow");
    }

    return *this;
}

inline constexpr Number::rep
Number::mantissa() const noexcept
{
    return mantissa_;
}

inline constexpr int
Number::exponent() const noexcept
{
    return exponent_;
}

inline void
Number::setIntegerEnforcement(EnforceInteger enforce)
{
    enforceInteger_ = enforce;

    checkInteger("Number::setIntegerEnforcement integer overflow");
}

inline Number::EnforceInteger
Number::integerEnforcement() const noexcept
{
    return enforceInteger_;
}

inline constexpr Number
Number::operator+() const noexcept
{
    return *this;
}

inline constexpr Number
Number::operator-() const noexcept
{
    auto x = *this;
    x.mantissa_ = -x.mantissa_;
    return x;
}

inline Number&
Number::operator++()
{
    *this += Number{1000000000000000, -15, unchecked{}};
    return *this;
}

inline Number
Number::operator++(int)
{
    auto x = *this;
    ++(*this);
    return x;
}

inline Number&
Number::operator--()
{
    *this -= Number{1000000000000000, -15, unchecked{}};
    return *this;
}

inline Number
Number::operator--(int)
{
    auto x = *this;
    --(*this);
    return x;
}

inline Number&
Number::operator-=(Number const& x)
{
    return *this += -x;
}

inline Number
operator+(Number const& x, Number const& y)
{
    auto z = x;
    z += y;
    return z;
}

inline Number
operator-(Number const& x, Number const& y)
{
    auto z = x;
    z -= y;
    return z;
}

inline Number
operator*(Number const& x, Number const& y)
{
    auto z = x;
    z *= y;
    return z;
}

inline Number
operator/(Number const& x, Number const& y)
{
    auto z = x;
    z /= y;
    return z;
}

inline constexpr Number
Number::min() noexcept
{
    return Number{minMantissa, minExponent, unchecked{}};
}

inline constexpr Number
Number::max() noexcept
{
    return Number{maxMantissa, maxExponent, unchecked{}};
}

inline constexpr Number
Number::lowest() noexcept
{
    return -Number{maxMantissa, maxExponent, unchecked{}};
}

inline constexpr bool
Number::isnormal() const noexcept
{
    auto const abs_m = mantissa_ < 0 ? -mantissa_ : mantissa_;
    return minMantissa <= abs_m && abs_m <= maxMantissa &&
        minExponent <= exponent_ && exponent_ <= maxExponent;
}

inline constexpr Number
abs(Number x) noexcept
{
    if (x < Number{})
        x = -x;
    return x;
}

// Returns f^n
// Uses a log_2(n) number of multiplications

Number
power(Number const& f, unsigned n);

// Returns f^(1/d)
// Uses Newton–Raphson iterations until the result stops changing
// to find the root of the polynomial g(x) = x^d - f

Number
root(Number f, unsigned d);

Number
root2(Number f);

// Returns f^(n/d)

Number
power(Number const& f, unsigned n, unsigned d);

// Return 0 if abs(x) < limit, else returns x

inline constexpr Number
squelch(Number const& x, Number const& limit) noexcept
{
    if (abs(x) < limit)
        return Number{};
    return x;
}

class saveNumberRoundMode
{
    Number::rounding_mode mode_;

public:
    ~saveNumberRoundMode()
    {
        Number::setround(mode_);
    }
    explicit saveNumberRoundMode(Number::rounding_mode mode) noexcept
        : mode_{mode}
    {
    }
    saveNumberRoundMode(saveNumberRoundMode const&) = delete;
    saveNumberRoundMode&
    operator=(saveNumberRoundMode const&) = delete;
};

// saveNumberRoundMode doesn't do quite enough for us.  What we want is a
// Number::RoundModeGuard that sets the new mode and restores the old mode
// when it leaves scope.  Since Number doesn't have that facility, we'll
// build it here.
class NumberRoundModeGuard
{
    saveNumberRoundMode saved_;

public:
    explicit NumberRoundModeGuard(Number::rounding_mode mode) noexcept
        : saved_{Number::setround(mode)}
    {
    }

    NumberRoundModeGuard(NumberRoundModeGuard const&) = delete;

    NumberRoundModeGuard&
    operator=(NumberRoundModeGuard const&) = delete;
};

}  // namespace ripple

#endif  // XRPL_BASICS_NUMBER_H_INCLUDED
