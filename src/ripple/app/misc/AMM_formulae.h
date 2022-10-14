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

namespace {

/** Save, set, restore Number rounding mode.
 * Applies to XRP only.
 */
class RoundingMode
{
    bool xrp_;
    Number::rounding_mode mode_;

public:
    RoundingMode(Issue const& issue, Number::rounding_mode mode)
        : xrp_(isXRP(issue)), mode_(Number::getround())
    {
        if (xrp_)
            Number::setround(mode);
    }
    ~RoundingMode()
    {
        if (xrp_)
            Number::setround(mode_);
    }
};

}  // namespace

template <typename T>
Issue
getIssue(T const& amt)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
        return noIssue();
    if constexpr (std::is_same_v<XRPAmount, T>)
        return xrpIssue();
    if constexpr (std::is_same_v<STAmount, T>)
        return amt.issue();
}

template <typename T>
T
toAmount(
    Issue const& issue,
    Number const& n,
    Number::rounding_mode mode = Number::getround())
{
    RoundingMode rm(issue, mode);
    if constexpr (std::is_same_v<IOUAmount, T>)
        return IOUAmount(n);
    if constexpr (std::is_same_v<XRPAmount, T>)
        return XRPAmount(static_cast<std::int64_t>(n));
    if constexpr (std::is_same_v<STAmount, T>)
    {
        if (isXRP(issue))
            return STAmount(issue, static_cast<std::int64_t>(n));
        return STAmount(issue, n.mantissa(), n.exponent());
    }
}

inline STAmount
toSTAmount(
    Issue const& issue,
    Number const& n,
    Number::rounding_mode mode = Number::getround())
{
    return toAmount<STAmount>(issue, n, mode);
}

template <typename T>
STAmount
toSTAmount(T const& a, Issue const& issue)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
        return toSTAmount(a, issue);
    if constexpr (std::is_same_v<XRPAmount, T>)
        return toSTAmount(a);
    if constexpr (std::is_same_v<STAmount, T>)
        return a;
}

template <typename T>
T
get(STAmount const& a)
{
    if constexpr (std::is_same_v<IOUAmount, T>)
        return a.iou();
    if constexpr (std::is_same_v<XRPAmount, T>)
        return a.xrp();
    if constexpr (std::is_same_v<STAmount, T>)
        return a;
}

/** Calculate LP Tokens given AMM pool reserves.
 * @param asset1 AMM one side of the pool reserve
 * @param asset2 AMM another side of the pool reserve
 * @return LP Tokens as IOU
 */
STAmount
calcAMMLPT(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue);

/** Convert to the fee from the basis points
 * @param tfee  trading fee in basis points
 */
inline Number
getFee(std::uint16_t tfee)
{
    return Number{tfee} / Number{100000};
}

/** Get fee multiplier (1 - tfee)
 * @tfee trading fee in basis points
 */
inline Number
feeMult(std::uint16_t tfee)
{
    return 1 - getFee(tfee);
}

/** Get fee multiplier (1 - tfee / 2)
 * @tfee trading fee in basis points
 */
inline Number
feeMultHalf(std::uint16_t tfee)
{
    return 1 - getFee(tfee) / 2;
}

/** Calculate LP Tokens given asset's deposit amount.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Deposit requested asset1 deposit amount
 * @param lpTokensBalance LP Tokens balance
 * @param tfee trading fee in basis points
 * @return tokens
 */
STAmount
calcLPTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lpTokensBalance,
    std::uint16_t tfee);

/** Calculate asset deposit given LP Tokens.
 * @param asset1Balance current AMM asset1 balance
 * @param lpTokensBalance LP Tokens balance
 * @param ammTokensBalance AMM LPT balance
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
calcAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lpTokensBalance,
    STAmount const& ammTokensBalance,
    std::uint16_t tfee);

/** Calculate LP Tokens given asset's withdraw amount. Return 0
 * if can't calculate.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Withdraw requested asset1 withdraw amount
 * @param lpTokensBalance LP Tokens balance
 * @param tfee trading fee in basis points
 * @return tokens out amount
 */
STAmount
calcLPTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lpTokensBalance,
    std::uint16_t tfee);

/** Calculate asset withdrawal by tokens
 * @param assetBalance balance of the asset being withdrawn
 * @param lptAMMBalance total AMM Tokens balance
 * @param lpTokens LP Tokens balance
 * @param tfee trading fee in basis points
 * @return calculated asset amount
 */
STAmount
calcWithdrawalByTokens(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint32_t tfee);

/** Find in/out amounts to change the spot price quality to the requested
 * quality.
 * @param pool AMM pool balances
 * @param quality requested quality
 * @param tfee trading fee in basis points
 * @return seated in/out amounts if the quality can be changed
 */
template <typename TIn, typename TOut>
std::optional<TAmounts<TIn, TOut>>
changeSpotPriceQuality(
    TAmounts<TIn, TOut> const& pool,
    Quality const& quality,
    std::uint32_t tfee)
{
    if (auto const nTakerPays =
            pool.in * (root2(quality.rate() / Quality(pool).rate()) - 1);
        nTakerPays > 0)
    {
        auto const takerPays = toAmount<TIn>(
            getIssue(pool.in), nTakerPays, Number::rounding_mode::upward);
        return TAmounts<TIn, TOut>{
            takerPays, swapAssetIn(pool, takerPays, tfee)};
    }
    return std::nullopt;
}

/** AMM pool invariant - the product (A * B) after swap in/out has to remain
 * at least the same: (A + in) * (B - out) >= A * B
 * XRP round-off may result in a smaller product after swap in/out.
 * To address this:
 *   - if on swapIn the out is XRP then the amount is round-off
 *     downward, making the product slightly larger since out
 *     value is reduced.
 *   - if on swapOut the in is XRP then the amount is round-off
 *     upward, making the product slightly larger since in
 *     value is increased.
 */

/** Swap assetIn into the pool and swap out a proportional amount
 * of the other asset.
 * @param pool current AMM pool balances
 * @param assetIn amount to swap in
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TIn, typename TOut>
TOut
swapAssetIn(
    TAmounts<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tfee)
{
    return toAmount<TOut>(
        getIssue(pool.out),
        pool.out - (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
        Number::rounding_mode::downward);
}

/** Swap assetOut out of the pool and swap in a proportional amount
 * of the other asset.
 * @param pool current AMM pool balances
 * @param assetOut amount to swap out
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TIn, typename TOut>
TIn
swapAssetOut(
    TAmounts<TIn, TOut> const& pool,
    TOut const& assetOut,
    std::uint16_t tfee)
{
    return toAmount<TIn>(
        getIssue(pool.in),
        ((pool.in * pool.out) / (pool.out - assetOut) - pool.in) /
            feeMult(tfee),
        Number::rounding_mode::upward);
}

/** Return square of n.
 */
Number
square(Number const& n);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED
