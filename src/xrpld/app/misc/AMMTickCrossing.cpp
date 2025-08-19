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

#include <xrpld/app/misc/AMMTickCrossing.h>
#include <xrpld/app/misc/AMMFeeCalculation.h>
#include <xrpld/app/ledger/Directory.h>
#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace ripple {

std::pair<STAmount, STAmount>
AMMTickCrossing::executeTickCrossing(
    ApplyView& view,
    uint256 const& ammID,
    std::int32_t fromTick,
    std::int32_t toTick,
    STAmount const& amountIn,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // Validate parameters
    if (!validateTickCrossingParams(fromTick, toTick, j))
    {
        JLOG(j.warn()) << "AMMTickCrossing: Invalid parameters for tick crossing";
        return {STAmount{0}, STAmount{0}};
    }

    // Optimize the tick crossing path
    auto const ticksToCross = optimizeTickCrossingPath(view, ammID, fromTick, toTick, j);
    
    if (ticksToCross.empty())
    {
        JLOG(j.debug()) << "AMMTickCrossing: No ticks to cross";
        return {STAmount{0}, STAmount{0}};
    }

    // Execute batch tick crossing for optimal performance
    return executeBatchTickCrossing(view, ammID, ticksToCross, amountIn, tradingFee, j);
}

std::pair<STAmount, STAmount>
AMMTickCrossing::executeBatchTickCrossing(
    ApplyView& view,
    uint256 const& ammID,
    std::vector<std::int32_t> const& ticks,
    STAmount const& amountIn,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    if (ticks.empty())
    {
        return {STAmount{0}, STAmount{0}};
    }

    // Calculate total liquidity delta for all ticks
    STAmount totalLiquidityDelta{0};
    for (auto const& tick : ticks)
    {
        auto const liquidityDelta = calculateLiquidityDelta(view, ammID, tick, j);
        totalLiquidityDelta += liquidityDelta;
    }

    // Calculate optimal liquidity distribution
    auto const liquidityDeltas = calculateOptimalLiquidityDistribution(
        view, ammID, ticks, totalLiquidityDelta, j);

    // Execute tick crossings with optimized state management
    STAmount totalFee0{0}, totalFee1{0};
    
    for (size_t i = 0; i < ticks.size(); ++i)
    {
        auto const tick = ticks[i];
        auto const liquidityDelta = liquidityDeltas[i];

        // Execute single tick crossing
        if (auto const ter = executeSingleTickCrossing(view, ammID, tick, liquidityDelta, j);
            ter != tesSUCCESS)
        {
            JLOG(j.warn()) << "AMMTickCrossing: Failed to execute single tick crossing for tick " << tick;
            return {STAmount{0}, STAmount{0}};
        }

        // Calculate fees for this tick crossing
        auto const [fee0, fee1] = calculateTickCrossingFees(
            view, ammID, tick, amountIn, tradingFee, j);
        
        totalFee0 += fee0;
        totalFee1 += fee1;

        JLOG(j.debug()) << "AMMTickCrossing: Crossed tick " << tick 
                        << " with liquidity delta " << liquidityDelta
                        << ", fees: (" << fee0 << ", " << fee1 << ")";
    }

    // Update AMM state with final values
    auto const finalTick = ticks.back();
    auto const finalSqrtPriceX64 = tickToSqrtPriceX64(finalTick);
    
    if (auto const ter = updateAMMState(view, ammID, finalTick, finalSqrtPriceX64, totalLiquidityDelta, j);
        ter != tesSUCCESS)
    {
        JLOG(j.warn()) << "AMMTickCrossing: Failed to update AMM state";
        return {STAmount{0}, STAmount{0}};
    }

    JLOG(j.debug()) << "AMMTickCrossing: Completed batch tick crossing - "
                    << "crossed " << ticks.size() << " ticks, "
                    << "total fees: (" << totalFee0 << ", " << totalFee1 << ")";

    return {totalFee0, totalFee1};
}

