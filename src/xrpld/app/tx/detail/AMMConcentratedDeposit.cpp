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
#include <xrpld/app/tx/detail/AMMConcentratedDeposit.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>

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

    // Check if AMM exists for the asset pair
    auto const ammKeylet = keylet::amm(asset.issue(), asset2.issue());
    auto const ammSle = ctx.view.read(ammKeylet);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: AMM not found.";
        return terNO_AMM;
    }

    // Check if position exists and is owned by the caller
    auto const positionKey =
        getConcentratedLiquidityPositionKey(accountID, tickLower, tickUpper, 0);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = ctx.view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Deposit: position not found.";
        return tecNO_ENTRY;
    }

    if (positionSle->getFieldAccount(sfAccount) != accountID)
    {
        JLOG(ctx.j.debug())
            << "AMM Concentrated Deposit: position not owned by caller.";
        return tecNO_PERMISSION;
    }

    // Check if account has sufficient balance for calculated amounts
    // (This will be validated more precisely in doApply after calculation)

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

    // Check if account has sufficient balance using existing AMM patterns
    auto checkBalance = [&](STAmount const& amt) -> TER {
        if (isXRP(amt.issue()))
        {
            auto const accountSle =
                ctx_.view().read(keylet::account(accountID));
            if (!accountSle || accountSle->getFieldAmount(sfBalance) < amt)
            {
                JLOG(ctx_.j.debug())
                    << "AMM Concentrated Deposit: insufficient XRP balance.";
                return tecUNFUNDED_AMM;
            }
        }
        else
        {
            auto const sle =
                ctx_.view().read(keylet::line(accountID, amt.issue()));
            if (!sle || sle->getFieldAmount(sfBalance) < amt)
            {
                JLOG(ctx_.j.debug())
                    << "AMM Concentrated Deposit: insufficient IOU balance.";
                return tecUNFUNDED_AMM;
            }

            // Check authorization using existing AMM patterns
            if (auto const ter =
                    requireAuth(ctx_.view(), amt.issue(), accountID))
            {
                JLOG(ctx_.j.debug())
                    << "AMM Concentrated Deposit: account not authorized for "
                    << amt.issue();
                return ter;
            }

            // Check if account or currency is frozen using existing AMM
            // patterns
            if (isFrozen(ctx_.view(), accountID, amt.issue()))
            {
                JLOG(ctx_.j.debug()) << "AMM Concentrated Deposit: account or "
                                        "currency frozen for "
                                     << amt.issue();
                return tecFROZEN;
            }
        }
        return tesSUCCESS;
    };

    if (auto const ter = checkBalance(amount0))
        return ter;

    if (auto const ter = checkBalance(amount1))
        return ter;

    // Update position
    if (auto const ter = updateConcentratedLiquidityPosition(
            ctx_.view(),
            accountID,
            tickLower,
            tickUpper,
            0,  // nonce
            liquidity,
            STAmount{0},  // fee growth (simplified for this implementation)
            STAmount{0},
            ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Update ticks
    if (auto const ter = updateTick(ctx_.view(), tickLower, liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    if (auto const ter = updateTick(ctx_.view(), tickUpper, liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Transfer tokens to AMM account
    if (auto const ter =
            transfer(ctx_.view(), accountID, ammAccountID, amount0, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    if (auto const ter =
            transfer(ctx_.view(), accountID, ammAccountID, amount1, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    return tesSUCCESS;
}

NotTEC
AMMConcentratedDeposit::validateConcentratedLiquidityDepositParams(
    STTx const& tx,
    beast::Journal const& j)
{
    auto const tickLower = tx[sfTickLower];
    auto const tickUpper = tx[sfTickUpper];
    auto const liquidity = tx[sfLiquidity];
    auto const amount0Max = tx[sfAmount0Max];
    auto const amount1Max = tx[sfAmount1Max];
    auto const liquidityMin = tx[sfLiquidityMin];

    // Validate tick range
    if (!isValidTickRange(tickLower, tickUpper, 1))  // Default tick spacing
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: invalid tick range.";
        return temBAD_AMM_TOKENS;
    }

    // Validate that ticks are valid for the AMM's fee tier
    auto const ammSle =
        ctx.view.read(keylet::amm(asset.issue(), asset2.issue()));
    if (ammSle)
    {
        auto const tradingFee = ammSle->getFieldU16(sfTradingFee);
        if (!isValidTickForFeeTier(tickLower, tradingFee) ||
            !isValidTickForFeeTier(tickUpper, tradingFee))
        {
            JLOG(j.debug())
                << "AMM Concentrated Deposit: ticks not valid for fee tier "
                << tradingFee;
            return temBAD_AMM_TOKENS;
        }
    }

    // Validate liquidity amount
    if (liquidity <= STAmount{0})
    {
        JLOG(j.debug())
            << "AMM Concentrated Deposit: invalid liquidity amount.";
        return temBAD_AMM_TOKENS;
    }

    // Validate maximum amounts
    if (amount0Max <= STAmount{0} || amount1Max <= STAmount{0})
    {
        JLOG(j.debug()) << "AMM Concentrated Deposit: invalid maximum amounts.";
        return temBAD_AMM_TOKENS;
    }

    // Validate minimum liquidity
    if (liquidityMin > liquidity)
    {
        JLOG(j.debug())
            << "AMM Concentrated Deposit: minimum liquidity too high.";
        return temBAD_AMM_TOKENS;
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
    return getAmountsForLiquidity(
        liquidity, sqrtPriceX64, sqrtPriceAX64, sqrtPriceBX64);
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
    // Get position
    auto const positionKey =
        getConcentratedLiquidityPositionKey(owner, tickLower, tickUpper, nonce);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(j.debug())
            << "AMM Concentrated Deposit: position not found for update.";
        return tecNO_ENTRY;
    }

    // Update position
    auto const currentLiquidity = positionSle->getFieldAmount(sfLiquidity);
    auto const newLiquidity = currentLiquidity + liquidityDelta;

    positionSle->setFieldAmount(sfLiquidity, newLiquidity);
    positionSle->setFieldAmount(
        sfFeeGrowthInside0LastX128, feeGrowthInside0X128);
    positionSle->setFieldAmount(
        sfFeeGrowthInside1LastX128, feeGrowthInside1X128);

    view.update(positionSle);

    JLOG(j.debug()) << "AMM Concentrated Deposit: updated position "
                    << positionKey;

    return tesSUCCESS;
}

TER
AMMConcentratedDeposit::updateTick(
    ApplyView& view,
    std::int32_t tick,
    STAmount const& liquidityNet,
    beast::Journal const& j)
{
    // Get tick
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickKeylet = keylet::child(tickKey);
    auto const tickSle = view.read(tickKeylet);
    if (!tickSle)
    {
        JLOG(j.debug())
            << "AMM Concentrated Deposit: tick not found for update.";
        return tecNO_ENTRY;
    }

    // Update tick
    auto const currentLiquidityNet = tickSle->getFieldAmount(sfLiquidityNet);
    auto const newLiquidityNet = currentLiquidityNet + liquidityNet;

    tickSle->setFieldAmount(sfLiquidityNet, newLiquidityNet);

    view.update(tickSle);

    JLOG(j.debug()) << "AMM Concentrated Deposit: updated tick " << tick;

    return tesSUCCESS;
}

}  // namespace ripple
