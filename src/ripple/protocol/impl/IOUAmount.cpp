//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/protocol/IOUAmount.h>
#include <stdexcept>

namespace ripple {

void
IOUAmount::normalize ()
{
    /* The range for the exponent when normalized */
    static int const minExponent = -96;
    static int const maxExponent = 80;

    /* The range for the mantissa when normalized */
    static std::int64_t const minMantissa = 1000000000000000ull;
    static std::int64_t const maxMantissa = 9999999999999999ull;

    if (mantissa_ == 0)
    {
        *this = zero;
        return;
    }

    bool const negative = (mantissa_ < 0);

    if (negative)
        mantissa_ = -mantissa_;

    while ((mantissa_ < minMantissa) && (exponent_ > minExponent))
    {
        mantissa_ *= 10;
        --exponent_;
    }

    while (mantissa_ > maxMantissa)
    {
        if (exponent_ >= maxExponent)
            throw std::overflow_error ("IOUAmount::normalize");

        mantissa_ /= 10;
        ++exponent_;
    }

    if ((exponent_ < minExponent) || (mantissa_ < minMantissa))
    {
        *this = zero;
        return;
    }

    if (exponent_ > maxExponent)
        throw std::overflow_error ("value overflow");

    if (negative)
        mantissa_ = -mantissa_;
}

IOUAmount&
IOUAmount::operator+= (IOUAmount const& other)
{
    if (other == zero)
        return *this;

    if (*this == zero)
    {
        *this = other;
        return *this;
    }

    auto m = other.mantissa_;
    auto e = other.exponent_;

    while (exponent_ < e)
    {
        mantissa_ /= 10;
        ++exponent_;
    }

    while (e < exponent_)
    {
        m /= 10;
        ++e;
    }

    // This addition cannot overflow an std::int64_t but we may throw from
    // normalize if the result isn't representable.
    mantissa_ += m;

    if (mantissa_ >= -10 && mantissa_ <= 10)
    {
        *this = zero;
        return *this;
    }

    normalize ();

    return *this;
}

bool
IOUAmount::operator<(IOUAmount const& other) const
{
    // If the two amounts have different signs (zero is treated as positive)
    // then the comparison is true iff the left is negative.
    bool const lneg = mantissa_ < 0;
    bool const rneg = other.mantissa_ < 0;

    if (lneg != rneg)
        return lneg;

    // Both have same sign and the left is zero: the right must be
    // greater than 0.
    if (mantissa_ == 0)
        return other.mantissa_ > 0;

    // Both have same sign, the right is zero and the left is non-zero.
    if (other.mantissa_ == 0)
        return false;

    // Both have the same sign, compare by exponents:
    if (exponent_ > other.exponent_)
        return lneg;
    if (exponent_ < other.exponent_)
        return !lneg;

    // If equal exponents, compare mantissas
    return mantissa_ < other.mantissa_;
}

}
