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

#include <ripple/basics/Number.h>
#include <algorithm>
#include <cassert>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

#ifdef BOOST_COMP_MSVC
#include <boost/multiprecision/cpp_int.hpp>
using uint128_t = boost::multiprecision::uint128_t;
#else   // !defined(_MSVC_LANG)
using uint128_t = __uint128_t;
#endif  // !defined(_MSVC_LANG)

namespace ripple {

thread_local Number::rounding_mode Number::mode_ = Number::to_nearest;

Number::rounding_mode
Number::getround()
{
    return mode_;
}

Number::rounding_mode
Number::setround(rounding_mode mode)
{
    return std::exchange(mode_, mode);
}

// Guard

// The Guard class is used to tempoarily add extra digits of
// preicision to an operation.  This enables the final result
// to be correctly rounded to the internal precision of Number.

class Number::Guard
{
    std::uint64_t digits_;   // 16 decimal guard digits
    std::uint8_t xbit_ : 1;  // has a non-zero digit been shifted off the end
    std::uint8_t sbit_ : 1;  // the sign of the guard digits

public:
    explicit Guard() : digits_{0}, xbit_{0}, sbit_{0}
    {
    }

    // set & test the sign bit
    void
    set_positive() noexcept;
    void
    set_negative() noexcept;
    bool
    is_negative() const noexcept;

    // add a digit
    void
    push(unsigned d) noexcept;

    // recover a digit
    unsigned
    pop() noexcept;

    // Indicate round direction:  1 is up, -1 is down, 0 is even
    // This enables the client to round towards nearest, and on
    // tie, round towards even.
    int
    round() noexcept;
};

inline void
Number::Guard::set_positive() noexcept
{
    sbit_ = 0;
}

inline void
Number::Guard::set_negative() noexcept
{
    sbit_ = 1;
}

inline bool
Number::Guard::is_negative() const noexcept
{
    return sbit_ == 1;
}

inline void
Number::Guard::push(unsigned d) noexcept
{
    xbit_ = xbit_ || (digits_ & 0x0000'0000'0000'000F) != 0;
    digits_ >>= 4;
    digits_ |= (d & 0x0000'0000'0000'000FULL) << 60;
}

inline unsigned
Number::Guard::pop() noexcept
{
    unsigned d = (digits_ & 0xF000'0000'0000'0000) >> 60;
    digits_ <<= 4;
    return d;
}

// Returns:
//     -1 if Guard is less than half
//      0 if Guard is exactly half
//      1 if Guard is greater than half
int
Number::Guard::round() noexcept
{
    auto mode = Number::getround();

    if (mode == towards_zero)
        return -1;

    if (mode == downward)
    {
        if (sbit_)
        {
            if (digits_ > 0 || xbit_)
                return 1;
        }
        return -1;
    }

    if (mode == upward)
    {
        if (sbit_)
            return -1;
        if (digits_ > 0 || xbit_)
            return 1;
        return -1;
    }

    // assume round to nearest if mode is not one of the predefined values
    if (digits_ > 0x5000'0000'0000'0000)
        return 1;
    if (digits_ < 0x5000'0000'0000'0000)
        return -1;
    if (xbit_)
        return 1;
    return 0;
}

// Number

constexpr Number one{1000000000000000, -15, Number::unchecked{}};

void
Number::normalize()
{
    if (mantissa_ == 0)
    {
        *this = Number{};
        return;
    }
    bool const negative = (mantissa_ < 0);
    auto m = static_cast<std::make_unsigned_t<rep>>(mantissa_);
    if (negative)
        m = -m;
    while ((m < minMantissa) && (exponent_ > minExponent))
    {
        m *= 10;
        --exponent_;
    }
    Guard g;
    if (negative)
        g.set_negative();
    while (m > maxMantissa)
    {
        if (exponent_ >= maxExponent)
            throw std::overflow_error("Number::normalize 1");
        g.push(m % 10);
        m /= 10;
        ++exponent_;
    }
    mantissa_ = m;
    if ((exponent_ < minExponent) || (mantissa_ < minMantissa))
    {
        *this = Number{};
        return;
    }

    auto r = g.round();
    if (r == 1 || (r == 0 && (mantissa_ & 1) == 1))
    {
        ++mantissa_;
        if (mantissa_ > maxMantissa)
        {
            mantissa_ /= 10;
            ++exponent_;
        }
    }
    if (exponent_ > maxExponent)
        throw std::overflow_error("Number::normalize 2");

    if (negative)
        mantissa_ = -mantissa_;
}

Number&
Number::operator+=(Number const& y)
{
    if (y == Number{})
        return *this;
    if (*this == Number{})
    {
        *this = y;
        return *this;
    }
    if (*this == -y)
    {
        *this = Number{};
        return *this;
    }
    assert(isnormal() && y.isnormal());
    auto xm = mantissa();
    auto xe = exponent();
    int xn = 1;
    if (xm < 0)
    {
        xm = -xm;
        xn = -1;
    }
    auto ym = y.mantissa();
    auto ye = y.exponent();
    int yn = 1;
    if (ym < 0)
    {
        ym = -ym;
        yn = -1;
    }
    Guard g;
    if (xe < ye)
    {
        if (xn == -1)
            g.set_negative();
        do
        {
            g.push(xm % 10);
            xm /= 10;
            ++xe;
        } while (xe < ye);
    }
    else if (xe > ye)
    {
        if (yn == -1)
            g.set_negative();
        do
        {
            g.push(ym % 10);
            ym /= 10;
            ++ye;
        } while (xe > ye);
    }
    if (xn == yn)
    {
        xm += ym;
        if (xm > maxMantissa)
        {
            g.push(xm % 10);
            xm /= 10;
            ++xe;
        }
        auto r = g.round();
        if (r == 1 || (r == 0 && (xm & 1) == 1))
        {
            ++xm;
            if (xm > maxMantissa)
            {
                xm /= 10;
                ++xe;
            }
        }
        if (xe > maxExponent)
            throw std::overflow_error("Number::addition overflow");
    }
    else
    {
        if (xm > ym)
        {
            xm = xm - ym;
        }
        else
        {
            xm = ym - xm;
            xe = ye;
            xn = yn;
        }
        while (xm < minMantissa)
        {
            xm *= 10;
            xm -= g.pop();
            --xe;
        }
        auto r = g.round();
        if (r == 1 || (r == 0 && (xm & 1) == 1))
        {
            --xm;
            if (xm < minMantissa)
            {
                xm *= 10;
                --xe;
            }
        }
        if (xe < minExponent)
        {
            xm = 0;
            xe = Number{}.exponent_;
        }
    }
    mantissa_ = xm * xn;
    exponent_ = xe;
    return *this;
}

Number&
Number::operator*=(Number const& y)
{
    if (*this == Number{})
        return *this;
    if (y == Number{})
    {
        *this = y;
        return *this;
    }
    assert(isnormal() && y.isnormal());
    auto xm = mantissa();
    auto xe = exponent();
    int xn = 1;
    if (xm < 0)
    {
        xm = -xm;
        xn = -1;
    }
    auto ym = y.mantissa();
    auto ye = y.exponent();
    int yn = 1;
    if (ym < 0)
    {
        ym = -ym;
        yn = -1;
    }
    auto zm = uint128_t(xm) * uint128_t(ym);
    auto ze = xe + ye;
    auto zn = xn * yn;
    Guard g;
    if (zn == -1)
        g.set_negative();
    while (zm > maxMantissa)
    {
        g.push(static_cast<unsigned>(zm % 10));
        zm /= 10;
        ++ze;
    }
    xm = static_cast<rep>(zm);
    xe = ze;
    auto r = g.round();
    if (r == 1 || (r == 0 && (xm & 1) == 1))
    {
        ++xm;
        if (xm > maxMantissa)
        {
            xm /= 10;
            ++xe;
        }
    }
    if (xe < minExponent)
    {
        xm = 0;
        xe = Number{}.exponent_;
    }
    if (xe > maxExponent)
        throw std::overflow_error(
            "Number::multiplication overflow : exponent is " +
            std::to_string(xe));
    mantissa_ = xm * zn;
    exponent_ = xe;
    assert(isnormal() || *this == Number{});
    return *this;
}

Number&
Number::operator/=(Number const& y)
{
    if (y == Number{})
        throw std::overflow_error("Number: divide by 0");
    if (*this == Number{})
        return *this;
    int np = 1;
    auto nm = mantissa();
    auto ne = exponent();
    if (nm < 0)
    {
        nm = -nm;
        np = -1;
    }
    int dp = 1;
    auto dm = y.mantissa();
    auto de = y.exponent();
    if (dm < 0)
    {
        dm = -dm;
        dp = -1;
    }
    // Shift by 10^17 gives greatest precision while not overflowing uint128_t
    // or the cast back to int64_t
    const uint128_t f = 100'000'000'000'000'000;
    mantissa_ = static_cast<std::int64_t>(uint128_t(nm) * f / uint128_t(dm));
    exponent_ = ne - de - 17;
    mantissa_ *= np * dp;
    normalize();
    return *this;
}

Number::operator rep() const
{
    rep drops = mantissa_;
    int offset = exponent_;
    Guard g;
    if (drops != 0)
    {
        if (drops < 0)
        {
            g.set_negative();
            drops = -drops;
        }
        for (; offset < 0; ++offset)
        {
            g.push(drops % 10);
            drops /= 10;
        }
        for (; offset > 0; --offset)
        {
            if (drops > std::numeric_limits<decltype(drops)>::max() / 10)
                throw std::overflow_error("Number::operator rep() overflow");
            drops *= 10;
        }
        auto r = g.round();
        if (r == 1 || (r == 0 && (drops & 1) == 1))
        {
            ++drops;
        }
        if (g.is_negative())
            drops = -drops;
    }
    return drops;
}

Number::operator XRPAmount() const
{
    return XRPAmount{static_cast<rep>(*this)};
}

std::string
to_string(Number const& amount)
{
    // keep full internal accuracy, but make more human friendly if possible
    if (amount == Number{})
        return "0";

    auto const exponent = amount.exponent();
    auto mantissa = amount.mantissa();

    // Use scientific notation for exponents that are too small or too large
    if (((exponent != 0) && ((exponent < -25) || (exponent > -5))))
    {
        std::string ret = std::to_string(mantissa);
        ret.append(1, 'e');
        ret.append(std::to_string(exponent));
        return ret;
    }

    bool negative = false;

    if (mantissa < 0)
    {
        mantissa = -mantissa;
        negative = true;
    }

    assert(exponent + 43 > 0);

    ptrdiff_t const pad_prefix = 27;
    ptrdiff_t const pad_suffix = 23;

    std::string const raw_value(std::to_string(mantissa));
    std::string val;

    val.reserve(raw_value.length() + pad_prefix + pad_suffix);
    val.append(pad_prefix, '0');
    val.append(raw_value);
    val.append(pad_suffix, '0');

    ptrdiff_t const offset(exponent + 43);

    auto pre_from(val.begin());
    auto const pre_to(val.begin() + offset);

    auto const post_from(val.begin() + offset);
    auto post_to(val.end());

    // Crop leading zeroes. Take advantage of the fact that there's always a
    // fixed amount of leading zeroes and skip them.
    if (std::distance(pre_from, pre_to) > pad_prefix)
        pre_from += pad_prefix;

    assert(post_to >= post_from);

    pre_from = std::find_if(pre_from, pre_to, [](char c) { return c != '0'; });

    // Crop trailing zeroes. Take advantage of the fact that there's always a
    // fixed amount of trailing zeroes and skip them.
    if (std::distance(post_from, post_to) > pad_suffix)
        post_to -= pad_suffix;

    assert(post_to >= post_from);

    post_to = std::find_if(
                  std::make_reverse_iterator(post_to),
                  std::make_reverse_iterator(post_from),
                  [](char c) { return c != '0'; })
                  .base();

    std::string ret;

    if (negative)
        ret.append(1, '-');

    // Assemble the output:
    if (pre_from == pre_to)
        ret.append(1, '0');
    else
        ret.append(pre_from, pre_to);

    if (post_to != post_from)
    {
        ret.append(1, '.');
        ret.append(post_from, post_to);
    }

    return ret;
}

// Returns f^n
// Uses a log_2(n) number of multiplications

Number
power(Number const& f, unsigned n)
{
    if (n == 0)
        return one;
    if (n == 1)
        return f;
    auto r = power(f, n / 2);
    r *= r;
    if (n % 2 != 0)
        r *= f;
    return r;
}

// Returns f^(1/d)
// Uses Newton–Raphson iterations until the result stops changing
// to find the non-negative root of the polynomial g(x) = x^d - f

// This function, and power(Number f, unsigned n, unsigned d)
// treat corner cases such as 0 roots as advised by Annex F of
// the C standard, which itself is consistent with the IEEE
// floating point standards.

Number
root(Number f, unsigned d)
{
    if (f == one || d == 1)
        return f;
    if (d == 0)
    {
        if (f == -one)
            return one;
        if (abs(f) < one)
            return Number{};
        throw std::overflow_error("Number::root infinity");
    }
    if (f < Number{} && d % 2 == 0)
        throw std::overflow_error("Number::root nan");
    if (f == Number{})
        return f;

    // Scale f into the range (0, 1) such that f's exponent is a multiple of d
    auto e = f.exponent() + 16;
    auto const di = static_cast<int>(d);
    auto ex = [e = e, di = di]()  // Euclidean remainder of e/d
    {
        int k = (e >= 0 ? e : e - (di - 1)) / di;
        int k2 = e - k * di;
        if (k2 == 0)
            return 0;
        return di - k2;
    }();
    e += ex;
    f = Number{f.mantissa(), f.exponent() - e};  // f /= 10^e;
    bool neg = false;
    if (f < Number{})
    {
        neg = true;
        f = -f;
    }

    // Quadratic least squares curve fit of f^(1/d) in the range [0, 1]
    auto const D = ((6 * di + 11) * di + 6) * di + 1;
    auto const a0 = 3 * di * ((2 * di - 3) * di + 1);
    auto const a1 = 24 * di * (2 * di - 1);
    auto const a2 = -30 * (di - 1) * di;
    Number r = ((Number{a2} * f + Number{a1}) * f + Number{a0}) / Number{D};
    if (neg)
    {
        f = -f;
        r = -r;
    }

    //  Newton–Raphson iteration of f^(1/d) with initial guess r
    //  halt when r stops changing, checking for bouncing on the last iteration
    Number rm1{};
    Number rm2{};
    do
    {
        rm2 = rm1;
        rm1 = r;
        r = (Number(d - 1) * r + f / power(r, d - 1)) / Number(d);
    } while (r != rm1 && r != rm2);

    //  return r * 10^(e/d) to reverse scaling
    return Number{r.mantissa(), r.exponent() + e / di};
}

Number
root2(Number f)
{
    if (f == one)
        return f;
    if (f < Number{})
        throw std::overflow_error("Number::root nan");
    if (f == Number{})
        return f;

    // Scale f into the range (0, 1) such that f's exponent is a multiple of d
    auto e = f.exponent() + 16;
    if (e % 2 != 0)
        ++e;
    f = Number{f.mantissa(), f.exponent() - e};  // f /= 10^e;

    // Quadratic least squares curve fit of f^(1/d) in the range [0, 1]
    auto const D = 105;
    auto const a0 = 18;
    auto const a1 = 144;
    auto const a2 = -60;
    Number r = ((Number{a2} * f + Number{a1}) * f + Number{a0}) / Number{D};

    //  Newton–Raphson iteration of f^(1/2) with initial guess r
    //  halt when r stops changing, checking for bouncing on the last iteration
    Number rm1{};
    Number rm2{};
    do
    {
        rm2 = rm1;
        rm1 = r;
        r = (r + f / r) / Number(2);
    } while (r != rm1 && r != rm2);

    //  return r * 10^(e/2) to reverse scaling
    return Number{r.mantissa(), r.exponent() + e / 2};
}

// Returns f^(n/d)

Number
power(Number const& f, unsigned n, unsigned d)
{
    if (f == one)
        return f;
    auto g = std::gcd(n, d);
    if (g == 0)
        throw std::overflow_error("Number::power nan");
    if (d == 0)
    {
        if (f == -one)
            return one;
        if (abs(f) < one)
            return Number{};
        // abs(f) > one
        throw std::overflow_error("Number::power infinity");
    }
    if (n == 0)
        return one;
    n /= g;
    d /= g;
    if ((n % 2) == 1 && (d % 2) == 0 && f < Number{})
        throw std::overflow_error("Number::power nan");
    return root(power(f, n), d);
}

}  // namespace ripple
