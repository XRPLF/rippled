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

#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/tx/detail/AMMConcentratedCollect.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpld/app/misc/AMMUtils.h>

namespace ripple {

NotTEC
AMMConcentratedCollect::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (!ctx.rules.enabled(featureAMMConcentratedLiquidity))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Collect: invalid flags.";
        return temINVALID_FLAG;
    }

    // Validate concentrated liquidity collect parameters
    if (auto const err = validateConcentratedLiquidityCollectParams(ctx.tx, ctx.j))
        return err;

    return preflight2(ctx);
}

XRPAmount
AMMConcentratedCollect::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AMMConcentratedCollect is one owner reserve.
    return view.fees().increment;
}

TER
AMMConcentratedCollect::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const tickLower = ctx.tx[sfTickLower];
    auto const tickUpper = ctx.tx[sfTickUpper];
    auto const positionNonce = ctx.tx[sfPositionNonce];

    // Check if AMM exists for the asset pair
    auto const ammKeylet = keylet::amm(asset.issue(), asset2.issue());
    auto const ammSle = ctx.view.read(ammKeylet);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Collect: AMM not found.";
        return terNO_AMM;
    }

    // Check if position exists and is owned by the caller
    auto const positionKey = getConcentratedLiquidityPositionKey(accountID, tickLower, tickUpper, positionNonce);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = ctx.view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Collect: position not found.";
        return tecNO_ENTRY;
    }

    if (positionSle->getFieldAccount(sfAccount) != accountID)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Collect: position not owned by caller.";
        return tecNO_PERMISSION;
    }

    // Check if there are fees to collect
    auto const tokensOwed0 = positionSle->getFieldAmount(sfTokensOwed0);
    auto const tokensOwed1 = positionSle->getFieldAmount(sfTokensOwed1);
    if (tokensOwed0 <= STAmount{0} && tokensOwed1 <= STAmount{0})
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Collect: no fees to collect.";
        return tecPATH_DRY;
    }

    return tesSUCCESS;
}

