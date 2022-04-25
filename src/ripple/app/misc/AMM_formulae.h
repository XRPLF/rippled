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

#ifndef RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED
#define RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/Number.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>

#include <type_traits>

namespace ripple {

inline STAmount
toSTAmount(Issue const& issue, Number const& n)
{
    return STAmount{issue, n.mantissa(), n.exponent()};
}

/** Calculate LP Tokens given AMM pool reserves.
 * @param asset1 AMM one side of the pool reserve
 * @param asset2 AMM another side of the pool reserve
 * @param weight1 xrp pool weight
 * @return LP Tokens as IOU
 */
STAmount
calcAMMLPT(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue,
    std::uint8_t weight1);

/** Get fee multiplier (1 - tfee)
 * @tfee trading fee in basis points
 */
inline Number
feeMult(std::uint16_t tfee)
{
    return Number{1} - Number{tfee} / Number{100000};
}

/** Get fee multiplier (1 - tfee * (1 - weight))
 * @tfee trading fee in basis points
 * @weight weight {0, 100}
 */
inline Number
feeMult(std::uint16_t tfee, std::uint16_t weight)
{
    return 1 - feeMult(tfee) * (1 - Number{weight} / 100);
}

inline STAmount
getPct(STAmount const& asset, std::uint16_t pct)
{
    auto const ratio = divide(
        STAmount{noIssue(), pct}, STAmount{noIssue(), 100000}, noIssue());
    return multiply(asset, ratio, asset.issue());
}

/** Calculate LP Tokens given asset's deposit amount.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Deposit requested asset1 deposit amount
 * @param lpTokensBalance LP Tokens balance
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return tokens
 */
STAmount
calcLPTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lpTokensBalance,
    std::uint16_t weight1,
    std::uint16_t tfee);

/** Calculate asset deposit given LP Tokens.
 * @param asset1Balance current AMM asset1 balance
 * @param lpTokensBalance LP Tokens balance
 * @param ammTokensBalance AMM LPT balance
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
calcAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lpTokensBalance,
    STAmount const& ammTokensBalance,
    std::uint16_t weight1,
    std::uint16_t tfee);

/** Calculate LP Tokens given asset's withdraw amount.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Withdraw requested asset1 withdraw amount
 * @param lpTokensBalance LP Tokens balance
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return tokens out amount
 */
STAmount
calcLPTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lpTokensBalance,
    std::uint16_t weight1,
    std::uint16_t tfee);

/** Calculate AMM's Spot Price
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return spot price
 */
STAmount
calcSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    std::uint8_t weight1,
    std::uint16_t tfee);

/** Get asset2 amount based on new AMM's Spot Price.
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param newSP requested Spot Price
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
std::optional<STAmount>
changeSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& newSP,
    std::uint8_t weight1,
    std::uint16_t tfee);

/** Swap assetIn into the pool and swap out a proportional amount
 * of the other asset.
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param assetIn amount to swap in
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
swapAssetIn(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetIn,
    std::uint8_t weight1,
    std::uint16_t tfee);

/** Swap assetOut out of the pool and swap in a proportional amount
 * of the other asset.
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param assetOut amount to swap out
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
swapAssetOut(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetOut,
    std::uint8_t weight1,
    std::uint16_t tfee);

/** Slippage slope when the traded amount tends to zero
 * @param assetBalance current AMM asset balance of the asset to be traded
 * @param assetWeight asset weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
slippageSlope(
    STAmount const& assetBalance,
    std::uint8_t assetWeight,
    std::uint16_t tfee);

/** Average slippage
 * @param assetBalance current AMM asset balance
 * @param assetIn asset to calculate the slippage for
 * @param assetWeight asset weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
averageSlippage(
    STAmount const& assetBalance,
    STAmount const& assetIn,
    std::uint8_t assetWeight,
    std::uint16_t tfee);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED
