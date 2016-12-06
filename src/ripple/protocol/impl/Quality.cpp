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
#include <ripple/protocol/Quality.h>
#include <cassert>
#include <limits>

namespace ripple {

Quality::Quality (std::uint64_t value)
    : m_value (value)
{
}

Quality::Quality (Amounts const& amount)
    : m_value (getRate (amount.out, amount.in))
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
    ++*this;
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
    --*this;
    return prev;
}

Amounts
Quality::ceil_in (Amounts const& amount, STAmount const& limit) const
{
    if (amount.in > limit)
    {
        Amounts result (limit, divRound (
            limit, rate(), amount.out.issue (), true));
        // Clamp out
        if (result.out > amount.out)
            result.out = amount.out;
        assert (result.in == limit);
        return result;
    }
    assert (amount.in <= limit);
    return amount;
}

Amounts
Quality::ceil_out (Amounts const& amount, STAmount const& limit) const
{
    if (amount.out > limit)
    {
        Amounts result (mulRound (
            limit, rate(), amount.in.issue (), true), limit);
        // Clamp in
        if (result.in > amount.in)
            result.in = amount.in;
        assert (result.out == limit);
        return result;
    }
    assert (amount.out <= limit);
    return amount;
}

Quality
composed_quality (Quality const& lhs, Quality const& rhs)
{
    STAmount const lhs_rate (lhs.rate ());
    assert (lhs_rate != zero);

    STAmount const rhs_rate (rhs.rate ());
    assert (rhs_rate != zero);

    STAmount const rate (mulRound (
        lhs_rate, rhs_rate, lhs_rate.issue (), true));

    std::uint64_t const stored_exponent (rate.exponent () + 100);
    std::uint64_t const stored_mantissa (rate.mantissa());

    assert ((stored_exponent > 0) && (stored_exponent <= 255));

    return Quality ((stored_exponent << (64 - 8)) | stored_mantissa);
}

Quality
Quality::round (int digits) const
{
    // Modulus for mantissa
    static const std::uint64_t mod[17] = {
        /* 0 */ 10000000000000000,
        /* 1 */  1000000000000000,
        /* 2 */   100000000000000,
        /* 3 */    10000000000000,
        /* 4 */     1000000000000,
        /* 5 */      100000000000,
        /* 6 */       10000000000,
        /* 7 */        1000000000,
        /* 8 */         100000000,
        /* 9 */          10000000,
        /* 10 */          1000000,
        /* 11 */           100000,
        /* 12 */            10000,
        /* 13 */             1000,
        /* 14 */              100,
        /* 15 */               10,
        /* 16 */                1,
    };

    auto exponent = m_value >> (64 - 8);
    auto mantissa = m_value & 0x00ffffffffffffffULL;
    mantissa += mod[digits] - 1;
    mantissa -= (mantissa % mod[digits]);

    return Quality{(exponent << (64 - 8)) | mantissa};
}

}
