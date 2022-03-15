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
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>

#include <type_traits>

namespace ripple {

namespace detail {

double
saToDouble(STAmount const& a);

template <typename T>
inline std::enable_if_t<(std::is_floating_point<T>::value), std::size_t>
decimal_places(T v)
{
    std::size_t count = 0;
    v = std::abs(v);
    auto c = v - std::floor(v);
    T factor = 10;
    T eps = std::numeric_limits<T>::epsilon() * c;

    while ((c > eps && c < (1 - eps)) &&
           count < std::numeric_limits<T>::max_digits10)
    {
        c = v * factor;
        c = c - std::floor(c);
        factor *= 10;
        eps = std::numeric_limits<T>::epsilon() * v * factor;
        count++;
    }

    return count;
}

STAmount
toSTAfromDouble(double v, Issue const& issue = noIssue());

template <typename T>
T
get(STAmount const& a)
{
    if constexpr (std::is_same_v<T, XRPAmount>)
        return a.xrp();
    else if constexpr (std::is_same_v<T, IOUAmount>)
        return a.iou();
    else
        return a;
}

}  // namespace detail

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
inline STAmount
getFeeMult(std::uint16_t tfee)
{
    STAmount const c{noIssue(), 100000};
    return divide(c - STAmount{noIssue(), tfee}, c, noIssue());
}

/** Get fee multiplier (1 - (1 - Wb) * tfee) or
 * c = 100 * 100000,  mult = (c - (100 - weight) * tfee)/ c
 * @tfee trading fee in basis points
 * @weight weight {0, 100}
 */
inline STAmount
getFeeMult(std::uint16_t tfee, std::uint16_t weight)
{
    STAmount const c{noIssue(), 10000000};  // 100 * 100000
    auto const num = c - STAmount{noIssue(), (100 - weight) * tfee};
    return divide(num, c, noIssue());
}

inline STAmount
getPct(STAmount const& asset, std::uint16_t pct)
{
    auto const ratio = divide(
        STAmount{noIssue(), pct}, STAmount{noIssue(), 100000}, noIssue());
    return multiply(asset, ratio, asset.issue());
}

#if 0
/** Swap asset out
 */
template <typename TIn, typename TOut>
TIn
swapAssetOut(
    TAmounts<TIn, TOut> const& reserves,
    Issue const& issueIn,  // TEMP, NEED LIB TO SUPPORT XRP/IOU OPS
    Issue const& issueOut,
    TOut const& out,
    std::uint8_t weightIn,
    std::uint16_t tfee)
{
    // Simplified formulae if equal weights
    assert(weightIn == 50);
    auto const feeMult = getFeeMult(tfee);
    auto const num =
        multiply(toSTAmount(reserves.in), toSTAmount(out), issueIn);
    auto const den =
        multiply(toSTAmount(reserves.out - out), feeMult, issueOut);
    return detail::get<TIn>(divide(num, den, issueIn));
}

/** Swap asset in
 */
template <typename TIn, typename TOut>
TOut
swapAssetIn(
    TAmounts<TIn, TOut> const& reserves,
    Issue const& issueIn,
    Issue const& issueOut,
    TIn const& in,
    std::uint16_t weightIn,
    std::uint8_t tfee)
{
    // Simplified formulae if equal weights
    assert(weightIn == 50);
    auto const feeMult = getFeeMult(tfee);
    auto const inwfee = [&] {
        if constexpr (std::is_same_v<TIn, STAmount>)
            return multiply(toSTAmount(in), feeMult, issueIn);
        else if constexpr (std::is_same_v<TIn, XRPAmount>)
            return multiply(toSTAmount(in), feeMult, xrpIssue());
        return multiply(toSTAmount(in), feeMult, noIssue());
    }();
    auto const num = multiply(toSTAmount(reserves.out), inwfee, issueOut);
    auto const den = toSTAmount(reserves.in) + inwfee;
    return detail::get<TOut>(divide(num, den, issueOut));
}

/** Change offer quality to match the target quality
 */
template <typename TIn, typename TOut>
std::optional<std::pair<TIn, TOut>>
changeQuality(
    TAmounts<TIn, TOut> const& reserves,
    Issue const& issueIn,
    Issue const& issueOut,
    Quality const& newQuality,
    std::uint16_t weightIn,
    std::uint8_t tfee)
{
    // Simplified formulae if equal weights
    assert(weightIn == 50);
    auto const curQuality = Quality(reserves);
    if (curQuality < newQuality)
        return std::nullopt;
    auto const mult = std::sqrt(detail::saToDouble(divide(
                          curQuality.rate(), newQuality.rate(), issueOut))) -
        1.;
    auto const out = detail::get<TOut>(multiply(
        toSTAmount(reserves.out), detail::toSTAfromDouble(mult), issueOut));
    auto const in =
        swapAssetOut(reserves, issueIn, issueOut, out, weightIn, tfee);
    return std::make_pair(in, out);
}
#endif

/** Calculate LP Tokens given asset's deposit amount.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Deposit requested asset1 deposit amount
 * @param lpTokensBalance LP Tokens balance
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return seated tokens in amount on success
 */
std::optional<STAmount>
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
std::optional<STAmount>
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
 * @return seated tokens out amount on success
 */
std::optional<STAmount>
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
