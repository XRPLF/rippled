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

#include <xrpld/app/tx/detail/AMMConcentratedDeposit.h>
#include <xrpld/app/ledger/Directory.h>
#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TER.h>
#include <xrpld/app/misc/AMMFeeCalculation.h>

// Helper function declarations
namespace {
std::pair<STAmount, STAmount>
calculateFeeGrowthInside(
    ReadView const& view,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    STAmount const& feeGrowthGlobal0X128,
    STAmount const& feeGrowthGlobal1X128,
    beast::Journal const& j);
}

namespace ripple {

NotTEC
AMMConcentratedDeposit::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (!ctx.rules.enabled(featureAMMConcentratedLiquidity))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: invalid flags.";
        return temINVALID_FLAG;
    }

    // Validate concentrated liquidity deposit parameters
    if (auto const err =
            validateConcentratedLiquidityDepositParams(ctx.tx, ctx.j))
        return err;

    return preflight2(ctx);
}

XRPAmount
AMMConcentratedDeposit::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AMMConcentratedDeposit is one owner reserve.
    return view.fees().increment;
}

TER
AMMConcentratedDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const tickLower = ctx.tx[sfTickLower];
    auto const tickUpper = ctx.tx[sfTickUpper];
    auto const liquidity = ctx.tx[sfLiquidity];
    auto const amount0Max = ctx.tx[sfAmount0Max];
    auto const amount1Max = ctx.tx[sfAmount1Max];

    // Check if AMM exists
    auto const ammKeylet = keylet::amm(asset.issue(), asset2.issue());
    auto const ammSle = ctx.view.read(ammKeylet);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: AMM not found.";
        return terNO_AMM;
    }

    // Verify AMM has concentrated liquidity support
    if (!ammSle->isFieldPresent(sfCurrentTick))
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: AMM does not support concentrated liquidity.";
        return terNO_AMM;
    }

    // Check if position exists
    auto const positionKey = getConcentratedLiquidityPositionKey(
        accountID, tickLower, tickUpper, 0);  // Assuming nonce 0 for now
    auto const positionSle = ctx.view.read(keylet::unchecked(positionKey));
    if (!positionSle)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Position not found.";
        return tecAMM_POSITION_NOT_FOUND;
    }

    // Verify position ownership
    if (positionSle->getAccountID(sfOwner) != accountID)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Position not owned by account.";
        return tecNO_PERMISSION;
    }

    // Check account balance for maximum amounts
    if (auto const ter = requireAuth(ctx.view, amount0Max.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Account not authorized for asset0.";
        return ter;
    }

    if (auto const ter = requireAuth(ctx.view, amount1Max.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Account not authorized for asset1.";
        return ter;
    }

    // Check if account has sufficient balance
    if (isXRP(amount0Max.issue()))
    {
        auto const accountSle = ctx.view.read(keylet::account(accountID));
        if (!accountSle || accountSle->getFieldAmount(sfBalance) < amount0Max)
        {
            JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Insufficient XRP balance.";
            return tecUNFUNDED;
        }
    }
    else
    {
        auto const lineSle = ctx.view.read(keylet::line(accountID, amount0Max.issue()));
        if (!lineSle || lineSle->getFieldAmount(sfBalance) < amount0Max)
        {
            JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Insufficient asset0 balance.";
            return tecUNFUNDED;
        }
    }

    if (isXRP(amount1Max.issue()))
    {
        auto const accountSle = ctx.view.read(keylet::account(accountID));
        if (!accountSle || accountSle->getFieldAmount(sfBalance) < amount1Max)
        {
            JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Insufficient XRP balance.";
            return tecUNFUNDED;
        }
    }
    else
    {
        auto const lineSle = ctx.view.read(keylet::line(accountID, amount1Max.issue()));
        if (!lineSle || lineSle->getFieldAmount(sfBalance) < amount1Max)
        {
            JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: Insufficient asset1 balance.";
            return tecUNFUNDED;
        }
    }

    return tesSUCCESS;
}

TER
AMMConcentratedDeposit::doApply()
{
    auto const accountID = ctx_.tx[sfAccount];
    auto const asset = ctx_.tx[sfAsset];
    auto const asset2 = ctx_.tx[sfAsset2];
    auto const tickLower = ctx_.tx[sfTickLower];
    auto const tickUpper = ctx_.tx[sfTickUpper];
    auto const liquidity = ctx_.tx[sfLiquidity];
    auto const amount0Max = ctx_.tx[sfAmount0Max];
    auto const amount1Max = ctx_.tx[sfAmount1Max];
    auto const liquidityMin = ctx_.tx[sfLiquidityMin];

    // Get AMM data
    auto const ammKeylet = keylet::amm(asset.issue(), asset2.issue());
    auto const ammSle = ctx_.view().read(ammKeylet);
    if (!ammSle)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Deposit: AMM not found.";
        return terNO_AMM;
    }

    auto const ammAccountID = ammSle->getFieldAccount(sfAccount);
    auto const currentTick = ammSle->getFieldU32(sfCurrentTick);
    auto const sqrtPriceX64 = ammSle->getFieldU64(sfSqrtPriceX64);

    // Calculate optimal amounts for the liquidity
    auto const sqrtPriceAX64 = tickToSqrtPriceX64(tickLower);
    auto const sqrtPriceBX64 = tickToSqrtPriceX64(tickUpper);

    auto const [amount0, amount1] = calculateOptimalAmounts(
        liquidity, sqrtPriceX64, sqrtPriceAX64, sqrtPriceBX64);

    // Validate against maximum amounts
    if (amount0 > amount0Max || amount1 > amount1Max)
    {
        JLOG(ctx_.j.debug())
            << "AMM Concentrated Deposit: amounts exceed maximum.";
        return tecPATH_DRY;
    }

    // Validate minimum liquidity
    if (liquidity < liquidityMin)
    {
        JLOG(ctx_.j.debug())
            << "AMM Concentrated Deposit: insufficient liquidity.";
        return tecPATH_DRY;
    }

    // Transfer assets from account to AMM
    if (auto const ter = accountSend(
            ctx_.view(), accountID, ammAccountID, amount0, ctx_.j);
        ter != tesSUCCESS)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Deposit: failed to transfer asset0.";
        return ter;
    }

    if (auto const ter = accountSend(
            ctx_.view(), accountID, ammAccountID, amount1, ctx_.j);
        ter != tesSUCCESS)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Deposit: failed to transfer asset1.";
        return ter;
    }

    // Get AMM ID for fee calculation
    auto const ammID = ammSle->getFieldH256(sfAMMID);
    
    // Calculate current fee growth inside the position's range
    auto const [feeGrowthInside0X128, feeGrowthInside1X128] = 
        AMMFeeCalculation::calculateFeeGrowthInside(
            ctx_.view(), ammID, tickLower, tickUpper, currentTick,
            ammSle->getFieldAmount(sfFeeGrowthGlobal0X128),
            ammSle->getFieldAmount(sfFeeGrowthGlobal1X128),
            ctx_.j);

    // Update position
    if (auto const ter = updateConcentratedLiquidityPosition(
            ctx_.view(),
            accountID,
            tickLower,
            tickUpper,
            0,  // Assuming nonce 0
            liquidity,
            feeGrowthInside0X128,
            feeGrowthInside1X128,
            ctx_.j);
        ter != tesSUCCESS)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Deposit: failed to update position.";
        return ter;
    }

    // Update ticks
    if (auto const ter = updateTick(ctx_.view(), tickLower, liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Deposit: failed to update lower tick.";
        return ter;
    }

    if (auto const ter = updateTick(ctx_.view(), tickUpper, liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Deposit: failed to update upper tick.";
        return ter;
    }

    return tesSUCCESS;
}

NotTEC
AMMConcentratedDeposit::validateConcentratedLiquidityDepositParams(
    STTx const& tx,
    beast::Journal const& j)
{
    auto const asset = tx[sfAsset];
    auto const asset2 = tx[sfAsset2];
    auto const tickLower = tx[sfTickLower];
    auto const tickUpper = tx[sfTickUpper];
    auto const liquidity = tx[sfLiquidity];
    auto const amount0Max = tx[sfAmount0Max];
    auto const amount1Max = tx[sfAmount1Max];

    // Validate asset pair
    if (asset.issue() == asset2.issue())
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: same asset pair.";
        return temBAD_AMM_TOKENS;
    }

    // Validate tick range
    if (tickLower >= tickUpper)
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: invalid tick range.";
        return temBAD_AMM_TOKENS;
    }

    // Validate tick bounds
    if (tickLower < CONCENTRATED_LIQUIDITY_MIN_TICK ||
        tickUpper > CONCENTRATED_LIQUIDITY_MAX_TICK)
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: tick out of bounds.";
        return temBAD_AMM_TOKENS;
    }

    // Validate liquidity amount
    if (liquidity <= beast::zero)
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: invalid liquidity amount.";
        return temBAD_AMOUNT;
    }

    // Validate maximum amounts
    if (amount0Max <= beast::zero || amount1Max <= beast::zero)
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: invalid maximum amounts.";
        return temBAD_AMOUNT;
    }

    return tesSUCCESS;
}