std::int32_t
AMMTickCrossing::findNextInitializedTick(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t currentTick,
    int direction,
    beast::Journal const& j)
{
    // Validate direction
    if (direction != 1 && direction != -1)
    {
        JLOG(j.warn()) << "AMMTickCrossing: Invalid direction for finding next tick";
        return currentTick;
    }

    // Use binary search for efficient tick finding
    std::int32_t searchTick = currentTick + direction;
    std::int32_t step = direction;
    
    // Exponential search for better performance
    while (searchTick >= CONCENTRATED_LIQUIDITY_MIN_TICK && 
           searchTick <= CONCENTRATED_LIQUIDITY_MAX_TICK)
    {
        auto const tickKey = getConcentratedLiquidityTickKey(searchTick);
        auto const tickSle = view.read(keylet::unchecked(tickKey));
        
        if (tickSle && tickSle->getFieldU8(sfTickInitialized) == 1)
        {
            JLOG(j.debug()) << "AMMTickCrossing: Found next initialized tick " << searchTick;
            return searchTick;
        }

        // Exponential step for faster search
        step *= 2;
        searchTick += step;
    }

    JLOG(j.debug()) << "AMMTickCrossing: No next initialized tick found in direction " << direction;
    return currentTick;
}

STAmount
AMMTickCrossing::calculateLiquidityDelta(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t tick,
    beast::Journal const& j)
{
    // Get tick data
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickSle = view.read(keylet::unchecked(tickKey));
    
    if (!tickSle || tickSle->getFieldU8(sfTickInitialized) != 1)
    {
        JLOG(j.debug()) << "AMMTickCrossing: Tick " << tick << " not initialized";
        return STAmount{0};
    }

    // Get liquidity net from tick
    auto const liquidityNet = tickSle->getFieldAmount(sfLiquidityNet);
    
    JLOG(j.debug()) << "AMMTickCrossing: Calculated liquidity delta " << liquidityNet << " for tick " << tick;
    
    return liquidityNet;
}

TER
AMMTickCrossing::updateTickLiquidity(
    ApplyView& view,
    uint256 const& ammID,
    std::int32_t tick,
    STAmount const& liquidityDelta,
    beast::Journal const& j)
{
    // Get tick data
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickKeylet = keylet::unchecked(tickKey);
    auto const tickSle = view.read(tickKeylet);
    
    if (!tickSle)
    {
        JLOG(j.warn()) << "AMMTickCrossing: Tick " << tick << " not found for liquidity update";
        return tecAMM_TICK_NOT_INITIALIZED;
    }

    // Update liquidity values
    auto const currentLiquidityGross = tickSle->getFieldAmount(sfLiquidityGross);
    auto const currentLiquidityNet = tickSle->getFieldAmount(sfLiquidityNet);
    
    auto const newLiquidityGross = currentLiquidityGross + liquidityDelta;
    auto const newLiquidityNet = currentLiquidityNet + liquidityDelta;

    // Update tick
    auto const newTickSle = std::make_shared<SLE>(*tickSle);
    newTickSle->setFieldAmount(sfLiquidityGross, newLiquidityGross);
    newTickSle->setFieldAmount(sfLiquidityNet, newLiquidityNet);
    
    view.update(newTickSle);

    JLOG(j.debug()) << "AMMTickCrossing: Updated tick " << tick 
                    << " liquidity - gross: " << newLiquidityGross 
                    << ", net: " << newLiquidityNet;

    return tesSUCCESS;
}

TER
AMMTickCrossing::updateAMMState(
    ApplyView& view,
    uint256 const& ammID,
    std::int32_t newTick,
    std::uint64_t newSqrtPriceX64,
    STAmount const& liquidityDelta,
    beast::Journal const& j)
{
    // Get AMM data
    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.warn()) << "AMMTickCrossing: AMM not found for state update";
        return terNO_AMM;
    }

    // Get current aggregated liquidity
    auto const currentAggregatedLiquidity = ammSle->getFieldAmount(sfAggregatedLiquidity);
    auto const newAggregatedLiquidity = currentAggregatedLiquidity + liquidityDelta;

    // Update AMM state
    auto const newAmmSle = std::make_shared<SLE>(*ammSle);
    newAmmSle->setFieldU32(sfCurrentTick, newTick);
    newAmmSle->setFieldU64(sfSqrtPriceX64, newSqrtPriceX64);
    newAmmSle->setFieldAmount(sfAggregatedLiquidity, newAggregatedLiquidity);
    
    view.update(newAmmSle);

    JLOG(j.debug()) << "AMMTickCrossing: Updated AMM state - "
                    << "tick: " << newTick 
                    << ", sqrt price: " << newSqrtPriceX64
                    << ", aggregated liquidity: " << newAggregatedLiquidity;

    return tesSUCCESS;
}

std::pair<STAmount, STAmount>
AMMTickCrossing::calculateTickCrossingFees(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t tick,
    STAmount const& amountIn,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // Calculate fees based on the liquidity at this tick and the trading fee
    auto const liquidityNet = calculateLiquidityDelta(view, ammID, tick, j);
    
    if (liquidityNet <= beast::zero)
    {
        return {STAmount{0}, STAmount{0}};
    }

    // Calculate fees using the trading fee and liquidity
    auto const feeAmount = (amountIn * tradingFee) / 1000000;  // Convert basis points to fraction
    auto const feePerLiquidity = feeAmount / liquidityNet;

    // Distribute fees between asset0 and asset1 based on current price
    auto const sqrtPriceX64 = tickToSqrtPriceX64(tick);
    auto const price = static_cast<double>(sqrtPriceX64) / (1ULL << 63);
    auto const priceSquared = price * price;

    // Calculate fee distribution (simplified - in practice, this would be more sophisticated)
    auto const fee0 = feePerLiquidity / (1.0 + priceSquared);
    auto const fee1 = feePerLiquidity * priceSquared / (1.0 + priceSquared);

    JLOG(j.debug()) << "AMMTickCrossing: Calculated tick crossing fees - "
                    << "tick: " << tick << ", fee0: " << fee0 << ", fee1: " << fee1;

    return {STAmount{static_cast<std::int64_t>(fee0)}, STAmount{static_cast<std::int64_t>(fee1)}};
}

std::vector<std::int32_t>
AMMTickCrossing::optimizeTickCrossingPath(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t fromTick,
    std::int32_t toTick,
    beast::Journal const& j)
{
    std::vector<std::int32_t> ticksToCross;
    
    if (fromTick == toTick)
    {
        return ticksToCross;
    }

    // Determine direction
    int const direction = (toTick > fromTick) ? 1 : -1;
    
    // Find all initialized ticks between fromTick and toTick
    std::int32_t currentTick = fromTick;
    std::unordered_set<std::int32_t> visitedTicks;
    
    while ((direction > 0 && currentTick < toTick) || 
           (direction < 0 && currentTick > toTick))
    {
        currentTick += direction;
        
        // Check bounds
        if (currentTick < CONCENTRATED_LIQUIDITY_MIN_TICK || 
            currentTick > CONCENTRATED_LIQUIDITY_MAX_TICK)
        {
            break;
        }

        // Check if tick is initialized and not already visited
        if (visitedTicks.find(currentTick) == visitedTicks.end())
        {
            auto const tickKey = getConcentratedLiquidityTickKey(currentTick);
            auto const tickSle = view.read(keylet::unchecked(tickKey));
            
            if (tickSle && tickSle->getFieldU8(sfTickInitialized) == 1)
            {
                ticksToCross.push_back(currentTick);
                visitedTicks.insert(currentTick);
            }
        }
    }

    // Sort ticks in the correct order
    if (direction < 0)
    {
        std::reverse(ticksToCross.begin(), ticksToCross.end());
    }

    JLOG(j.debug()) << "AMMTickCrossing: Optimized path from " << fromTick 
                    << " to " << toTick << " crosses " << ticksToCross.size() << " ticks";

    return ticksToCross;
}

// Private implementation methods

