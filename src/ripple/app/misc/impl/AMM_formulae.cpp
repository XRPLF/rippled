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

#include <ripple/app/misc/AMM_formulae.h>

#include <cmath>

namespace ripple {

STAmount
ammLPTokens(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue)
{
    auto const tokens = root2(asset1 * asset2);
    return toSTAmount(lptIssue, tokens);
}

STAmount
lpTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lpTokensBalance,
    std::uint16_t tfee)
{
    return toSTAmount(
        lpTokensBalance.issue(),
        lpTokensBalance *
            (root2(1 + (asset1Deposit * feeMultHalf(tfee)) / asset1Balance) -
             1));
}

STAmount
assetIn(
    STAmount const& asset1Balance,
    STAmount const& lpTokensBalance,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee)
{
    return toSTAmount(
        asset1Balance.issue(),
        ((square(lpTokensBalance / lptAMMBalance + 1) - 1) /
         feeMultHalf(tfee)) *
            asset1Balance);
}

STAmount
lpTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lpTokensBalance,
    std::uint16_t tfee)
{
    if (auto const a =
            Number(1) - asset1Withdraw / (asset1Balance * feeMultHalf(tfee));
        a <= 0 || a >= 1)
        return STAmount{};
    else
        return toSTAmount(
            lpTokensBalance.issue(), lpTokensBalance * (1 - root2(a)));
}

STAmount
withdrawByTokens(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint32_t tfee)
{
    return toSTAmount(
        assetBalance.issue(),
        assetBalance * (1 - square(1 - lpTokens / lptAMMBalance)) *
            feeMultHalf(tfee));
}

Number
square(Number const& n)
{
    return n * n;
}

}  // namespace ripple
