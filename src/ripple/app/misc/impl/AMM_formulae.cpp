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
calcAMMLPT(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue)
{
    auto const tokens = root(asset1 * asset2, 2);
    return toSTAmount(lptIssue, tokens);
}

STAmount
calcLPTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lpTokensBalance,
    std::uint16_t tfee)
{
    return toSTAmount(
        lpTokensBalance.issue(),
        lpTokensBalance *
            (root(1 + (asset1Deposit * feeMultHalf(tfee)) / asset1Balance, 2) -
             1));
}

STAmount
calcAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lpTokensBalance,
    STAmount const& lptAMMBalance,
    std::uint16_t tfee)
{
    return toSTAmount(
        asset1Balance.issue(),
        ((power(lpTokensBalance / lptAMMBalance + 1, 2) - 1) /
         feeMultHalf(tfee)) *
            asset1Balance);
}

STAmount
calcLPTokensOut(
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
            lpTokensBalance.issue(), lpTokensBalance * (1 - root(a, 2)));
}

STAmount
calcSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    std::uint16_t tfee)
{
    return toSTAmount(
        noIssue(), Number{asset2Balance} / (asset1Balance * feeMult(tfee)));
}

std::optional<STAmount>
changeSpotPrice(
    STAmount const& assetInBalance,
    STAmount const& assetOutBalance,
    STAmount const& newSP,
    std::uint16_t tfee)
{
    auto const sp = calcSpotPrice(assetInBalance, assetOutBalance, tfee);
    // can't change to a better or same SP
    if (Number(newSP) <= sp)
        return std::nullopt;
    auto const res = assetInBalance * (root(newSP / sp, 2) - 1);
    if (res > 0)
        return toSTAmount(assetInBalance.issue(), res);
    return std::nullopt;
}

STAmount
calcWithdrawalByTokens(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint32_t tfee)
{
    return toSTAmount(
        assetBalance.issue(),
        assetBalance * (1 - power(1 - lpTokens / lptAMMBalance, 2)) *
            feeMultHalf(tfee));
}

std::optional<Amounts>
changeSpotPriceQuality(
    Amounts const& pool,
    Quality const& quality,
    std::uint32_t tfee)
{
    auto const curQuality = Quality(pool);
    auto const takerPays =
        pool.in * (root(quality.rate() / curQuality.rate(), 2) - 1);
    if (takerPays > 0)
    {
        auto const saTakerPays = toSTAmount(pool.in.issue(), takerPays);
        return Amounts{saTakerPays, swapAssetIn(pool, saTakerPays, tfee)};
    }
    return std::nullopt;
}

}  // namespace ripple
