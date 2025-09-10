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

#ifndef RIPPLE_APP_MISC_AMMTICKCROSSING_H_INCLUDED
#define RIPPLE_APP_MISC_AMMTICKCROSSING_H_INCLUDED

#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpld/ledger/View.h>

#include <cstdint>
#include <vector>
#include <utility>

namespace ripple {

/** Optimized tick crossing for high-frequency trading.
 * 
 * This module implements advanced tick crossing algorithms optimized for:
 * - High-frequency trading scenarios
 * - Batch tick crossing operations
 * - Efficient liquidity updates
 * - Minimal state changes
 * - Atomic operations
 * - Performance optimization
 */
class AMMTickCrossing
{
public:
    /** Execute optimized tick crossing for a swap operation.
     * 
     * This is the main entry point for tick crossing during swaps.
     * It handles the entire tick crossing process efficiently.
     * 
     * @param view The ledger view (mutable)
     * @param ammID The AMM identifier
     * @param fromTick Starting tick
     * @param toTick Target tick
     * @param amountIn Input amount for the swap
     * @param tradingFee Trading fee in basis points
     * @param j Journal for logging
     * @return Pair of fees collected (fee0, fee1)
     */
    static std::pair<STAmount, STAmount>
    executeTickCrossing(
        ApplyView& view,
        uint256 const& ammID,
        std::int32_t fromTick,
        std::int32_t toTick,
        STAmount const& amountIn,
        std::uint16_t tradingFee,
        beast::Journal const& j);

    /** Execute batch tick crossing for multiple ticks.
     * 
     * This optimizes crossing multiple ticks in a single operation,
     * reducing the number of state changes and improving performance.
     * 
     * @param view The ledger view (mutable)
     * @param ammID The AMM identifier
     * @param ticks Vector of ticks to cross
     * @param amountIn Input amount for the swap
     * @param tradingFee Trading fee in basis points
     * @param j Journal for logging
     * @return Pair of total fees collected (fee0, fee1)
     */
    static std::pair<STAmount, STAmount>
    executeBatchTickCrossing(
        ApplyView& view,
        uint256 const& ammID,
        std::vector<std::int32_t> const& ticks,
        STAmount const& amountIn,
        std::uint16_t tradingFee,
        beast::Journal const& j);

    /** Find the next initialized tick in the given direction.
     * 
     * This efficiently finds the next tick that has liquidity,
     * optimizing for high-frequency trading scenarios.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param currentTick Current tick position
     * @param direction Direction to search (1 for up, -1 for down)
     * @param j Journal for logging
     * @return Next initialized tick, or current tick if none found
     */
    static std::int32_t
    findNextInitializedTick(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t currentTick,
        int direction,
        beast::Journal const& j);

    /** Calculate liquidity delta for tick crossing.
     * 
     * This calculates how much liquidity should be added/removed
     * when crossing a specific tick.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param tick The tick to calculate liquidity delta for
     * @param j Journal for logging
     * @return Liquidity delta (positive for adding, negative for removing)
     */
    static STAmount
    calculateLiquidityDelta(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t tick,
        beast::Journal const& j);

    /** Update tick liquidity during crossing.
     * 
     * This efficiently updates the liquidity at a tick when it's crossed,
     * optimizing for minimal state changes.
     * 
     * @param view The ledger view (mutable)
     * @param ammID The AMM identifier
     * @param tick The tick to update
     * @param liquidityDelta Change in liquidity
     * @param j Journal for logging
     * @return Success/failure status
     */
    static TER
    updateTickLiquidity(
        ApplyView& view,
        uint256 const& ammID,
        std::int32_t tick,
        STAmount const& liquidityDelta,
        beast::Journal const& j);

    /** Update AMM state after tick crossing.
     * 
     * This updates the AMM's current tick, sqrt price, and aggregated liquidity
     * after completing tick crossing operations.
     * 
     * @param view The ledger view (mutable)
     * @param ammID The AMM identifier
     * @param newTick New current tick
     * @param newSqrtPriceX64 New sqrt price
     * @param liquidityDelta Total liquidity delta from all crossed ticks
     * @param j Journal for logging
     * @return Success/failure status
     */
    static TER
    updateAMMState(
        ApplyView& view,
        uint256 const& ammID,
        std::int32_t newTick,
        std::uint64_t newSqrtPriceX64,
        STAmount const& liquidityDelta,
        beast::Journal const& j);

    /** Calculate fees for tick crossing.
     * 
     * This calculates the fees that should be distributed to positions
     * when crossing a specific tick.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param tick The tick being crossed
     * @param amountIn Input amount for the swap
     * @param tradingFee Trading fee in basis points
     * @param j Journal for logging
     * @return Pair of fees (fee0, fee1)
     */
    static std::pair<STAmount, STAmount>
    calculateTickCrossingFees(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t tick,
        STAmount const& amountIn,
        std::uint16_t tradingFee,
        beast::Journal const& j);

    /** Optimize tick crossing path.
     * 
     * This finds the optimal path for crossing multiple ticks,
     * minimizing the number of state changes and maximizing efficiency.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param fromTick Starting tick
     * @param toTick Target tick
     * @param j Journal for logging
     * @return Vector of ticks to cross in order
     */
    static std::vector<std::int32_t>
    optimizeTickCrossingPath(
        ReadView const& view,
        uint256 const& ammID,
        std::int32_t fromTick,
        std::int32_t toTick,
        beast::Journal const& j);

private:
    /** Execute single tick crossing with optimized state management.
     * 
     * @param view The ledger view (mutable)
     * @param ammID The AMM identifier
     * @param tick The tick to cross
     * @param liquidityDelta Liquidity delta for this tick
     * @param j Journal for logging
     * @return Success/failure status
     */
    static TER
    executeSingleTickCrossing(
        ApplyView& view,
        uint256 const& ammID,
        std::int32_t tick,
        STAmount const& liquidityDelta,
        beast::Journal const& j);

    /** Validate tick crossing parameters.
     * 
     * @param fromTick Starting tick
     * @param toTick Target tick
     * @param j Journal for logging
     * @return true if parameters are valid
     */
    static bool
    validateTickCrossingParams(
        std::int32_t fromTick,
        std::int32_t toTick,
        beast::Journal const& j);

    /** Calculate optimal liquidity distribution.
     * 
     * @param view The ledger view
     * @param ammID The AMM identifier
     * @param ticks Vector of ticks to cross
     * @param totalLiquidityDelta Total liquidity delta
     * @param j Journal for logging
     * @return Vector of liquidity deltas for each tick
     */
    static std::vector<STAmount>
    calculateOptimalLiquidityDistribution(
        ReadView const& view,
        uint256 const& ammID,
        std::vector<std::int32_t> const& ticks,
        STAmount const& totalLiquidityDelta,
        beast::Journal const& j);
};

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMMTICKCROSSING_H_INCLUDED