TER
AMMTickCrossing::executeSingleTickCrossing(
    ApplyView& view,
    uint256 const& ammID,
    std::int32_t tick,
    STAmount const& liquidityDelta,
    beast::Journal const& j)
{
    // Get AMM data
    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.warn()) << "AMMTickCrossing: AMM not found for tick crossing";
        return terNO_AMM;
    }

    // Get current global fee growth
    auto const feeGrowthGlobal0X128 = ammSle->getFieldAmount(sfFeeGrowthGlobal0X128);
    auto const feeGrowthGlobal1X128 = ammSle->getFieldAmount(sfFeeGrowthGlobal1X128);

    // Get tick key and SLE
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickSle = view.read(keylet::unchecked(tickKey));
    
    if (!tickSle)
    {
        JLOG(j.warn()) << "AMMTickCrossing: Tick " << tick << " not found";
        return terNO_ENTRY;
    }

    // Get current tick data
    auto const currentLiquidityNet = tickSle->getFieldAmount(sfLiquidityNet);
    auto const currentLiquidityGross = tickSle->getFieldAmount(sfLiquidityGross);
    auto const feeGrowthOutside0X128 = tickSle->getFieldAmount(sfFeeGrowthOutside0X128);
    auto const feeGrowthOutside1X128 = tickSle->getFieldAmount(sfFeeGrowthOutside1X128);

    // Update tick liquidity
    auto const newLiquidityNet = currentLiquidityNet + liquidityDelta;
    auto const newLiquidityGross = currentLiquidityGross + abs(liquidityDelta);

    // Create updated tick SLE
    auto const newTickSle = std::make_shared<SLE>(*tickSle);
    newTickSle->setFieldAmount(sfLiquidityNet, newLiquidityNet);
    newTickSle->setFieldAmount(sfLiquidityGross, newLiquidityGross);

    // Update fee growth outside when crossing the tick
    // When crossing from left to right (price increasing), we need to update fee growth outside
    auto const currentTick = ammSle->getFieldU32(sfCurrentTick);
    
    if (tick <= currentTick)
    {
        // Crossing from below - update fee growth outside to current global values
        newTickSle->setFieldAmount(sfFeeGrowthOutside0X128, feeGrowthGlobal0X128);
        newTickSle->setFieldAmount(sfFeeGrowthOutside1X128, feeGrowthGlobal1X128);
    }
    else
    {
        // Crossing from above - fee growth outside remains unchanged
        // (it was already set when the tick was last crossed from below)
    }

    // Check if tick should be deleted (no liquidity remaining)
    if (newLiquidityGross <= beast::zero)
    {
        // Remove tick from ledger
        view.erase(newTickSle);
        
        JLOG(j.debug()) << "AMMTickCrossing: Removed empty tick " << tick;
    }
    else
    {
        // Update tick in ledger
        view.update(newTickSle);
        
        JLOG(j.debug()) << "AMMTickCrossing: Updated tick " << tick 
                        << " with liquidity net: " << newLiquidityNet
                        << ", gross: " << newLiquidityGross;
    }

    // Update AMM's aggregated liquidity if this affects the current range
    auto const tickLower = ammSle->getFieldU32(sfTickLower);
    auto const tickUpper = ammSle->getFieldU32(sfTickUpper);
    
    if (tick >= tickLower && tick < tickUpper)
    {
        auto const currentAggregatedLiquidity = ammSle->getFieldAmount(sfAggregatedLiquidity);
        auto const newAggregatedLiquidity = currentAggregatedLiquidity + liquidityDelta;
        
        auto const newAmmSle = std::make_shared<SLE>(*ammSle);
        newAmmSle->setFieldAmount(sfAggregatedLiquidity, newAggregatedLiquidity);
        view.update(newAmmSle);
        
        JLOG(j.debug()) << "AMMTickCrossing: Updated aggregated liquidity to " << newAggregatedLiquidity;
    }

    JLOG(j.debug()) << "AMMTickCrossing: Successfully executed single tick crossing for tick " << tick
                    << " with liquidity delta: " << liquidityDelta;
    
    return tesSUCCESS;
}

bool
AMMTickCrossing::validateTickCrossingParams(
    std::int32_t fromTick,
    std::int32_t toTick,
    beast::Journal const& j)
{
    // Validate tick bounds
    if (fromTick < CONCENTRATED_LIQUIDITY_MIN_TICK || fromTick > CONCENTRATED_LIQUIDITY_MAX_TICK)
    {
        JLOG(j.warn()) << "AMMTickCrossing: From tick out of bounds: " << fromTick;
        return false;
    }

    if (toTick < CONCENTRATED_LIQUIDITY_MIN_TICK || toTick > CONCENTRATED_LIQUIDITY_MAX_TICK)
    {
        JLOG(j.warn()) << "AMMTickCrossing: To tick out of bounds: " << toTick;
        return false;
    }

    return true;
}

std::vector<STAmount>
AMMTickCrossing::calculateOptimalLiquidityDistribution(
    ReadView const& view,
    uint256 const& ammID,
    std::vector<std::int32_t> const& ticks,
    STAmount const& totalLiquidityDelta,
    beast::Journal const& j)
{
    std::vector<STAmount> liquidityDeltas;
    liquidityDeltas.reserve(ticks.size());

    // Calculate individual liquidity deltas for each tick
    for (auto const& tick : ticks)
    {
        auto const liquidityDelta = calculateLiquidityDelta(view, ammID, tick, j);
        liquidityDeltas.push_back(liquidityDelta);
    }

    // Normalize liquidity deltas to match total
    STAmount calculatedTotal{0};
    for (auto const& delta : liquidityDeltas)
    {
        calculatedTotal += delta;
    }

    if (calculatedTotal != beast::zero)
    {
        // Scale liquidity deltas proportionally
        for (auto& delta : liquidityDeltas)
        {
            delta = (delta * totalLiquidityDelta) / calculatedTotal;
        }
    }

    JLOG(j.debug()) << "AMMTickCrossing: Calculated optimal liquidity distribution for " 
                    << ticks.size() << " ticks";

    return liquidityDeltas;
}

}  // namespace ripple
