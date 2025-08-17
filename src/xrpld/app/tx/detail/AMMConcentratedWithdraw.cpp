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
#include <xrpld/app/tx/detail/AMMConcentratedWithdraw.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
AMMConcentratedWithdraw::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (!ctx.rules.enabled(featureAMMConcentratedLiquidity))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Withdraw: invalid flags.";
        return temINVALID_FLAG;
    }

    // Validate concentrated liquidity withdraw parameters
    if (auto const err =
            validateConcentratedLiquidityWithdrawParams(ctx.tx, ctx.j))
        return err;

    return preflight2(ctx);
}

XRPAmount
AMMConcentratedWithdraw::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AMMConcentratedWithdraw is one owner reserve.
    return view.fees().increment;
}

TER
AMMConcentratedWithdraw::preclaim(PreclaimContext const& ctx)
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
        JLOG(ctx.j.debug()) << "AMM Concentrated Withdraw: AMM not found.";
        return terNO_AMM;
    }

    // Check if position exists and is owned by the caller
    auto const positionKey =
        getConcentratedLiquidityPositionKey(accountID, tickLower, tickUpper, 0);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = ctx.view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Withdraw: position not found.";
        return tecNO_ENTRY;
    }

    if (positionSle->getFieldAccount(sfAccount) != accountID)
    {
        JLOG(ctx.j.debug())
            << "AMM Concentrated Withdraw: position not owned by caller.";
        return tecNO_PERMISSION;
    }

    // Check if position has sufficient liquidity
    auto const currentLiquidity = positionSle->getFieldAmount(sfLiquidity);
    if (currentLiquidity < liquidity)
    {
        JLOG(ctx.j.debug())
            << "AMM Concentrated Withdraw: insufficient liquidity in position.";
        return tecUNFUNDED_AMM;
    }

    return tesSUCCESS;
}

