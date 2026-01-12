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

#include <xrpl/basics/Number.h>
// Keep Number.h first to ensure it can build without hidden dependencies
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#ifdef _MSC_VER
#pragma message("Using boost::multiprecision::uint128_t and int128_t")
#include <boost/multiprecision/cpp_int.hpp>
using uint128_t = boost::multiprecision::uint128_t;
using int128_t = boost::multiprecision::int128_t;
#else   // !defined(_MSC_VER)
using uint128_t = __uint128_t;
using int128_t = __int128_t;
#endif  // !defined(_MSC_VER)

namespace ripple {

thread_local Number::rounding_mode Number::mode_ = Number::to_nearest;
thread_local std::reference_wrapper<MantissaRange const> Number::range_ =
    largeRange;

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

MantissaRange::mantissa_scale
Number::getMantissaScale()
{
    return range_.get().scale;
}

void
Number::setMantissaScale(MantissaRange::mantissa_scale scale)
{
    if (scale != MantissaRange::small && scale != MantissaRange::large)
        LogicError("Unknown mantissa scale");
    range_ = scale == MantissaRange::small ? smallRange : largeRange;
}

// Guard

// The Guard class is used to tempoarily add extra digits of
// preicision to an operation.  This enables the final result
// to be correctly rounded to the internal precision of Number.

template <class T>
concept UnsignedMantissa =
    std::is_unsigned_v<T> || std::is_same_v<T, uint128_t>;

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
    template <class T>
    void
    push(T d) noexcept;

    // recover a digit
    unsigned
    pop() noexcept;

    // Indicate round direction:  1 is up, -1 is down, 0 is even
    // This enables the client to round towards nearest, and on
    // tie, round towards even.
    int
    round() noexcept;

    // Modify the result to the correctly rounded value
    template <UnsignedMantissa T>
    void
    doRoundUp(
        bool& negative,
        T& mantissa,
        int& exponent,
        internalrep const& minMantissa,
        internalrep const& maxMantissa,
        std::string location);

    // Modify the result to the correctly rounded value
    template <UnsignedMantissa T>
    void
    doRoundDown(
        bool& negative,
        T& mantissa,
        int& exponent,
        internalrep const& minMantissa);

    // Modify the result to the correctly rounded value
    void
    doRound(rep& drops, std::string location);

private:
    void
    doPush(unsigned d) noexcept;

    template <UnsignedMantissa T>
    void
    bringIntoRange(
        bool& negative,
        T& mantissa,
        int& exponent,
        internalrep const& minMantissa);
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
Number::Guard::doPush(unsigned d) noexcept
{
    xbit_ = xbit_ || ((digits_ & 0x0000'0000'0000'000F) != 0);
    digits_ >>= 4;
    digits_ |= (d & 0x0000'0000'0000'000FULL) << 60;
}

template <class T>
inline void
Number::Guard::push(T d) noexcept
{
    doPush(static_cast<unsigned>(d));
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

template <UnsignedMantissa T>
void
Number::Guard::bringIntoRange(
    bool& negative,
    T& mantissa,
    int& exponent,
    internalrep const& minMantissa)
{
    // Bring mantissa back into the minMantissa / maxMantissa range AFTER
    // rounding
    if (mantissa < minMantissa)
    {
        mantissa *= 10;
        --exponent;
    }
    if (exponent < minExponent)
    {
        constexpr Number zero = Number{};

        negative = zero.negative_;
        mantissa = zero.mantissa_;
        exponent = zero.exponent_;
    }
}

template <UnsignedMantissa T>
void
Number::Guard::doRoundUp(
    bool& negative,
    T& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa,
    std::string location)
{
    auto r = round();
    if (r == 1 || (r == 0 && (mantissa & 1) == 1))
    {
        ++mantissa;
        // Ensure mantissa after incrementing fits within both the
        // min/maxMantissa range and is a valid "rep".
        if (mantissa > maxMantissa || mantissa > maxRep)
        {
            mantissa /= 10;
            ++exponent;
        }
    }
    bringIntoRange(negative, mantissa, exponent, minMantissa);
    if (exponent > maxExponent)
        throw std::overflow_error(location);
}

template <UnsignedMantissa T>
void
Number::Guard::doRoundDown(
    bool& negative,
    T& mantissa,
    int& exponent,
    internalrep const& minMantissa)
{
    auto r = round();
    if (r == 1 || (r == 0 && (mantissa & 1) == 1))
    {
        --mantissa;
        if (mantissa < minMantissa)
        {
            mantissa *= 10;
            --exponent;
        }
    }
    bringIntoRange(negative, mantissa, exponent, minMantissa);
}

// Modify the result to the correctly rounded value
void
Number::Guard::doRound(rep& drops, std::string location)
{
    auto r = round();
    if (r == 1 || (r == 0 && (drops & 1) == 1))
    {
        if (drops >= maxRep)
        {
            static_assert(sizeof(internalrep) == sizeof(rep));
            // This should be impossible, because it's impossible to represent
            // "maxRep + 0.6" in Number, regardless of the scale. There aren't
            // enough digits available. You'd either get a mantissa of "maxRep"
            // or "(maxRep + 1) / 10", neither of which will round up when
            // converting to rep, though the latter might overflow _before_
            // rounding.
            throw std::overflow_error(location);  // LCOV_EXCL_LINE
        }
        ++drops;
    }
    if (is_negative())
        drops = -drops;
}

// Number

// Safely convert rep (int64) mantissa to internalrep (uint64). If the rep is
// negative, returns the positive value. This takes a little extra work because
// converting std::numeric_limits<std::int64_t>::min() flirts with UB, and can
// vary across compilers.
Number::internalrep
Number::externalToInternal(rep mantissa)
{
    // If the mantissa is already positive, just return it
    if (mantissa >= 0)
        return mantissa;
    // If the mantissa is negative, but fits within the positive range of rep,
    // return it negated
    if (mantissa >= -std::numeric_limits<rep>::max())
        return -mantissa;

    // If the mantissa doesn't fit within the positive range, convert to
    // int128_t, negate that, and cast it back down to the internalrep
    // In practice, this is only going to cover the case of
    // std::numeric_limits<rep>::min().
    int128_t temp = mantissa;
    return static_cast<internalrep>(-temp);
}

constexpr Number
Number::oneSmall()
{
    return Number{
        false,
        Number::smallRange.min,
        -Number::smallRange.log,
        Number::unchecked{}};
};

constexpr Number oneSml = Number::oneSmall();

constexpr Number
Number::oneLarge()
{
    return Number{
        false,
        Number::largeRange.min,
        -Number::largeRange.log,
        Number::unchecked{}};
};

constexpr Number oneLrg = Number::oneLarge();

Number
Number::one()
{
    if (&range_.get() == &smallRange)
        return oneSml;
    XRPL_ASSERT(&range_.get() == &largeRange, "Number::one() : valid range_");
    return oneLrg;
}

// Use the member names in this static function for now so the diff is cleaner
// TODO: Rename the function parameters to get rid of the "_" suffix
template <class T>
void
doNormalize(
    bool& negative,
    T& mantissa_,
    int& exponent_,
    MantissaRange::rep const& minMantissa,
    MantissaRange::rep const& maxMantissa)
{
    auto constexpr minExponent = Number::minExponent;
    auto constexpr maxExponent = Number::maxExponent;
    auto constexpr maxRep = Number::maxRep;

    using Guard = Number::Guard;

    constexpr Number zero = Number{};
    if (mantissa_ == 0)
    {
        mantissa_ = zero.mantissa_;
        exponent_ = zero.exponent_;
        negative = zero.negative_;
        return;
    }
    auto m = mantissa_;
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
    if ((exponent_ < minExponent) || (m < minMantissa))
    {
        mantissa_ = zero.mantissa_;
        exponent_ = zero.exponent_;
        negative = zero.negative_;
        return;
    }

    // When using the largeRange, "m" needs fit within an int64, even if
    // the final mantissa_ is going to end up larger to fit within the
    // MantissaRange. Cut it down here so that the rounding will be done while
    // it's smaller.
    //
    // Example: 9,900,000,000,000,123,456 > 9,223,372,036,854,775,807,
    //      so "m" will be modified to 990,000,000,000,012,345. Then that value
    //      will be rounded to 990,000,000,000,012,345 or
    //      990,000,000,000,012,346, depending on the rounding mode. Finally,
    //      mantissa_ will be "m*10" so it fits within the range, and end up as
    //      9,900,000,000,000,123,450 or 9,900,000,000,000,123,460.
    // mantissa() will return mantissa_ / 10, and exponent() will return
    // exponent_ + 1.
    if (m > maxRep)
    {
        if (exponent_ >= maxExponent)
            throw std::overflow_error("Number::normalize 1.5");
        g.push(m % 10);
        m /= 10;
        ++exponent_;
    }
    // Before modification, m should be within the min/max range. After
    // modification, it must be less than maxRep. In other words, the original
    // value should have been no more than maxRep * 10.
    // (maxRep * 10 > maxMantissa)
    XRPL_ASSERT_PARTS(
        m <= maxRep,
        "xrpl::doNormalize",
        "intermediate mantissa fits in int64");
    mantissa_ = m;

    g.doRoundUp(
        negative,
        mantissa_,
        exponent_,
        minMantissa,
        maxMantissa,
        "Number::normalize 2");
    XRPL_ASSERT_PARTS(
        mantissa_ >= minMantissa && mantissa_ <= maxMantissa,
        "xrpl::doNormalize",
        "final mantissa fits in range");
}

template <>
void
Number::normalize<uint128_t>(
    bool& negative,
    uint128_t& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa)
{
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa);
}

template <>
void
Number::normalize<unsigned long long>(
    bool& negative,
    unsigned long long& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa)
{
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa);
}

template <>
void
Number::normalize<unsigned long>(
    bool& negative,
    unsigned long& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa)
{
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa);
}

void
Number::normalize()
{
    auto const& range = range_.get();
    normalize(negative_, mantissa_, exponent_, range.min, range.max);
}

// Copy the number, but set a new exponent. Because the mantissa doesn't change,
// the result will be "mostly" normalized, but the exponent could go out of
// range.
Number
Number::shiftExponent(int exponentDelta) const
{
    XRPL_ASSERT_PARTS(isnormal(), "xrpl::Number::shiftExponent", "normalized");
    auto const newExponent = exponent_ + exponentDelta;
    if (newExponent >= maxExponent)
        throw std::overflow_error("Number::shiftExponent");
    if (newExponent < minExponent)
    {
        return Number{};
    }
    Number const result{negative_, mantissa_, newExponent, unchecked{}};
    XRPL_ASSERT_PARTS(
        result.isnormal(),
        "xrpl::Number::shiftExponent",
        "result is normalized");
    return result;
}

Number&
Number::operator+=(Number const& y)
{
    constexpr Number zero = Number{};
    if (y == zero)
        return *this;
    if (*this == zero)
    {
        *this = y;
        return *this;
    }
    if (*this == -y)
    {
        *this = zero;
        return *this;
    }

    XRPL_ASSERT(
        isnormal() && y.isnormal(),
        "xrpl::Number::operator+=(Number) : is normal");
    // *n = negative
    // *m = mantissa
    // *e = exponent

    // Need to use uint128_t, because large mantissas can overflow when added
    // together.
    bool xn = negative_;
    uint128_t xm = mantissa_;
    auto xe = exponent_;

    bool yn = y.negative_;
    uint128_t ym = y.mantissa_;
    auto ye = y.exponent_;
    Guard g;
    if (xe < ye)
    {
        if (xn)
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
        if (yn)
            g.set_negative();
        do
        {
            g.push(ym % 10);
            ym /= 10;
            ++ye;
        } while (xe > ye);
    }

    auto const& range = range_.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;

    if (xn == yn)
    {
        xm += ym;
        if (xm > maxMantissa || xm > maxRep)
        {
            g.push(xm % 10);
            xm /= 10;
            ++xe;
        }
        g.doRoundUp(
            xn, xm, xe, minMantissa, maxMantissa, "Number::addition overflow");
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
        while (xm < minMantissa && xm * 10 <= maxRep)
        {
            xm *= 10;
            xm -= g.pop();
            --xe;
        }
        g.doRoundDown(xn, xm, xe, minMantissa);
    }

    negative_ = xn;
    mantissa_ = static_cast<internalrep>(xm);
    exponent_ = xe;
    normalize();
    return *this;
}

// Optimization equivalent to:
// auto r = static_cast<unsigned>(u % 10);
// u /= 10;
// return r;
// Derived from Hacker's Delight Second Edition Chapter 10
// by Henry S. Warren, Jr.
static inline unsigned
divu10(uint128_t& u)
{
    // q = u * 0.75
    auto q = (u >> 1) + (u >> 2);
    // iterate towards q = u * 0.8
    q += q >> 4;
    q += q >> 8;
    q += q >> 16;
    q += q >> 32;
    q += q >> 64;
    // q /= 8 approximately == u / 10
    q >>= 3;
    // r = u - q * 10  approximately == u % 10
    auto r = static_cast<unsigned>(u - ((q << 3) + (q << 1)));
    // correction c is 1 if r >= 10 else 0
    auto c = (r + 6) >> 4;
    u = q + c;
    r -= c * 10;
    return r;
}

Number&
Number::operator*=(Number const& y)
{
    constexpr Number zero = Number{};
    if (*this == zero)
        return *this;
    if (y == zero)
    {
        *this = y;
        return *this;
    }
    // *n = negative
    // *s = sign
    // *m = mantissa
    // *e = exponent

    bool xn = negative_;
    int xs = xn ? -1 : 1;
    internalrep xm = mantissa_;
    auto xe = exponent_;

    bool yn = y.negative_;
    int ys = yn ? -1 : 1;
    internalrep ym = y.mantissa_;
    auto ye = y.exponent_;

    auto zm = uint128_t(xm) * uint128_t(ym);
    auto ze = xe + ye;
    auto zs = xs * ys;
    bool zn = (zs == -1);
    Guard g;
    if (zn)
        g.set_negative();

    auto const& range = range_.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;

    while (zm > maxMantissa || zm > maxRep)
    {
        // The following is optimization for:
        // g.push(static_cast<unsigned>(zm % 10));
        // zm /= 10;
        g.push(divu10(zm));
        ++ze;
    }
    xm = static_cast<internalrep>(zm);
    xe = ze;
    g.doRoundUp(
        zn,
        xm,
        xe,
        minMantissa,
        maxMantissa,
        "Number::multiplication overflow : exponent is " + std::to_string(xe));
    negative_ = zn;
    mantissa_ = xm;
    exponent_ = xe;

    normalize();
    return *this;
}

Number&
Number::operator/=(Number const& y)
{
    constexpr Number zero = Number{};
    if (y == zero)
        throw std::overflow_error("Number: divide by 0");
    if (*this == zero)
        return *this;
    // n* = numerator
    // d* = denominator
    // *p = negative (positive?)
    // *s = sign
    // *m = mantissa
    // *e = exponent

    bool np = negative_;
    int ns = (np ? -1 : 1);
    auto nm = mantissa_;
    auto ne = exponent_;

    bool dp = y.negative_;
    int ds = (dp ? -1 : 1);
    auto dm = y.mantissa_;
    auto de = y.exponent_;

    auto const& range = range_.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;

    // Shift by 10^17 gives greatest precision while not overflowing
    // uint128_t or the cast back to int64_t
    // TODO: Can/should this be made bigger for largeRange?
    // log(2^128,10) ~ 38.5
    // largeRange.log = 18, fits in 10^19
    // f can be up to 10^(38-19) = 10^19 safely
    static_assert(smallRange.log == 15);
    static_assert(largeRange.log == 18);
    bool small = Number::getMantissaScale() == MantissaRange::small;
    uint128_t const f =
        small ? 100'000'000'000'000'000 : 10'000'000'000'000'000'000ULL;
    XRPL_ASSERT_PARTS(
        f >= minMantissa * 10, "Number::operator/=", "factor expected size");

    // unsigned denominator
    auto const dmu = static_cast<uint128_t>(dm);
    // correctionFactor can be anything between 10 and f, depending on how much
    // extra precision we want to only use for rounding with the
    // largeRange. Three digits seems like plenty, and is more than
    // the smallRange uses.
    uint128_t const correctionFactor = 1'000;

    auto const numerator = uint128_t(nm) * f;

    auto zm = numerator / dmu;
    auto ze = ne - de - (small ? 17 : 19);
    bool zn = (ns * ds) < 0;
    if (!small)
    {
        // Virtually multiply numerator by correctionFactor. Since that would
        // overflow in the existing uint128_t, we'll do that part separately.
        // The math for this would work for small mantissas, but we need to
        // preserve existing behavior.
        //
        // Consider:
        // ((numerator * correctionFactor) / dmu) / correctionFactor
        // = ((numerator / dmu) * correctionFactor) / correctionFactor)
        //
        // But that assumes infinite precision. With integer math, this is
        // equivalent to
        //
        // = ((numerator / dmu * correctionFactor)
        //   + ((numerator % dmu) * correctionFactor) / dmu) / correctionFactor
        //
        // We have already set `mantissa_ = numerator / dmu`. Now we
        // compute `remainder = numerator % dmu`, and if it is
        // nonzero, we do the rest of the arithmetic. If it's zero, we can skip
        // it.
        auto const remainder = (numerator % dmu);
        if (remainder != 0)
        {
            zm *= correctionFactor;
            auto const correction = remainder * correctionFactor / dmu;
            zm += correction;
            // divide by 1000 by moving the exponent, so we don't lose the
            // integer value we just computed
            ze -= 3;
        }
    }
    normalize(zn, zm, ze, minMantissa, maxMantissa);
    negative_ = zn;
    mantissa_ = static_cast<internalrep>(zm);
    exponent_ = ze;
    XRPL_ASSERT_PARTS(
        isnormal(), "xrpl::Number::operator/=", "result is normalized");

    return *this;
}

Number::operator rep() const
{
    rep drops = mantissa();
    int offset = exponent();
    Guard g;
    if (drops != 0)
    {
        if (negative_)
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
            if (drops > maxRep / 10)
                throw std::overflow_error("Number::operator rep() overflow");
            drops *= 10;
        }
        g.doRound(drops, "Number::operator rep() rounding overflow");
    }
    return drops;
}

Number
Number::truncate() const noexcept
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

std::string
to_string(Number const& amount)
{
    // keep full internal accuracy, but make more human friendly if possible
    constexpr Number zero = Number{};
    if (amount == zero)
        return "0";

    auto exponent = amount.exponent_;
    auto mantissa = amount.mantissa_;
    bool const negative = amount.negative_;

    // Use scientific notation for exponents that are too small or too large
    auto const rangeLog = Number::mantissaLog();
    if (((exponent != 0) &&
         ((exponent < -(rangeLog + 10)) || (exponent > -(rangeLog - 10)))))
    {
        while (mantissa != 0 && mantissa % 10 == 0 &&
               exponent < Number::maxExponent)
        {
            mantissa /= 10;
            ++exponent;
        }
        std::string ret = negative ? "-" : "";
        ret.append(std::to_string(mantissa));
        ret.append(1, 'e');
        ret.append(std::to_string(exponent));
        return ret;
    }

    XRPL_ASSERT(
        exponent + 43 > 0, "ripple::to_string(Number) : minimum exponent");

    ptrdiff_t const pad_prefix = rangeLog + 12;
    ptrdiff_t const pad_suffix = rangeLog + 8;

    std::string const raw_value(std::to_string(mantissa));
    std::string val;

    val.reserve(raw_value.length() + pad_prefix + pad_suffix);
    val.append(pad_prefix, '0');
    val.append(raw_value);
    val.append(pad_suffix, '0');

    ptrdiff_t const offset(exponent + pad_prefix + rangeLog + 1);

    auto pre_from(val.begin());
    auto const pre_to(val.begin() + offset);

    auto const post_from(val.begin() + offset);
    auto post_to(val.end());

    // Crop leading zeroes. Take advantage of the fact that there's always a
    // fixed amount of leading zeroes and skip them.
    if (std::distance(pre_from, pre_to) > pad_prefix)
        pre_from += pad_prefix;

    XRPL_ASSERT(
        post_to >= post_from,
        "ripple::to_string(Number) : first distance check");

    pre_from = std::find_if(pre_from, pre_to, [](char c) { return c != '0'; });

    // Crop trailing zeroes. Take advantage of the fact that there's always a
    // fixed amount of trailing zeroes and skip them.
    if (std::distance(post_from, post_to) > pad_suffix)
        post_to -= pad_suffix;

    XRPL_ASSERT(
        post_to >= post_from,
        "ripple::to_string(Number) : second distance check");

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
        return Number::one();
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
    constexpr Number zero = Number{};
    auto const one = Number::one();

    if (f == one || d == 1)
        return f;
    if (d == 0)
    {
        if (f == -one)
            return one;
        if (abs(f) < one)
            return zero;
        throw std::overflow_error("Number::root infinity");
    }
    if (f < zero && d % 2 == 0)
        throw std::overflow_error("Number::root nan");
    if (f == zero)
        return f;

    // Scale f into the range (0, 1) such that f's exponent is a multiple of d
    auto e = f.exponent_ + Number::mantissaLog() + 1;
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
    f = f.shiftExponent(-e);  // f /= 10^e;

    XRPL_ASSERT_PARTS(
        f.isnormal(), "xrpl::root(Number, unsigned)", "f is normalized");
    bool neg = false;
    if (f < zero)
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
    auto const result = r.shiftExponent(e / di);
    XRPL_ASSERT_PARTS(
        result.isnormal(),
        "xrpl::root(Number, unsigned)",
        "result is normalized");
    return result;
}

Number
root2(Number f)
{
    constexpr Number zero = Number{};
    auto const one = Number::one();

    if (f == one)
        return f;
    if (f < zero)
        throw std::overflow_error("Number::root nan");
    if (f == zero)
        return f;

    // Scale f into the range (0, 1) such that f's exponent is a multiple of d
    auto e = f.exponent_ + Number::mantissaLog() + 1;
    if (e % 2 != 0)
        ++e;
    f = f.shiftExponent(-e);  // f /= 10^e;
    XRPL_ASSERT_PARTS(f.isnormal(), "xrpl::root2(Number)", "f is normalized");

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
    auto const result = r.shiftExponent(e / 2);
    XRPL_ASSERT_PARTS(
        result.isnormal(), "xrpl::root2(Number)", "result is normalized");

    return result;
}

// Returns f^(n/d)

Number
power(Number const& f, unsigned n, unsigned d)
{
    constexpr Number zero = Number{};
    auto const one = Number::one();

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
            return zero;
        // abs(f) > one
        throw std::overflow_error("Number::power infinity");
    }
    if (n == 0)
        return one;
    n /= g;
    d /= g;
    if ((n % 2) == 1 && (d % 2) == 0 && f < zero)
        throw std::overflow_error("Number::power nan");
    return root(power(f, n), d);
}

}  // namespace ripple
