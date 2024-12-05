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

#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>

namespace ripple {

Rate const parityRate(QUALITY_ONE);

namespace detail {

STAmount
as_amount(Rate const& rate)
{
    return {noIssue(), rate.value, -9, false};
}

}  // namespace detail

namespace nft {
Rate
transferFeeAsRate(std::uint16_t fee)
{
    return Rate{static_cast<std::uint32_t>(fee) * 10000};
}

}  // namespace nft

STAmount
multiply(STAmount const& amount, Rate const& rate)
{
    ASSERT(rate.value != 0, "ripple::nft::multiply : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return multiply(amount, detail::as_amount(rate), amount.asset());
}

STAmount
multiplyRound(STAmount const& amount, Rate const& rate, bool roundUp)
{
    ASSERT(rate.value != 0, "ripple::nft::multiplyRound : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return mulRound(amount, detail::as_amount(rate), amount.asset(), roundUp);
}

STAmount
multiplyRound(
    STAmount const& amount,
    Rate const& rate,
    Asset const& asset,
    bool roundUp)
{
    ASSERT(
        rate.value != 0,
        "ripple::nft::multiplyRound(Issue) : nonzero rate input");

    if (rate == parityRate)
    {
        return amount;
    }

    return mulRound(amount, detail::as_amount(rate), asset, roundUp);
}

STAmount
divide(STAmount const& amount, Rate const& rate)
{
    ASSERT(rate.value != 0, "ripple::nft::divide : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return divide(amount, detail::as_amount(rate), amount.asset());
}

STAmount
divideRound(STAmount const& amount, Rate const& rate, bool roundUp)
{
    ASSERT(rate.value != 0, "ripple::nft::divideRound : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return divRound(amount, detail::as_amount(rate), amount.asset(), roundUp);
}

STAmount
divideRound(
    STAmount const& amount,
    Rate const& rate,
    Asset const& asset,
    bool roundUp)
{
    ASSERT(
        rate.value != 0,
        "ripple::nft::divideRound(Issue) : nonzero rate input");

    if (rate == parityRate)
        return amount;

    return divRound(amount, detail::as_amount(rate), asset, roundUp);
}

}  // namespace ripple
