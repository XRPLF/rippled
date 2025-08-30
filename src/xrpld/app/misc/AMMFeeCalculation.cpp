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

namespace ripple {

std::pair<STAmount, STAmount>
AMMFeeCalculation::calculateFeeGrowthInside(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::int32_t currentTick,
    STAmount const& feeGrowthGlobal0X128,
    STAmount const& feeGrowthGlobal1X128,
    beast::Journal const& j)
{
    // Validate parameters
    if (!validateFeeCalculationParams(tickLower, tickUpper, currentTick, j))
    {
        JLOG(j.warn()) << "AMMFeeCalculation: Invalid parameters for fee growth calculation";
        return {STAmount{0}, STAmount{0}};
    }

    // Use detailed calculation for better accuracy
    return calculateFeeGrowthInsideDetailed(
        view, ammID, tickLower, tickUpper, currentTick,
        feeGrowthGlobal0X128, feeGrowthGlobal1X128, j);
}

std::pair<STAmount, STAmount>
AMMFeeCalculation::calculateAccumulatedFees(
    ReadView const& view,
    uint256 const& ammID,
    AccountID const& owner,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::uint32_t nonce,
    beast::Journal const& j)
{
    // Get position
    auto const positionKey = getConcentratedLiquidityPositionKey(
        owner, tickLower, tickUpper, nonce);
    auto const positionSle = view.read(keylet::unchecked(positionKey));
    if (!positionSle)
    {
        JLOG(j.debug()) << "AMMFeeCalculation: Position not found for fee calculation";
        return {STAmount{0}, STAmount{0}};
    }

    // Get AMM data
    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.warn()) << "AMMFeeCalculation: AMM not found for fee calculation";
        return {STAmount{0}, STAmount{0}};
    }

    auto const currentTick = ammSle->getFieldU32(sfCurrentTick);
    auto const feeGrowthGlobal0X128 = ammSle->getFieldAmount(sfFeeGrowthGlobal0X128);
    auto const feeGrowthGlobal1X128 = ammSle->getFieldAmount(sfFeeGrowthGlobal1X128);

    // Get current position state
    auto const currentFeeGrowthInside0X128 = positionSle->getFieldAmount(sfFeeGrowthInside0LastX128);
    auto const currentFeeGrowthInside1X128 = positionSle->getFieldAmount(sfFeeGrowthInside1LastX128);
    auto const currentTokensOwed0 = positionSle->getFieldAmount(sfTokensOwed0);
    auto const currentTokensOwed1 = positionSle->getFieldAmount(sfTokensOwed1);
    auto const positionLiquidity = positionSle->getFieldAmount(sfLiquidity);

    // Calculate current fee growth inside the position's range
    auto const [feeGrowthInside0X128, feeGrowthInside1X128] = calculateFeeGrowthInside(
        view, ammID, tickLower, tickUpper, currentTick,
        feeGrowthGlobal0X128, feeGrowthGlobal1X128, j);

    // Calculate fee growth delta
    auto const feeGrowthDelta0X128 = feeGrowthInside0X128 - currentFeeGrowthInside0X128;
    auto const feeGrowthDelta1X128 = feeGrowthInside1X128 - currentFeeGrowthInside1X128;

    // Calculate new fees earned
    auto const [newFees0, newFees1] = calculateFeesForLiquidity(
        positionLiquidity, feeGrowthDelta0X128, feeGrowthDelta1X128, j);

    // Add to existing tokens owed
    auto const totalFees0 = currentTokensOwed0 + newFees0;
    auto const totalFees1 = currentTokensOwed1 + newFees1;

    JLOG(j.debug()) << "AMMFeeCalculation: Calculated accumulated fees - "
                    << "fee0: " << totalFees0 << ", fee1: " << totalFees1
                    << " for position " << positionKey;

    return {totalFees0, totalFees1};
}