std::pair<STAmount, STAmount>
AMMConcentratedDeposit::calculateOptimalAmounts(
    STAmount const& liquidity,
    std::uint64_t sqrtPriceX64,
    std::uint64_t sqrtPriceAX64,
    std::uint64_t sqrtPriceBX64)
{
    // Calculate optimal amounts using concentrated liquidity formulas
    // This is a simplified implementation - in practice, you'd want more precision
    
    // Convert liquidity to a numeric value for calculations
    auto const liquidityValue = liquidity.getText();
    double liquidityDouble = std::stod(liquidityValue);
    
    // Convert sqrt prices from Q64.64 to double
    double const sqrtPrice = static_cast<double>(sqrtPriceX64) / (1ULL << 64);
    double const sqrtPriceA = static_cast<double>(sqrtPriceAX64) / (1ULL << 64);
    double const sqrtPriceB = static_cast<double>(sqrtPriceBX64) / (1ULL << 64);
    
    // Calculate amounts using concentrated liquidity formulas
    double amount0 = 0.0;
    double amount1 = 0.0;
    
    if (sqrtPrice <= sqrtPriceA)
    {
        // Price is below range - only asset0 needed
        amount0 = liquidityDouble * (sqrtPriceB - sqrtPriceA) / (sqrtPriceA * sqrtPriceB);
    }
    else if (sqrtPrice >= sqrtPriceB)
    {
        // Price is above range - only asset1 needed
        amount1 = liquidityDouble * (sqrtPriceB - sqrtPriceA);
    }
    else
    {
        // Price is within range - both assets needed
        amount0 = liquidityDouble * (sqrtPriceB - sqrtPrice) / (sqrtPrice * sqrtPriceB);
        amount1 = liquidityDouble * (sqrtPrice - sqrtPriceA);
    }
    
    // Convert back to STAmount (simplified)
    return {STAmount{static_cast<std::int64_t>(amount0)}, STAmount{static_cast<std::int64_t>(amount1)}};
}

