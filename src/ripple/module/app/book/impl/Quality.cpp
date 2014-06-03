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

#include <ripple/module/app/book/Quality.h>

#include <cassert>
#include <limits>

namespace ripple {
namespace core {

Quality::Quality (std::uint64_t value)
    : m_value (value)
{
}

Quality::Quality (Amounts const& amount)
    : m_value (Amount::getRate (amount.out, amount.in))
{
}

Quality&
Quality::operator++()
{
    assert (m_value > 0);
    --m_value;
    return *this;
}

Quality
Quality::operator++ (int)
{
    Quality prev (*this);
    --*this;
    return prev;
}

Quality&
Quality::operator--()
{
    assert (m_value < std::numeric_limits<value_type>::max());
    ++m_value;
    return *this;
}

Quality
Quality::operator-- (int)
{
    Quality prev (*this);
    ++*this;
    return prev;
}

Amounts
Quality::ceil_in (Amounts const& amount, Amount const& limit) const
{
    if (amount.in > limit)
    {
        Amounts result (limit, Amount::divRound (
            limit, rate(), amount.out, true));
        // Clamp out
        if (result.out > amount.out)
            result.out = amount.out;
        return result;
    }
    return amount;
}

Amounts
Quality::ceil_out (Amounts const& amount, Amount const& limit) const
{
    if (amount.out > limit)
    {
        Amounts result (Amount::mulRound (
            limit, rate(), amount.in, true), limit);
        // Clamp in
        if (result.in > amount.in)
            result.in = amount.in;
        return result;
    }
    return amount;
}

Quality
composed_quality (Quality const& lhs, Quality const& rhs)
{
    Amount const lhs_rate (lhs.rate ());
    assert (lhs_rate != zero);

    Amount const rhs_rate (rhs.rate ());
    assert (rhs_rate != zero);

    Amount const rate (Amount::mulRound (lhs_rate, rhs_rate, true));

    std::uint64_t const stored_exponent (rate.getExponent () + 100);
    std::uint64_t const stored_mantissa (rate.getMantissa ());

    assert ((stored_exponent >= 0) && (stored_exponent <= 255));

    return Quality ((stored_exponent << (64 - 8)) | stored_mantissa);
}

}
}