TER
AMMFeeCalculation::updateFeeGrowthForSwap(
    ApplyView& view,
    uint256 const& ammID,
    STAmount const& amountIn,
    STAmount const& amountOut,
    std::uint16_t tradingFee,
    beast::Journal const& j)
{
    // Get AMM data
    auto const ammSle = view.read(keylet::amm(ammID));
    if (!ammSle)
    {
        JLOG(j.warn()) << "AMMFeeCalculation: AMM not found for fee growth update";
        return terNO_AMM;
    }

    // Calculate fees from the swap
    auto const feeAmount = (amountIn * tradingFee) / 1000000;  // Convert basis points to fraction

    // Get current global fee growth
    auto const currentFeeGrowth0X128 = ammSle->getFieldAmount(sfFeeGrowthGlobal0X128);
    auto const currentFeeGrowth1X128 = ammSle->getFieldAmount(sfFeeGrowthGlobal1X128);

    // Get current aggregated liquidity
    auto const aggregatedLiquidity = ammSle->getFieldAmount(sfAggregatedLiquidity);

    if (aggregatedLiquidity <= beast::zero)
    {
        JLOG(j.debug()) << "AMMFeeCalculation: No liquidity for fee growth update";
        return tesSUCCESS;  // No liquidity means no fees to distribute
    }

    // Calculate fee growth increment
    // Fee growth = (fee amount * 2^128) / aggregated liquidity
    auto const feeGrowthIncrement0X128 = (feeAmount * (STAmount{1} << 128)) / aggregatedLiquidity;
    auto const feeGrowthIncrement1X128 = (feeAmount * (STAmount{1} << 128)) / aggregatedLiquidity;

    // Update global fee growth
    auto const newFeeGrowth0X128 = currentFeeGrowth0X128 + feeGrowthIncrement0X128;
    auto const newFeeGrowth1X128 = currentFeeGrowth1X128 + feeGrowthIncrement1X128;

    // Update AMM
    auto const newAmmSle = std::make_shared<SLE>(*ammSle);
    newAmmSle->setFieldAmount(sfFeeGrowthGlobal0X128, newFeeGrowth0X128);
    newAmmSle->setFieldAmount(sfFeeGrowthGlobal1X128, newFeeGrowth1X128);

    view.update(newAmmSle);

    JLOG(j.debug()) << "AMMFeeCalculation: Updated fee growth for swap - "
                    << "fee: " << feeAmount << ", new growth0: " << newFeeGrowth0X128
                    << ", new growth1: " << newFeeGrowth1X128;

    return tesSUCCESS;
}

std::pair<STAmount, STAmount>
AMMFeeCalculation::calculateFeeGrowthOutside(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t tick,
    STAmount const& feeGrowthGlobal0X128,
    STAmount const& feeGrowthGlobal1X128,
    beast::Journal const& j)
{
    // Get tick data
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickSle = view.read(keylet::unchecked(tickKey));
    if (!tickSle)
    {
        JLOG(j.debug()) << "AMMFeeCalculation: Tick not found for fee growth outside calculation";
        return {STAmount{0}, STAmount{0}};
    }

    // Get fee growth outside from tick data
    auto const feeGrowthOutside0X128 = tickSle->getFieldAmount(sfFeeGrowthOutside0X128);
    auto const feeGrowthOutside1X128 = tickSle->getFieldAmount(sfFeeGrowthOutside1X128);

    return {feeGrowthOutside0X128, feeGrowthOutside1X128};
}

TER
AMMFeeCalculation::updatePositionFeeTracking(
    ApplyView& view,
    AccountID const& owner,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::uint32_t nonce,
    STAmount const& feeGrowthInside0X128,
    STAmount const& feeGrowthInside1X128,
    beast::Journal const& j)
{
    // Get position
    auto const positionKey = getConcentratedLiquidityPositionKey(
        owner, tickLower, tickUpper, nonce);
    auto const positionKeylet = keylet::unchecked(positionKey);
    auto const positionSle = view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(j.warn()) << "AMMFeeCalculation: Position not found for fee tracking update";
        return tecAMM_POSITION_NOT_FOUND;
    }

    // Update position fee tracking
    auto const newPositionSle = std::make_shared<SLE>(*positionSle);
    newPositionSle->setFieldAmount(sfFeeGrowthInside0LastX128, feeGrowthInside0X128);
    newPositionSle->setFieldAmount(sfFeeGrowthInside1LastX128, feeGrowthInside1X128);

    view.update(newPositionSle);

    JLOG(j.debug()) << "AMMFeeCalculation: Updated position fee tracking for position " << positionKey;

    return tesSUCCESS;
}