TER
AMMConcentratedDeposit::updateConcentratedLiquidityPosition(
    ApplyView& view,
    AccountID const& owner,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::uint32_t nonce,
    STAmount const& liquidityDelta,
    STAmount const& feeGrowthInside0X128,
    STAmount const& feeGrowthInside1X128,
    beast::Journal const& j)
{
    // Get position keylet
    auto const positionKey = getConcentratedLiquidityPositionKey(
        owner, tickLower, tickUpper, nonce);
    auto const positionKeylet = keylet::unchecked(positionKey);
    
    // Get current position
    auto const positionSle = view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: Position not found for update.";
        return tecAMM_POSITION_NOT_FOUND;
    }
    
    // Get current liquidity
    auto const currentLiquidity = positionSle->getFieldAmount(sfLiquidity);
    auto const newLiquidity = currentLiquidity + liquidityDelta;
    
    // Update position
    auto const newPositionSle = std::make_shared<SLE>(*positionSle);
    newPositionSle->setFieldAmount(sfLiquidity, newLiquidity);
    newPositionSle->setFieldAmount(sfFeeGrowthInside0LastX128, feeGrowthInside0X128);
    newPositionSle->setFieldAmount(sfFeeGrowthInside1LastX128, feeGrowthInside1X128);
    
    view.update(newPositionSle);
    
    return tesSUCCESS;
}

TER
AMMConcentratedDeposit::updateTick(
    ApplyView& view,
    std::int32_t tick,
    STAmount const& liquidityNet,
    beast::Journal const& j)
{
    // Get tick keylet
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickKeylet = keylet::unchecked(tickKey);
    
    // Get current tick
    auto const tickSle = view.read(tickKeylet);
    if (!tickSle)
    {
        // Create new tick
        auto const newTickSle = std::make_shared<SLE>(tickKeylet);
        newTickSle->setFieldU32(sfTickIndex, tick);
        newTickSle->setFieldAmount(sfLiquidityGross, liquidityNet);
        newTickSle->setFieldAmount(sfLiquidityNet, liquidityNet);
        newTickSle->setFieldAmount(sfFeeGrowthOutside0X128, STAmount{0});
        newTickSle->setFieldAmount(sfFeeGrowthOutside1X128, STAmount{0});
        newTickSle->setFieldU8(sfTickInitialized, 1);
        
        view.insert(newTickSle);
    }
    else
    {
        // Update existing tick
        auto const currentLiquidityGross = tickSle->getFieldAmount(sfLiquidityGross);
        auto const currentLiquidityNet = tickSle->getFieldAmount(sfLiquidityNet);
        
        auto const newLiquidityGross = currentLiquidityGross + liquidityNet;
        auto const newLiquidityNet = currentLiquidityNet + liquidityNet;
        
        auto const newTickSle = std::make_shared<SLE>(*tickSle);
        newTickSle->setFieldAmount(sfLiquidityGross, newLiquidityGross);
        newTickSle->setFieldAmount(sfLiquidityNet, newLiquidityNet);
        
        view.update(newTickSle);
    }
    
    return tesSUCCESS;
}

// Helper function implementations
namespace {

std::pair<STAmount, STAmount>
calculateFeeGrowthInside(
    ReadView const& view,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    STAmount const& feeGrowthGlobal0X128,
    STAmount const& feeGrowthGlobal1X128,
    beast::Journal const& j)
{
    // Use the sophisticated fee calculation implementation
    // Get AMM ID from the view (simplified - in practice you'd get this from context)
    uint256 ammID; // This would be passed in or derived from context
    
    // Use the sophisticated fee calculation
    return AMMFeeCalculation::calculateFeeGrowthInside(
        view, ammID, tickLower, tickUpper, 0, // currentTick would be passed in
        feeGrowthGlobal0X128, feeGrowthGlobal1X128, j);
}

}  // namespace

}  // namespace ripple
