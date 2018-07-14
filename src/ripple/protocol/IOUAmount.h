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

#ifndef RIPPLE_PROTOCOL_IOUAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_IOUAMOUNT_H_INCLUDED

#include <ripple/beast/utility/Zero.h>
#include <boost/operators.hpp>
#include <cstdint>
#include <string>
#include <utility>

namespace ripple {

/** Floating point representation of amounts with high dynamic range

    Amounts are stored as a normalized signed mantissa and an exponent. The
    range of the normalized exponent is [-96,80] and the range of the absolute
    value of the normalized mantissa is [1000000000000000, 9999999999999999].

    Arithmetic operations can throw std::overflow_error during normalization
    if the amount exceeds the largest representable amount, but underflows
    will silently trunctate to zero.
*/
class IOUAmount
    : private boost::totally_ordered<IOUAmount>
    , private boost::additive <IOUAmount>
{
private:
    std::int64_t mantissa_;
    int exponent_;

    /** Adjusts the mantissa and exponent to the proper range.

        This can throw if the amount cannot be normalized, or is larger than
        the largest value that can be represented as an IOU amount. Amounts
        that are too small to be represented normalize to 0.
    */
    void
    normalize ();

public:
    IOUAmount () = default;
    IOUAmount (IOUAmount const& other) = default;
    IOUAmount&operator= (IOUAmount const& other) = default;

    IOUAmount (beast::Zero)
    {
        *this = beast::zero;
    }

    IOUAmount (std::int64_t mantissa, int exponent)
        : mantissa_ (mantissa)
        , exponent_ (exponent)
    {
        normalize ();
    }

    IOUAmount&
    operator= (beast::Zero)
    {
        // The -100 is used to allow 0 to sort less than small positive values
        // which will have a large negative exponent.
        mantissa_ = 0;
        exponent_ = -100;
        return *this;
    }

    IOUAmount&
    operator+= (IOUAmount const& other);

    IOUAmount&
    operator-= (IOUAmount const& other)
    {
        *this += -other;
        return *this;
    }

    IOUAmount
    operator- () const
    {
        return { -mantissa_, exponent_ };
    }

    bool
    operator==(IOUAmount const& other) const
    {
        return exponent_ == other.exponent_ &&
            mantissa_ == other.mantissa_;
    }

    bool
    operator<(IOUAmount const& other) const;

    /** Returns true if the amount is not zero */
    explicit
    operator bool() const noexcept
    {
        return mantissa_ != 0;
    }

    /** Return the sign of the amount */
    int
    signum() const noexcept
    {
        return (mantissa_ < 0) ? -1 : (mantissa_ ? 1 : 0);
    }

    int
    exponent() const noexcept
    {
        return exponent_;
    }

    std::int64_t
    mantissa() const noexcept
    {
        return mantissa_;
    }
};

std::string
to_string (IOUAmount const& amount);

/* Return num*amt/den
   This function keeps more precision than computing
   num*amt, storing the result in an IOUAmount, then
   dividing by den.
*/
IOUAmount
mulRatio (
    IOUAmount const& amt,
    std::uint32_t num,
    std::uint32_t den,
    bool roundUp);

}

#endif