TER
AMMConcentratedWithdraw::doApply()
{
    auto const accountID = ctx_.tx[sfAccount];
    auto const asset = ctx_.tx[sfAsset];
    auto const asset2 = ctx_.tx[sfAsset2];
    auto const tickLower = ctx_.tx[sfTickLower];
    auto const tickUpper = ctx_.tx[sfTickUpper];
    auto const liquidity = ctx_.tx[sfLiquidity];
    auto const amount0Min = ctx_.tx[sfAmount0Min];
    auto const amount1Min = ctx_.tx[sfAmount1Min];
    auto const collectFees = ctx_.tx[sfCollectFees];

    // Get AMM data
    auto const ammKeylet = keylet::amm(asset.issue(), asset2.issue());
    auto const ammSle = ctx_.view().read(ammKeylet);
    if (!ammSle)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Withdraw: AMM not found.";
        return terNO_AMM;
    }

    auto const ammAccountID = ammSle->getFieldAccount(sfAccount);
    auto const currentTick = ammSle->getFieldU32(sfCurrentTick);
    auto const sqrtPriceX64 = ammSle->getFieldU64(sfSqrtPriceX64);

    // Get position data
    auto const positionKey =
        getConcentratedLiquidityPositionKey(accountID, tickLower, tickUpper, 0);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = ctx_.view().read(positionKeylet);
    if (!positionSle)
    {
        JLOG(ctx_.j.debug())
            << "AMM Concentrated Withdraw: position not found.";
        return tecNO_ENTRY;
    }

    auto const currentLiquidity = positionSle->getFieldAmount(sfLiquidity);
    auto const tokensOwed0 = positionSle->getFieldAmount(sfTokensOwed0);
    auto const tokensOwed1 = positionSle->getFieldAmount(sfTokensOwed1);

    // Calculate amounts to return for the liquidity
    auto const sqrtPriceAX64 = tickToSqrtPriceX64(tickLower);
    auto const sqrtPriceBX64 = tickToSqrtPriceX64(tickUpper);

    auto const [amount0, amount1] = calculateReturnAmounts(
        liquidity, sqrtPriceX64, sqrtPriceAX64, sqrtPriceBX64);

    // Validate against minimum amounts
    if (amount0 < amount0Min || amount1 < amount1Min)
    {
        JLOG(ctx_.j.debug())
            << "AMM Concentrated Withdraw: amounts below minimum.";
        return tecPATH_DRY;
    }

    // Check if AMM has sufficient balance using existing AMM patterns
    auto const expected = ammHolds(
        ctx_.view(),
        *ammSle,
        amount0.issue(),
        amount1.issue(),
        FreezeHandling::fhIGNORE_FREEZE,
        ctx_.j);
    if (!expected)
        return expected.error();

    auto const [amount0Balance, amount1Balance, lptAMMBalance] = *expected;

    if (amount0 > amount0Balance)
    {
        JLOG(ctx_.j.debug())
            << "AMM Concentrated Withdraw: insufficient balance for amount0.";
        return tecAMM_BALANCE;
    }

    if (amount1 > amount1Balance)
    {
        JLOG(ctx_.j.debug())
            << "AMM Concentrated Withdraw: insufficient balance for amount1.";
        return tecAMM_BALANCE;
    }

    // SECURITY: Validate liquidity withdrawal to prevent underflow
    if (liquidity > currentLiquidity)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Withdraw: insufficient "
                                "liquidity for withdrawal.";
        return tecUNFUNDED_AMM;
    }

    // SECURITY: Ensure atomic operation - update position first
    auto const newLiquidity = currentLiquidity - liquidity;
    if (auto const ter = updateConcentratedLiquidityPosition(
            ctx_.view(),
            accountID,
            tickLower,
            tickUpper,
            0,            // nonce
            -liquidity,   // negative for withdrawal
            STAmount{0},  // fee growth (simplified for this implementation)
            STAmount{0},
            ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Update ticks
    if (auto const ter = updateTick(ctx_.view(), tickLower, -liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    if (auto const ter = updateTick(ctx_.view(), tickUpper, -liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Transfer tokens from AMM to caller
    if (auto const ter =
            transfer(ctx_.view(), ammAccountID, accountID, amount0, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    if (auto const ter =
            transfer(ctx_.view(), ammAccountID, accountID, amount1, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Collect fees if requested
    if (collectFees && (tokensOwed0 > STAmount{0} || tokensOwed1 > STAmount{0}))
    {
        if (auto const ter = collectFees(
                ctx_.view(),
                accountID,
                tickLower,
                tickUpper,
                0,  // nonce
                tokensOwed0,
                tokensOwed1,
                ammAccountID,
                ctx_.j);
            ter != tesSUCCESS)
        {
            return ter;
        }
    }

    return tesSUCCESS;
}

NotTEC
AMMConcentratedWithdraw::validateConcentratedLiquidityWithdrawParams(
    STTx const& tx,
    beast::Journal const& j)
{
    auto const tickLower = tx[sfTickLower];
    auto const tickUpper = tx[sfTickUpper];
    auto const liquidity = tx[sfLiquidity];
    auto const amount0Min = tx[sfAmount0Min];
    auto const amount1Min = tx[sfAmount1Min];

    // Validate tick range
    if (!isValidTickRange(tickLower, tickUpper, 1))  // Default tick spacing
    {
        JLOG(j.debug()) << "AMM Concentrated Withdraw: invalid tick range.";
        return temBAD_AMM_TOKENS;
    }

    // Validate liquidity amount
    if (liquidity <= STAmount{0})
    {
        JLOG(j.debug())
            << "AMM Concentrated Withdraw: invalid liquidity amount.";
        return temBAD_AMM_TOKENS;
    }

    // Validate minimum amounts
    if (amount0Min < STAmount{0} || amount1Min < STAmount{0})
    {
        JLOG(j.debug())
            << "AMM Concentrated Withdraw: invalid minimum amounts.";
        return temBAD_AMM_TOKENS;
    }

    return tesSUCCESS;
}

std::pair<STAmount, STAmount>
AMMConcentratedWithdraw::calculateReturnAmounts(
    STAmount const& liquidity,
    std::uint64_t sqrtPriceX64,
    std::uint64_t sqrtPriceAX64,
    std::uint64_t sqrtPriceBX64)
{
    return getAmountsForLiquidity(
        liquidity, sqrtPriceX64, sqrtPriceAX64, sqrtPriceBX64);
}

TER
AMMConcentratedWithdraw::updateConcentratedLiquidityPosition(
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
            << "AMM Concentrated Withdraw: position not found for update.";
        return tecNO_ENTRY;
    }

    // Update position
    auto const currentLiquidity = positionSle->getFieldAmount(sfLiquidity);
    auto const newLiquidity = currentLiquidity + liquidityDelta;

    if (newLiquidity < STAmount{0})
    {
        JLOG(j.debug())
            << "AMM Concentrated Withdraw: liquidity would become negative.";
        return tecUNFUNDED_AMM;
    }

    positionSle->setFieldAmount(sfLiquidity, newLiquidity);
    positionSle->setFieldAmount(
        sfFeeGrowthInside0LastX128, feeGrowthInside0X128);
    positionSle->setFieldAmount(
        sfFeeGrowthInside1LastX128, feeGrowthInside1X128);

    view.update(positionSle);

    JLOG(j.debug()) << "AMM Concentrated Withdraw: updated position "
                    << positionKey;

    return tesSUCCESS;
}

TER
AMMConcentratedWithdraw::updateTick(
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
            << "AMM Concentrated Withdraw: tick not found for update.";
        return tecNO_ENTRY;
    }

    // Update tick
    auto const currentLiquidityNet = tickSle->getFieldAmount(sfLiquidityNet);
    auto const newLiquidityNet = currentLiquidityNet + liquidityNet;

    tickSle->setFieldAmount(sfLiquidityNet, newLiquidityNet);

    view.update(tickSle);

    JLOG(j.debug()) << "AMM Concentrated Withdraw: updated tick " << tick;

    return tesSUCCESS;
}

TER
AMMConcentratedWithdraw::collectFees(
    ApplyView& view,
    AccountID const& owner,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    std::uint32_t nonce,
    STAmount const& tokensOwed0,
    STAmount const& tokensOwed1,
    AccountID const& ammAccountID,
    beast::Journal const& j)
{
    // Get position
    auto const positionKey =
        getConcentratedLiquidityPositionKey(owner, tickLower, tickUpper, nonce);
    auto const positionKeylet = keylet::child(positionKey);
    auto const positionSle = view.read(positionKeylet);
    if (!positionSle)
    {
        JLOG(j.debug()) << "AMM Concentrated Withdraw: position not found for "
                           "fee collection.";
        return tecNO_ENTRY;
    }

    // Check if AMM has sufficient balance for fees
    if (tokensOwed0.issue().currency == xrpCurrency())
    {
        if (view.read(keylet::account(ammAccountID))
                ->getFieldAmount(sfBalance) < tokensOwed0)
        {
            JLOG(j.debug()) << "AMM Concentrated Withdraw: insufficient XRP "
                               "for fee collection.";
            return tecUNFUNDED_AMM;
        }
    }
    else
    {
        auto const sle =
            view.read(keylet::line(ammAccountID, tokensOwed0.issue()));
        if (!sle || sle->getFieldAmount(sfBalance) < tokensOwed0)
        {
            JLOG(j.debug()) << "AMM Concentrated Withdraw: insufficient IOU "
                               "for fee collection.";
            return tecUNFUNDED_AMM;
        }
    }

    if (tokensOwed1.issue().currency == xrpCurrency())
    {
        if (view.read(keylet::account(ammAccountID))
                ->getFieldAmount(sfBalance) < tokensOwed1)
        {
            JLOG(j.debug()) << "AMM Concentrated Withdraw: insufficient XRP "
                               "for fee collection (token1).";
            return tecUNFUNDED_AMM;
        }
    }
    else
    {
        auto const sle =
            view.read(keylet::line(ammAccountID, tokensOwed1.issue()));
        if (!sle || sle->getFieldAmount(sfBalance) < tokensOwed1)
        {
            JLOG(j.debug()) << "AMM Concentrated Withdraw: insufficient IOU "
                               "for fee collection (token1).";
            return tecUNFUNDED_AMM;
        }
    }

    // Transfer fees from AMM to owner
    if (tokensOwed0 > STAmount{0})
    {
        if (auto const ter =
                transfer(view, ammAccountID, owner, tokensOwed0, j);
            ter != tesSUCCESS)
        {
            return ter;
        }
    }

    if (tokensOwed1 > STAmount{0})
    {
        if (auto const ter =
                transfer(view, ammAccountID, owner, tokensOwed1, j);
            ter != tesSUCCESS)
        {
            return ter;
        }
    }

    // Reset fee tracking
    positionSle->setFieldAmount(sfTokensOwed0, STAmount{0});
    positionSle->setFieldAmount(sfTokensOwed1, STAmount{0});

    view.update(positionSle);

    JLOG(j.debug()) << "AMM Concentrated Withdraw: collected fees for position "
                    << positionKey;

    return tesSUCCESS;
}

}  // namespace ripple
