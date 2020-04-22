//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_RATE_H_INCLUDED
#define RIPPLE_PROTOCOL_RATE_H_INCLUDED

#include <ripple/protocol/STAmount.h>
#include <boost/operators.hpp>
#include <cassert>
#include <cstdint>
#include <ostream>

namespace ripple {

/** Represents a transfer rate

    Transfer rates are specified as fractions of 1 billion.
    For example, a transfer rate of 1% is represented as
    1,010,000,000.
*/
struct Rate : private boost::totally_ordered<Rate>
{
    std::uint32_t value;

    Rate() = delete;

    explicit Rate(std::uint32_t rate) : value(rate)
    {
    }
};

inline bool
operator==(Rate const& lhs, Rate const& rhs) noexcept
{
    return lhs.value == rhs.value;
}

inline bool
operator<(Rate const& lhs, Rate const& rhs) noexcept
{
    return lhs.value < rhs.value;
}

inline std::ostream&
operator<<(std::ostream& os, Rate const& rate)
{
    os << rate.value;
    return os;
}

STAmount
multiply(STAmount const& amount, Rate const& rate);

STAmount
multiplyRound(STAmount const& amount, Rate const& rate, bool roundUp);

STAmount
multiplyRound(
    STAmount const& amount,
    Rate const& rate,
    Issue const& issue,
    bool roundUp);

STAmount
divide(STAmount const& amount, Rate const& rate);

STAmount
divideRound(STAmount const& amount, Rate const& rate, bool roundUp);

STAmount
divideRound(
    STAmount const& amount,
    Rate const& rate,
    Issue const& issue,
    bool roundUp);

/** A transfer rate signifying a 1:1 exchange */
extern Rate const parityRate;

}  // namespace ripple

#endif