TER
AMMConcentratedCollect::doApply()
{
    auto const accountID = ctx_.tx[sfAccount];
    auto const asset = ctx_.tx[sfAsset];
    auto const asset2 = ctx_.tx[sfAsset2];
    auto const tickLower = ctx_.tx[sfTickLower];
    auto const tickUpper = ctx_.tx[sfTickUpper];
    auto const positionNonce = ctx_.tx[sfPositionNonce];
    auto const amount0Max = ctx_.tx[sfAmount0Max];
    auto const amount1Max = ctx_.tx[sfAmount1Max];

    // Get AMM data
    auto const ammKeylet = keylet::amm(asset.issue(), asset2.issue());
    auto const ammSle = ctx_.view().read(ammKeylet);
    if (!ammSle)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Collect: AMM not found.";
        return terNO_AMM;
    }

    auto const ammAccountID = ammSle->getFieldAccount(sfAccount);

    // Get position data
    auto const positionKey = getConcentratedLiquidityPositionKey(accountID, tickLower, tickUpper, positionNonce);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = ctx_.view().read(positionKeylet);
    if (!positionSle)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Collect: position not found.";
        return tecNO_ENTRY;
    }

    auto const liquidity = positionSle->getFieldAmount(sfLiquidity);
    auto const feeGrowthInside0LastX128 = positionSle->getFieldAmount(sfFeeGrowthInside0LastX128);
    auto const feeGrowthInside1LastX128 = positionSle->getFieldAmount(sfFeeGrowthInside1LastX128);
    auto const tokensOwed0 = positionSle->getFieldAmount(sfTokensOwed0);
    auto const tokensOwed1 = positionSle->getFieldAmount(sfTokensOwed1);

    // Calculate current fee growth (simplified - in a real implementation this would be updated during trades)
    auto const currentFeeGrowthInside0X128 = STAmount{0};  // Would be calculated from global state
    auto const currentFeeGrowthInside1X128 = STAmount{0};  // Would be calculated from global state

    // Calculate accumulated fees
    auto const [accumulatedFees0, accumulatedFees1] = calculateAccumulatedFees(
        liquidity,
        feeGrowthInside0LastX128,
        feeGrowthInside1LastX128,
        currentFeeGrowthInside0X128,
        currentFeeGrowthInside1X128);

    // Total fees to collect
    auto const totalFees0 = tokensOwed0 + accumulatedFees0;
    auto const totalFees1 = tokensOwed1 + accumulatedFees1;

    // Determine amounts to collect (respecting maximum limits)
    auto const collectAmount0 = std::min(totalFees0, amount0Max);
    auto const collectAmount1 = std::min(totalFees1, amount1Max);

    if (collectAmount0 <= STAmount{0} && collectAmount1 <= STAmount{0})
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Collect: no fees to collect.";
        return tecPATH_DRY;
    }

    // Check if AMM has sufficient balance for fee collection using existing AMM patterns
    if (collectAmount0 > STAmount{0} || collectAmount1 > STAmount{0})
    {
        auto const expected = ammHolds(
            ctx_.view(),
            *ammSle,
            collectAmount0 > STAmount{0} ? collectAmount0.issue() : std::optional<Issue>{},
            collectAmount1 > STAmount{0} ? collectAmount1.issue() : std::optional<Issue>{},
            FreezeHandling::fhIGNORE_FREEZE,
            ctx_.j);
        if (!expected)
            return expected.error();
        
        auto const [amount0Balance, amount1Balance, lptAMMBalance] = *expected;
        
        if (collectAmount0 > amount0Balance)
        {
            JLOG(ctx_.j.debug()) << "AMM Concentrated Collect: insufficient balance for fee collection (amount0).";
            return tecAMM_BALANCE;
        }
        
        if (collectAmount1 > amount1Balance)
        {
            JLOG(ctx_.j.debug()) << "AMM Concentrated Collect: insufficient balance for fee collection (amount1).";
            return tecAMM_BALANCE;
        }
    }

    // Transfer fees from AMM to owner
    if (collectAmount0 > STAmount{0})
    {
        if (auto const ter = transfer(ctx_.view(), ammAccountID, accountID, collectAmount0, ctx_.j);
            ter != tesSUCCESS)
        {
            return ter;
        }
    }

    if (collectAmount1 > STAmount{0})
    {
        if (auto const ter = transfer(ctx_.view(), ammAccountID, accountID, collectAmount1, ctx_.j);
            ter != tesSUCCESS)
        {
            return ter;
        }
    }

    // Update position fee tracking data
    if (auto const ter = updatePositionFeeTracking(
            ctx_.view(),
            accountID,
            tickLower,
            tickUpper,
            positionNonce,
            currentFeeGrowthInside0X128,
            currentFeeGrowthInside1X128,
            ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Update tokens owed (reduce by collected amounts)
    auto const newTokensOwed0 = tokensOwed0 - collectAmount0;
    auto const newTokensOwed1 = tokensOwed1 - collectAmount1;
    
    positionSle->setFieldAmount(sfTokensOwed0, newTokensOwed0);
    positionSle->setFieldAmount(sfTokensOwed1, newTokensOwed1);

    ctx_.view().update(positionSle);

    JLOG(ctx_.j.debug()) << "AMM Concentrated Collect: collected fees for position " << positionKey;

    return tesSUCCESS;
}

NotTEC
AMMConcentratedCollect::validateConcentratedLiquidityCollectParams(
    STTx const& tx,
    beast::Journal const& j)
{
    auto const tickLower = tx[sfTickLower];
    auto const tickUpper = tx[sfTickUpper];
    auto const positionNonce = tx[sfPositionNonce];
    auto const amount0Max = tx[sfAmount0Max];
    auto const amount1Max = tx[sfAmount1Max];

    // Validate tick range
    if (!isValidTickRange(tickLower, tickUpper, 1))  // Default tick spacing
    {
        JLOG(j.debug()) << "AMM Concentrated Collect: invalid tick range.";
        return temBAD_AMM_TOKENS;
    }

    // Validate maximum amounts
    if (amount0Max < STAmount{0} || amount1Max < STAmount{0})
    {
        JLOG(j.debug()) << "AMM Concentrated Collect: invalid maximum amounts.";
        return temBAD_AMM_TOKENS;
    }

    return tesSUCCESS;
}

std::pair<STAmount, STAmount>
AMMConcentratedCollect::calculateAccumulatedFees(
    STAmount const& liquidity,
    STAmount const& feeGrowthInside0LastX128,
    STAmount const& feeGrowthInside1LastX128,
    STAmount const& feeGrowthInside0X128,
    STAmount const& feeGrowthInside1X128)
{
    // Calculate fee growth delta
    auto const feeGrowthInside0DeltaX128 = feeGrowthInside0X128 - feeGrowthInside0LastX128;
    auto const feeGrowthInside1DeltaX128 = feeGrowthInside1X128 - feeGrowthInside1LastX128;

    // Calculate accumulated fees
    auto const accumulatedFees0 = liquidity * feeGrowthInside0DeltaX128 / STAmount{1ULL << 128};
    auto const accumulatedFees1 = liquidity * feeGrowthInside1DeltaX128 / STAmount{1ULL << 128};

    return {accumulatedFees0, accumulatedFees1};
}

TER
AMMConcentratedCollect::updatePositionFeeTracking(
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
    auto const positionKey = getConcentratedLiquidityPositionKey(owner, tickLower, tickUpper, nonce);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(j.debug()) << "AMM Concentrated Collect: position not found for fee tracking update.";
        return tecNO_ENTRY;
    }

    // Update fee growth tracking
    positionSle->setFieldAmount(sfFeeGrowthInside0LastX128, feeGrowthInside0X128);
    positionSle->setFieldAmount(sfFeeGrowthInside1LastX128, feeGrowthInside1X128);

    view.update(positionSle);

    JLOG(j.debug()) << "AMM Concentrated Collect: updated fee tracking for position " << positionKey;

    return tesSUCCESS;
}

}  // namespace ripple
