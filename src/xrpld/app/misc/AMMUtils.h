//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_AMMUTILS_H_INCLUDED
#define RIPPLE_APP_MISC_AMMUTILS_H_INCLUDED

#include <xrpld/ledger/View.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <utility>

namespace ripple {

// Template alias for amount pairs used in AMM operations
template <typename TIn, typename TOut>
using TAmountPair = std::pair<TIn, TOut>;

class ReadView;
class ApplyView;
class Sandbox;
class NetClock;

/** Get AMM pool balances.
 */
std::pair<STAmount, STAmount>
ammPoolHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue1,
    Issue const& issue2,
    FreezeHandling freezeHandling,
    beast::Journal const j);

/** Get AMM pool and LP token balances. If both optIssue are
 * provided then they are used as the AMM token pair issues.
 * Otherwise the missing issues are fetched from ammSle.
 */
Expected<std::tuple<STAmount, STAmount, STAmount>, TER>
ammHolds(
    ReadView const& view,
    SLE const& ammSle,
    std::optional<Issue> const& optIssue1,
    std::optional<Issue> const& optIssue2,
    FreezeHandling freezeHandling,
    beast::Journal const j);

/** Get the balance of LP tokens.
 */
STAmount
ammLPHolds(
    ReadView const& view,
    Currency const& cur1,
    Currency const& cur2,
    AccountID const& ammAccount,
    AccountID const& lpAccount,
    beast::Journal const j);

STAmount
ammLPHolds(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& lpAccount,
    beast::Journal const j);

/** Get AMM trading fee for the given account. The fee is discounted
 * if the account is the auction slot owner or one of the slot's authorized
 * accounts.
 */
std::uint16_t
getTradingFee(
    ReadView const& view,
    SLE const& ammSle,
    AccountID const& account);

/** Returns total amount held by AMM for the given token.
 */
STAmount
ammAccountHolds(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& issue);

/** Delete trustlines to AMM. If all trustlines are deleted then
 * AMM object and account are deleted. Otherwise tecIMPCOMPLETE is returned.
 */

// Concentrated Liquidity Fee Functions

/** Calculate fee growth for concentrated liquidity positions.
 */
std::pair<STAmount, STAmount>
ammConcentratedLiquidityFeeGrowth(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    STAmount const& amountIn,
    STAmount const& amountOut,
    std::uint16_t tradingFee,
    beast::Journal const& j);

/** Update fee growth for a concentrated liquidity position.
 */
TER
ammConcentratedLiquidityUpdatePositionFees(
    ApplyView& view,
    Keylet const& positionKey,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::int32_t currentTick,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j);

/** Calculate fees owed to a concentrated liquidity position.
 */
std::pair<STAmount, STAmount>
ammConcentratedLiquidityCalculateFeesOwed(
    ReadView const& view,
    Keylet const& positionKey,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j);

/** Update tick fee growth for concentrated liquidity.
 */
TER
ammConcentratedLiquidityUpdateTickFeeGrowth(
    ApplyView& view,
    std::int32_t tick,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    bool isAboveCurrentTick,
    beast::Journal const& j);

// Integrated AMM swap functions
template <typename TIn, typename TOut>
TOut
ammSwapAssetIn(
    ReadView const& view,
    uint256 const& ammID,
    TAmountPair<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j);

template <typename TIn, typename TOut>
TOut
ammConcentratedLiquiditySwapAssetIn(
    ReadView const& view,
    uint256 const& ammID,
    TAmountPair<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j);

// Tick crossing functions
template <typename TIn, typename TOut>
std::pair<TOut, TER>
ammConcentratedLiquiditySwapWithTickCrossing(
    ApplyView& view,
    uint256 const& ammID,
    TIn const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j);

std::uint64_t
calculateTargetSqrtPrice(
    std::uint64_t currentSqrtPriceX64,
    STAmount const& assetIn,
    std::uint16_t tradingFee,
    beast::Journal const& j);

std::int32_t
findNextInitializedTick(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    bool ascending,
    beast::Journal const& j);

std::tuple<STAmount, STAmount, std::uint64_t>
calculateSwapStep(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    std::uint64_t currentSqrtPriceX64,
    std::int32_t nextTick,
    STAmount const& maxInput,
    std::uint16_t tradingFee,
    beast::Journal const& j);

STAmount
calculateOutputForInput(
    std::uint64_t sqrtPriceStartX64,
    std::uint64_t sqrtPriceEndX64,
    STAmount const& input,
    beast::Journal const& j);

std::pair<STAmount, STAmount>
calculateFeeGrowthForSwap(
    STAmount const& input,
    STAmount const& output,
    std::uint16_t tradingFee,
    beast::Journal const& j);

TER
crossTick(
    ApplyView& view,
    uint256 const& ammID,
    std::int32_t tick,
    std::uint64_t newSqrtPriceX64,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j);

// Helper functions for price conversion
std::int32_t
sqrtPriceX64ToTick(std::uint64_t sqrtPriceX64);

std::uint64_t
tickToSqrtPriceX64(std::int32_t tick);

// Helper function for calculating fee growth inside a tick range
std::pair<STAmount, STAmount>
ammConcentratedLiquidityCalculateFeeGrowthInside(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::int32_t currentTick,
    STAmount const& feeGrowthGlobal0,
    STAmount const& feeGrowthGlobal1,
    beast::Journal const& j);
TER
deleteAMMAccount(
    Sandbox& view,
    Issue const& asset,
    Issue const& asset2,
    beast::Journal j);

/** Initialize Auction and Voting slots and set the trading/discounted fee.
 */
void
initializeFeeAuctionVote(
    ApplyView& view,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account,
    Issue const& lptIssue,
    std::uint16_t tfee);

/** Return true if the Liquidity Provider is the only AMM provider, false
 * otherwise. Return tecINTERNAL if encountered an unexpected condition,
 * for instance Liquidity Provider has more than one LPToken trustline.
 */
Expected<bool, TER>
isOnlyLiquidityProvider(
    ReadView const& view,
    Issue const& ammIssue,
    AccountID const& lpAccount);

/** Due to rounding, the LPTokenBalance of the last LP might
 * not match the LP's trustline balance. If it's within the tolerance,
 * update LPTokenBalance to match the LP's trustline balance.
 */
Expected<bool, TER>
verifyAndAdjustLPTokenBalance(
    Sandbox& sb,
    STAmount const& lpTokens,
    std::shared_ptr<SLE>& ammSle,
    AccountID const& account);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMMUTILS_H_INCLUDED
