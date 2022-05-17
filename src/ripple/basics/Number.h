//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_NUMBER_H_INCLUDED
#define RIPPLE_BASICS_NUMBER_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/XRPAmount.h>
#include <cstdint>
#include <ostream>
#include <string>

namespace ripple {

class Number;

std::string
to_string(Number const& amount);

class Number
{
    using rep = std::int64_t;
    rep mantissa_{0};
    int exponent_{std::numeric_limits<int>::lowest()};

public:
    struct unchecked
    {
        explicit unchecked() = default;
    };

    explicit constexpr Number() = default;

    Number(rep mantissa);
    explicit Number(rep mantissa, int exponent);
    explicit constexpr Number(rep mantissa, int exponent, unchecked) noexcept;

    Number(IOUAmount const& x);
    Number(XRPAmount const& x);

    constexpr rep
    mantissa() const noexcept;
    constexpr int
    exponent() const noexcept;

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

    explicit operator IOUAmount() const;
    explicit operator XRPAmount() const;  // round to nearest, even on tie
    explicit operator rep() const;        // round to nearest, even on tie

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

private:
    void
    normalize();
    constexpr bool
    isnormal() const noexcept;

    // The range for the mantissa when normalized
    constexpr static std::int64_t minMantissa = 1'000'000'000'000'000LL;
    constexpr static std::int64_t maxMantissa = 9'999'999'999'999'999LL;

    // The range for the exponent when normalized
    constexpr static int minExponent = -32768;
    constexpr static int maxExponent = 32768;

    class Guard;
};

inline constexpr Number::Number(rep mantissa, int exponent, unchecked) noexcept
    : mantissa_{mantissa}, exponent_{exponent}
{
}

inline Number::Number(rep mantissa, int exponent)
    : mantissa_{mantissa}, exponent_{exponent}
{
    normalize();
}

inline Number::Number(rep mantissa) : Number{mantissa, 0}
{
}

inline Number::Number(IOUAmount const& x) : Number{x.mantissa(), x.exponent()}
{
}

inline Number::Number(XRPAmount const& x) : Number{x.drops()}
{
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

inline Number::operator IOUAmount() const
{
    return IOUAmount{mantissa(), exponent()};
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

}  // namespace ripple

#endif  // RIPPLE_BASICS_NUMBER_H_INCLUDED
