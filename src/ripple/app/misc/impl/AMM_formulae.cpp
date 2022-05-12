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
    Issue const& lptIssue,
    std::uint8_t weight1)
{
    auto const tokens = power(asset1 / asset2, weight1, 100) * asset2;
    return toSTAmount(lptIssue, tokens);
}

STAmount
calcLPTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lpTokensBalance,
    std::uint16_t weight,
    std::uint16_t tfee)
{
    return toSTAmount(
        lpTokensBalance.issue(),
        lpTokensBalance *
            (power(
                 1 + (asset1Deposit * feeMult(tfee, weight)) / asset1Balance,
                 weight,
                 100) -
             1));
}

STAmount
calcAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lpTokensBalance,
    STAmount const& lptAMMBalance,
    std::uint16_t weight1,
    std::uint16_t tfee)
{
    return toSTAmount(
        asset1Balance.issue(),
        ((power(Number{lpTokensBalance} / lptAMMBalance + 1, 100, weight1) -
          1) /
         feeMult(tfee, weight1)) *
            asset1Balance);
}

STAmount
calcLPTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lpTokensBalance,
    std::uint16_t weight,
    std::uint16_t tfee)
{
    return toSTAmount(
        lpTokensBalance.issue(),
        lpTokensBalance *
            (1 -
             power(
                 1 - asset1Withdraw / (asset1Balance * feeMult(tfee, weight)),
                 weight,
                 100)));
}

STAmount
calcSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return toSTAmount(
        noIssue(),
        Number{asset2Balance} * weight1 /
            (asset1Balance * (100 - weight1) * feeMult(tfee)));
}

std::optional<STAmount>
changeSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& newSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const sp = calcSpotPrice(asset1Balance, asset2Balance, weight1, tfee);
    auto const res = asset1Balance * (power(newSP / sp, weight1, 100) - 1);
    if (res > 0)
        return toSTAmount(asset1Balance.issue(), res);
    return std::nullopt;
}

STAmount
swapAssetIn(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetIn,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return toSTAmount(
        asset2Balance.issue(),
        asset2Balance *
            (1 -
             power(
                 asset1Balance / (asset1Balance + assetIn * feeMult(tfee)),
                 weight1,
                 100 - weight1)));
}

STAmount
swapAssetOut(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetOut,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return toSTAmount(
        asset2Balance.issue(),
        asset2Balance *
            (power(
                 asset1Balance / (asset1Balance - assetOut),
                 weight1,
                 100 - weight1) -
             1) /
            feeMult(tfee));
}

STAmount
calcWithdrawalByTokens(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint8_t weight,
    std::uint32_t tfee)
{
    return toSTAmount(
        assetBalance.issue(),
        assetBalance * (1 - power(1 - lpTokens / lptAMMBalance, 100, weight)) *
            feeMult(tfee, weight));
}

Number
slippageSlopeIn(
    STAmount const& assetBalance,
    STAmount const& assetIn,
    std::uint8_t assetWeight,
    std::uint16_t tfee)
{
    return feeMult(tfee) * 100 / (2 * assetBalance * (100 - assetWeight));
}

Number
averageSlippageIn(
    STAmount const& assetBalance,
    STAmount const& assetIn,
    std::uint8_t assetWeight,
    std::uint16_t tfee)
{
    return assetIn * slippageSlopeIn(assetBalance, assetIn, assetWeight, tfee);
}

Number
slippageSlopeOut(
    STAmount const& assetBalance,
    STAmount const& assetOut,
    std::uint8_t assetWeight,
    std::uint16_t tfee)
{
    return Number{100} / (2 * assetBalance * assetWeight);
}

Number
averageSlippageOut(
    STAmount const& assetBalance,
    STAmount const& assetOut,
    std::uint8_t assetWeight,
    std::uint16_t tfee)
{
    return assetOut *
        slippageSlopeOut(assetBalance, assetOut, assetWeight, tfee);
}

}  // namespace ripple