std::pair<STAmount, STAmount>
AMMFeeCalculation::calculateFeesForLiquidity(
    STAmount const& liquidity,
    STAmount const& feeGrowthDelta0X128,
    STAmount const& feeGrowthDelta1X128,
    beast::Journal const& j)
{
    // Calculate fees using the formula: fees = liquidity * feeGrowthDelta / 2^128
    // This ensures precise fee calculation with proper scaling

    if (liquidity <= beast::zero)
    {
        return {STAmount{0}, STAmount{0}};
    }

    // Use high-precision arithmetic for fee calculations
    auto const fees0 = (liquidity * feeGrowthDelta0X128) / (STAmount{1} << 128);
    auto const fees1 = (liquidity * feeGrowthDelta1X128) / (STAmount{1} << 128);

    JLOG(j.debug()) << "AMMFeeCalculation: Calculated fees for liquidity - "
                    << "liquidity: " << liquidity << ", fees0: " << fees0 << ", fees1: " << fees1;

    return {fees0, fees1};
}

// Private implementation methods

std::pair<STAmount, STAmount>
AMMFeeCalculation::calculateFeeGrowthInsideDetailed(
    ReadView const& view,
    uint256 const& ammID,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::int32_t currentTick,
    STAmount const& feeGrowthGlobal0X128,
    STAmount const& feeGrowthGlobal1X128,
    beast::Journal const& j)
{
    // This is the sophisticated fee calculation algorithm that handles
    // cross-tick fee distribution and position-specific fee accumulation

    // Get fee growth outside for both ticks
    auto const [feeGrowthOutsideLower0X128, feeGrowthOutsideLower1X128] = 
        calculateFeeGrowthOutside(view, ammID, tickLower, feeGrowthGlobal0X128, feeGrowthGlobal1X128, j);
    
    auto const [feeGrowthOutsideUpper0X128, feeGrowthOutsideUpper1X128] = 
        calculateFeeGrowthOutside(view, ammID, tickUpper, feeGrowthGlobal0X128, feeGrowthGlobal1X128, j);

    // Calculate fee growth inside based on current tick position
    STAmount feeGrowthInside0X128, feeGrowthInside1X128;

    if (currentTick < tickLower)
    {
        // Current tick is below the position range
        // Fee growth inside = global fee growth - fee growth outside lower - fee growth outside upper
        feeGrowthInside0X128 = feeGrowthGlobal0X128 - feeGrowthOutsideLower0X128 - feeGrowthOutsideUpper0X128;
        feeGrowthInside1X128 = feeGrowthGlobal1X128 - feeGrowthOutsideLower1X128 - feeGrowthOutsideUpper1X128;
    }
    else if (currentTick >= tickUpper)
    {
        // Current tick is above the position range
        // Fee growth inside = 0 (position is not active)
        feeGrowthInside0X128 = STAmount{0};
        feeGrowthInside1X128 = STAmount{0};
    }
    else
    {
        // Current tick is inside the position range
        // Fee growth inside = fee growth outside upper - fee growth outside lower
        feeGrowthInside0X128 = feeGrowthOutsideUpper0X128 - feeGrowthOutsideLower0X128;
        feeGrowthInside1X128 = feeGrowthOutsideUpper1X128 - feeGrowthOutsideLower1X128;
    }

    // Ensure non-negative fee growth
    if (feeGrowthInside0X128 < beast::zero)
        feeGrowthInside0X128 = STAmount{0};
    if (feeGrowthInside1X128 < beast::zero)
        feeGrowthInside1X128 = STAmount{0};

    JLOG(j.debug()) << "AMMFeeCalculation: Detailed fee growth calculation - "
                    << "tick range: [" << tickLower << ", " << tickUpper << "], "
                    << "current tick: " << currentTick << ", "
                    << "fee growth inside: (" << feeGrowthInside0X128 << ", " << feeGrowthInside1X128 << ")";

    return {feeGrowthInside0X128, feeGrowthInside1X128};
}

bool
AMMFeeCalculation::validateFeeCalculationParams(
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::int32_t currentTick,
    beast::Journal const& j)
{
    // Validate tick range
    if (tickLower >= tickUpper)
    {
        JLOG(j.warn()) << "AMMFeeCalculation: Invalid tick range - lower >= upper";
        return false;
    }

    // Validate tick bounds
    if (tickLower < CONCENTRATED_LIQUIDITY_MIN_TICK || tickUpper > CONCENTRATED_LIQUIDITY_MAX_TICK)
    {
        JLOG(j.warn()) << "AMMFeeCalculation: Tick out of bounds";
        return false;
    }

    // Validate current tick
    if (currentTick < CONCENTRATED_LIQUIDITY_MIN_TICK || currentTick > CONCENTRATED_LIQUIDITY_MAX_TICK)
    {
        JLOG(j.warn()) << "AMMFeeCalculation: Current tick out of bounds";
        return false;
    }

    return true;
}

}  // namespace ripple
