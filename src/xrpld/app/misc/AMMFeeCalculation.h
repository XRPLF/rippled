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

#ifndef RIPPLE_APP_MISC_AMMFEECALCULATION_H_INCLUDED
#define RIPPLE_APP_MISC_AMMFEECALCULATION_H_INCLUDED

#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpld/ledger/View.h>

#include <cstdint>
#include <utility>

namespace ripple {

/** Sophisticated fee calculation for concentrated liquidity positions.
 * 
 * This module implements advanced fee calculation algorithms that handle:
 * - Fee growth tracking across tick boundaries
 * - Position-specific fee accumulation
 * - Cross-tick fee distribution
 * - Fee rebasing and precision management
 * - High-frequency trading optimizations
 */
class AMMFeeCalculation
{
public:
    /** Calculate fee growth inside a specific tick range.
     * 
     * This is the core fee calculation algorithm that determines how much
     * fees a position has earned based on its tick range and the global
     * fee growth.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param tickLower Lower tick of the position
     * @param tickUpper Upper tick of the position
     * @param currentTick Current tick of the AMM
     * @param feeGrowthGlobal0X128 Global fee growth for asset 0
     * @param feeGrowthGlobal1X128 Global fee growth for asset 1
     * @param j Journal for logging
     * @return Pair of fee growth inside the range (feeGrowth0, feeGrowth1)
     */
    static std::pair<STAmount, STAmount>
    calculateFeeGrowthInside(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::int32_t currentTick,
        STAmount const& feeGrowthGlobal0X128,
        STAmount const& feeGrowthGlobal1X128,
        beast::Journal const& j);

    /** Calculate accumulated fees for a position.
     * 
     * This calculates the total fees earned by a position since the last
     * fee collection, taking into account the position's liquidity and
     * the fee growth inside its tick range.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param owner Position owner
     * @param tickLower Lower tick of the position
     * @param tickUpper Upper tick of the position
     * @param nonce Position nonce
     * @param j Journal for logging
     * @return Pair of accumulated fees (fee0, fee1)
     */
    static std::pair<STAmount, STAmount>
    calculateAccumulatedFees(
        ReadView const& view,
        uint256 const& ammID,
        AccountID const& owner,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::uint32_t nonce,
        beast::Journal const& j);

    /** Update fee growth for a swap operation.
     * 
     * This updates the global fee growth and position-specific fee tracking
     * when a swap occurs, ensuring accurate fee distribution.
     * 
     * @param view The ledger view (mutable)
     * @param ammID The AMM identifier
     * @param amountIn Input amount for the swap
     * @param amountOut Output amount for the swap
     * @param tradingFee Trading fee in basis points
     * @param j Journal for logging
     * @return Success/failure status
     */
    static TER
    updateFeeGrowthForSwap(
        ApplyView& view,
        uint256 const& ammID,
        STAmount const& amountIn,
        STAmount const& amountOut,
        std::uint16_t tradingFee,
        beast::Journal const& j);

    /** Calculate fee growth outside a tick.
     * 
     * This calculates the fee growth that occurs outside a specific tick,
     * which is needed for accurate fee distribution across tick boundaries.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param tick The tick to calculate fee growth outside
     * @param feeGrowthGlobal0X128 Global fee growth for asset 0
     * @param feeGrowthGlobal1X128 Global fee growth for asset 1
     * @param j Journal for logging
     * @return Pair of fee growth outside the tick (feeGrowth0, feeGrowth1)
     */
    static std::pair<STAmount, STAmount>
    calculateFeeGrowthOutside(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t tick,
        STAmount const& feeGrowthGlobal0X128,
        STAmount const& feeGrowthGlobal1X128,
        beast::Journal const& j);

    /** Update position fee tracking.
     * 
     * This updates a position's fee tracking state after fee collection
     * or position modification.
     * 
     * @param view The ledger view (mutable)
     * @param owner Position owner
     * @param tickLower Lower tick of the position
     * @param tickUpper Upper tick of the position
     * @param nonce Position nonce
     * @param feeGrowthInside0X128 Fee growth inside for asset 0
     * @param feeGrowthInside1X128 Fee growth inside for asset 1
     * @param j Journal for logging
     * @return Success/failure status
     */
    static TER
    updatePositionFeeTracking(
        ApplyView& view,
        AccountID const& owner,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::uint32_t nonce,
        STAmount const& feeGrowthInside0X128,
        STAmount const& feeGrowthInside1X128,
        beast::Journal const& j);

    /** Calculate fee growth for a specific liquidity amount.
     * 
     * This calculates how much fee growth a specific amount of liquidity
     * should receive based on the global fee growth and position parameters.
     * 
     * @param liquidity The liquidity amount
     * @param feeGrowthDelta0X128 Fee growth delta for asset 0
     * @param feeGrowthDelta1X128 Fee growth delta for asset 1
     * @param j Journal for logging
     * @return Pair of fees earned (fee0, fee1)
     */
    static std::pair<STAmount, STAmount>
    calculateFeesForLiquidity(
        STAmount const& liquidity,
        STAmount const& feeGrowthDelta0X128,
        STAmount const& feeGrowthDelta1X128,
        beast::Journal const& j);

private:
    /** Calculate fee growth inside using tick-specific data.
     * 
     * This is the internal implementation that uses tick-specific
     * fee growth data for more accurate calculations.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param tickLower Lower tick of the position
     * @param tickUpper Upper tick of the position
     * @param currentTick Current tick of the AMM
     * @param feeGrowthGlobal0X128 Global fee growth for asset 0
     * @param feeGrowthGlobal1X128 Global fee growth for asset 1
     * @param j Journal for logging
     * @return Pair of fee growth inside the range (feeGrowth0, feeGrowth1)
     */
    static std::pair<STAmount, STAmount>
    calculateFeeGrowthInsideDetailed(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::int32_t currentTick,
        STAmount const& feeGrowthGlobal0X128,
        STAmount const& feeGrowthGlobal1X128,
        beast::Journal const& j);

    /** Validate fee calculation parameters.
     * 
     * @param tickLower Lower tick
     * @param tickUpper Upper tick
     * @param currentTick Current tick
     * @param j Journal for logging
     * @return true if parameters are valid
     */
    static bool
    validateFeeCalculationParams(
        std::int32_t tickLower,
        std::int32_t tickUpper,
        std::int32_t currentTick,
        beast::Journal const& j);
};

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMMFEECALCULATION_H_INCLUDED
