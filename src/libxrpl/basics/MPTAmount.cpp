//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpl/basics/MPTAmount.h>

namespace ripple {

MPTAmount&
MPTAmount::operator+=(MPTAmount const& other)
{
    value_ += other.value();
    return *this;
}

MPTAmount&
MPTAmount::operator-=(MPTAmount const& other)
{
    value_ -= other.value();
    return *this;
}

MPTAmount
MPTAmount::operator-() const
{
    return MPTAmount{-value_};
}

bool
MPTAmount::operator==(MPTAmount const& other) const
{
    return value_ == other.value_;
}

bool
MPTAmount::operator==(value_type other) const
{
    return value_ == other;
}

bool
MPTAmount::operator<(MPTAmount const& other) const
{
    return value_ < other.value_;
}

Json::Value
MPTAmount::jsonClipped() const
{
    static_assert(
        std::is_signed_v<value_type> && std::is_integral_v<value_type>,
        "Expected MPTAmount to be a signed integral type");

    constexpr auto min = std::numeric_limits<Json::Int>::min();
    constexpr auto max = std::numeric_limits<Json::Int>::max();

    if (value_ < min)
        return min;
    if (value_ > max)
        return max;
    return static_cast<Json::Int>(value_);
}

MPTAmount
MPTAmount::minPositiveAmount()
{
    return MPTAmount{1};
}

}  // namespace ripple
