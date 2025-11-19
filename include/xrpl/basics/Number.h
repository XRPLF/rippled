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
    using rep = std::int64_t;
    rep mantissa_{0};
    int exponent_{std::numeric_limits<int>::lowest()};

    // Within Number itself limited_ is informational only. It is not
    // serialized, transmitted, or used in calculations in any way. It is used
    // only to indicate that a given Number's absolute value _might_ need to be
    // less than maxIntValue or maxMantissa.
    //
    // It is a one-way switch. Once it's on, it stays on. It is also
    // transmissible in that any operation (e.g +, /, power, etc.) involving a
    // Number with this flag will have a result with this flag.
    //
    // The flag is checked in the following places:
    // 1. "fits()" indicates whether the Number fits into the safe range of
    //    -maxIntValue to maxIntValue.
    // 2. "representable()" indicates whether the Number can accurately
    //    represent an integer, meaning that it fits withing the allowable range
    //    of -maxMantissa to maxMantissa. Values larger than this will be
    //    truncated before the decimal point, rendering the value inaccurate.
    // 3. In "operator rep()", which explicitly converts the number into a
    //    64-bit integer, if the Number is not representable(), AND one of the
    //    SingleAssetVault (or LendingProtocol, coming soon) amendments are
    //    enabled, the operator will throw a "std::overflow_error" as if the
    //    number had overflowed the limits of the 64-bit integer range.
    //
    // The Number is usually only going to be checked in transactions, based on
    // the specific transaction logic, and is entirely context dependent.
    //
    bool limited_ = false;

public:
    // The range for the mantissa when normalized
    constexpr static rep minMantissa = 1'000'000'000'000'000LL;
    static_assert(isPowerOfTen(minMantissa));
    constexpr static rep maxMantissa = minMantissa * 10 - 1;
    static_assert(maxMantissa == 9'999'999'999'999'999LL);

    constexpr static rep maxIntValue = maxMantissa / 100;
    static_assert(maxIntValue == 99'999'999'999'999LL);

    // The range for the exponent when normalized
    constexpr static int minExponent = -32768;
    constexpr static int maxExponent = 32768;

    struct unchecked
    {
        explicit unchecked() = default;
    };

    explicit constexpr Number() = default;

    Number(rep mantissa, bool limited = false);
    explicit Number(rep mantissa, int exponent, bool limited = false);
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

    // Sets the limited_ flag. See the description of limited for how it works.
    // Note that the flag can only change from false to true, not from true to
    // false.
    void
    setLimited(bool limited);

    // Gets the current value of the limited_ flag. See the description of
    // limited for how it works.
    bool
    getLimited() const noexcept;

    // 1. "fits()" indicates whether the Number fits into the safe range of
    //    -maxIntValue to maxIntValue.
    bool
    fits() const noexcept;
    bool
    // 2. "representable()" indicates whether the Number can accurately
    //    represent an integer, meaning that it fits withing the allowable range
    //    of -maxMantissa to maxMantissa. Values larger than this will be
    //    truncated before the decimal point, rendering the value inaccurate.
    representable() const noexcept;
    /// Combines setLimited(bool) and fits()
    bool
    fits(bool limited);
    /// Because this function is const, it should only be used for one-off
    /// checks
    bool
    fits(bool limited) const;

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

         3. In "operator rep()", which explicitly converts the number into a
            64-bit integer, if the Number is not representable(), AND one of the
            SingleAssetVault (or LendingProtocol, coming soon) amendments are
            enabled, the operator will throw a "std::overflow_error" as if the
            number had overflowed the limits of the 64-bit integer range.
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
    truncate() const noexcept
    {
        if (exponent_ >= 0 || mantissa_ == 0)
            return *this;

        Number ret = *this;
        while (ret.exponent_ < 0 && ret.mantissa_ != 0)
        {
            ret.exponent_ += 1;
            ret.mantissa_ /= rep(10);
        }
        // We are guaranteed that normalize() will never throw an exception
        // because exponent is either negative or zero at this point.
        ret.normalize();
        return ret;
    }

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

    // Thread local integer overflow control. See overflowLargeIntegers_ for
    // more info.
    static bool
    getEnforceIntegerOverflow();
    // Thread local integer overflow control. See overflowLargeIntegers_ for
    // more info.
    static void
    setEnforceIntegerOverflow(bool enforce);

private:
    static thread_local rounding_mode mode_;

    // This flag defaults to false. It is set and cleared by
    // "setCurrentTransactionRules" in Rules.cpp. It will be set to true if and
    // only if any of the SingleAssetVault or LendingProtocol amendments are
    // enabled.
    //
    // If set, then any explicit conversions from Number to rep (which is
    // std::int64_t) of Numbers that are not representable (which means their
    // magnitude is larger than maxMantissa, and thus they will lose integer
    // precision) will throw a std::overflow_error. Note that this coversion
    // will already throw an overflow error if the Number is larger 2^63.
    // See also "operator rep()".
    static thread_local bool overflowLargeIntegers_;

    void
    normalize();
    constexpr bool
    isnormal() const noexcept;

    class Guard;
};

inline constexpr Number::Number(rep mantissa, int exponent, unchecked) noexcept
    : mantissa_{mantissa}, exponent_{exponent}
{
}

inline Number::Number(rep mantissa, int exponent, bool limited)
    : mantissa_{mantissa}, exponent_{exponent}, limited_(limited)
{
    normalize();
}

inline Number::Number(rep mantissa, bool limited) : Number{mantissa, 0, limited}
{
}

constexpr Number&
Number::operator=(Number const& other)
{
    if (this != &other)
    {
        mantissa_ = other.mantissa_;
        exponent_ = other.exponent_;
        if (!limited_)
            limited_ = other.limited_;
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
        if (!limited_)
            limited_ = std::move(other.limited_);
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
Number::setLimited(bool limited)
{
    if (limited_)
        return;
    limited_ = limited;
}

inline bool
Number::getLimited() const noexcept
{
    return limited_;
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
