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
#include <xrpld/app/tx/detail/AMMConcentratedCreate.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpld/app/ledger/Directory.h>
#include <xrpld/app/misc/AMMHelpers.h>

namespace ripple {

NotTEC
AMMConcentratedCreate::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (!ctx.rules.enabled(featureAMMConcentratedLiquidity))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Create: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    if (amount.issue() == amount2.issue())
    {
        JLOG(ctx.j.debug())
            << "AMM Concentrated Create: tokens cannot have the same currency/issuer.";
        return temBAD_AMM_TOKENS;
    }

    if (auto const err = invalidAMMAmount(amount))
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Create: invalid asset1 amount.";
        return err;
    }

    if (auto const err = invalidAMMAmount(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Create: invalid asset2 amount.";
        return err;
    }

    if (ctx.tx[sfTradingFee] > TRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Create: invalid trading fee.";
        return temBAD_FEE;
    }

    // Validate concentrated liquidity parameters
    if (auto const err = validateConcentratedLiquidityParams(ctx.tx, ctx.j))
        return err;

    return preflight2(ctx);
}

XRPAmount
AMMConcentratedCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AMMConcentratedCreate is one owner reserve.
    return view.fees().increment;
}

TER
AMMConcentratedCreate::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    // Check if AMM already exists for the token pair
    if (auto const ammKeylet = keylet::amm(amount.issue(), amount2.issue());
        ctx.view.read(ammKeylet))
    {
        JLOG(ctx.j.debug()) << "AMM Concentrated Create: ltAMM already exists.";
        return tecDUPLICATE;
    }

    // Check if account has sufficient balance using existing AMM patterns
    auto checkBalance = [&](STAmount const& amt) -> TER {
        if (isXRP(amt.issue()))
        {
            auto const accountSle = ctx.view.read(keylet::account(accountID));
            if (!accountSle || accountSle->getFieldAmount(sfBalance) < amt)
            {
                JLOG(ctx.j.debug()) << "AMM Concentrated Create: insufficient XRP balance.";
                return tecUNFUNDED_AMM;
            }
        }
        else
        {
            auto const sle = ctx.view.read(keylet::line(accountID, amt.issue()));
            if (!sle || sle->getFieldAmount(sfBalance) < amt)
            {
                JLOG(ctx.j.debug()) << "AMM Concentrated Create: insufficient IOU balance.";
                return tecUNFUNDED_AMM;
            }
            
            // Check authorization
            if (auto const ter = requireAuth(ctx.view, amt.issue(), accountID))
            {
                JLOG(ctx.j.debug()) << "AMM Concentrated Create: account not authorized for " << amt.issue();
                return ter;
            }
            
            // Check if account or currency is frozen
            if (isFrozen(ctx.view, accountID, amt.issue()))
            {
                JLOG(ctx.j.debug()) << "AMM Concentrated Create: account or currency frozen for " << amt.issue();
                return tecFROZEN;
            }
        }
        return tesSUCCESS;
    };
    
    if (auto const ter = checkBalance(amount))
        return ter;
    
    if (auto const ter = checkBalance(amount2))
        return ter;

    if (amount2.issue().currency == xrpCurrency())
    {
        if (ctx.view.read(keylet::account(accountID))->getFieldAmount(sfBalance) < amount2)
        {
            JLOG(ctx.j.debug()) << "AMM Concentrated Create: insufficient XRP balance for amount2.";
            return tecUNFUNDED_AMM;
        }
    }
    else
    {
        auto const sle = ctx.view.read(keylet::line(accountID, amount2.issue()));
        if (!sle || sle->getFieldAmount(sfBalance) < amount2)
        {
            JLOG(ctx.j.debug()) << "AMM Concentrated Create: insufficient IOU balance for amount2.";
            return tecUNFUNDED_AMM;
        }
    }

    return tesSUCCESS;
}

TER
AMMConcentratedCreate::doApply()
{
    auto const accountID = ctx_.tx[sfAccount];
    auto const amount = ctx_.tx[sfAmount];
    auto const amount2 = ctx_.tx[sfAmount2];
    auto const tradingFee = ctx_.tx[sfTradingFee];
    auto const asset = ctx_.tx[sfAsset];
    auto const asset2 = ctx_.tx[sfAsset2];
    auto const tickLower = ctx_.tx[sfTickLower];
    auto const tickUpper = ctx_.tx[sfTickUpper];
    auto const liquidity = ctx_.tx[sfLiquidity];
    auto const tickSpacing = ctx_.tx[sfTickSpacing];

    // Create AMM keylet
    auto const ammKeylet = keylet::amm(amount.issue(), amount2.issue());

    // Create AMM account
    auto const maybeAccount = createPseudoAccount(ctx_.view(), ammKeylet.key, sfAMMID);
    if (!maybeAccount)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Create: failed to create AMM account.";
        return tecINTERNAL;
    }

    auto const ammAccountID = *maybeAccount;

    // Create ltAMM ledger object with concentrated liquidity support
    auto const ammSle = std::make_shared<SLE>(ammKeylet);
    ammSle->setFieldAmount(sfAmount, amount);
    ammSle->setFieldAmount(sfAmount2, amount2);
    ammSle->setFieldU16(sfTradingFee, tradingFee);
    ammSle->setFieldIssue(sfAsset, STIssue{sfAsset, amount.issue()});
    ammSle->setFieldIssue(sfAsset2, STIssue{sfAsset2, amount2.issue()});
    ammSle->setFieldU32(sfTickSpacing, tickSpacing);
    ammSle->setFieldU32(sfCurrentTick, tickLower);  // Start at lower tick
    ammSle->setFieldU64(sfSqrtPriceX64, tickToSqrtPriceX64(tickLower));
    
    // Initialize concentrated liquidity specific fields
    ammSle->setFieldAmount(sfAggregatedLiquidity, liquidity);  // Initial liquidity
    ammSle->setFieldAmount(sfFeeGrowthGlobal0X128, STAmount{0});
    ammSle->setFieldAmount(sfFeeGrowthGlobal1X128, STAmount{0});

    // Create concentrated liquidity position
    if (auto const ter = createConcentratedLiquidityPosition(
            ctx_.view(),
            accountID,
            amount,
            amount2,
            tickLower,
            tickUpper,
            liquidity,
            0,  // First position has nonce 0
            ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Initialize ticks
    if (auto const ter = initializeTick(ctx_.view(), tickLower, liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    if (auto const ter = initializeTick(ctx_.view(), tickUpper, -liquidity, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Transfer assets to AMM account
    if (auto const ter = transfer(ctx_.view(), accountID, ammAccountID, amount, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    if (auto const ter = transfer(ctx_.view(), accountID, ammAccountID, amount2, ctx_.j);
        ter != tesSUCCESS)
    {
        return ter;
    }

    // Save AMM ledger object
    ctx_.view().insert(ammSle);

    // Add AMM to directory
    auto const ammDir = keylet::ammDir(amount.issue(), amount2.issue());
    auto const page = dirAdd(
        ctx_.view(), 
        ammDir, 
        ammKeylet.key, 
        false, 
        describeAMMDir(amount.issue(), amount2.issue()), 
        ctx_.j);
    
    if (!page)
    {
        JLOG(ctx_.j.debug()) << "AMM Concentrated Create: failed to add AMM to directory";
        return tecDIR_FULL;
    }

    return tesSUCCESS;
}

NotTEC
AMMConcentratedCreate::validateConcentratedLiquidityParams(
    STTx const& tx,
    beast::Journal const& j)
{
    auto const tickLower = tx[sfTickLower];
    auto const tickUpper = tx[sfTickUpper];
    auto const liquidity = tx[sfLiquidity];
    auto const tickSpacing = tx[sfTickSpacing];

    // Validate tick range
    if (!isValidTickRange(tickLower, tickUpper, tickSpacing))
    {
        JLOG(j.debug()) << "AMM Concentrated Create: invalid tick range.";
        return tecAMM_INVALID_TICK_RANGE;
    }

    // Validate liquidity amount
    if (liquidity <= STAmount{CONCENTRATED_LIQUIDITY_MIN_LIQUIDITY})
    {
        JLOG(j.debug()) << "AMM Concentrated Create: insufficient liquidity.";
        return tecAMM_INSUFFICIENT_LIQUIDITY;
    }

    // SECURITY: Validate fee tier and tick spacing
    if (!isValidConcentratedLiquidityFeeTier(tradingFee))
    {
        JLOG(j.debug()) << "AMM Concentrated Create: invalid fee tier: " << tradingFee;
        return temBAD_FEE;
    }
    
    // SECURITY: Validate that tick spacing matches the fee tier
    auto const expectedTickSpacing = getConcentratedLiquidityTickSpacing(tradingFee);
    if (tickSpacing != expectedTickSpacing)
    {
        JLOG(j.debug()) << "AMM Concentrated Create: tick spacing " << tickSpacing 
                        << " does not match fee tier " << tradingFee 
                        << " (expected: " << expectedTickSpacing << ")";
        return temBAD_AMM_TOKENS;
    }
    
    // SECURITY: Validate liquidity amount to prevent manipulation
    if (liquidity < STAmount{CONCENTRATED_LIQUIDITY_MIN_LIQUIDITY})
    {
        JLOG(j.debug()) << "AMM Concentrated Create: liquidity below minimum: " << liquidity;
        return temBAD_AMM_TOKENS;
    }
    
    // SECURITY: Validate tick range bounds
    if (tickLower < CONCENTRATED_LIQUIDITY_MIN_TICK || tickUpper > CONCENTRATED_LIQUIDITY_MAX_TICK)
    {
        JLOG(j.debug()) << "AMM Concentrated Create: tick range out of bounds: [" 
                        << tickLower << ", " << tickUpper << "]";
        return temBAD_AMM_TOKENS;
    }

    return tesSUCCESS;
}

TER
AMMConcentratedCreate::createConcentratedLiquidityPosition(
    ApplyView& view,
    AccountID const& owner,
    STAmount const& amount0,
    STAmount const& amount1,
    std::int32_t tickLower,
    std::int32_t tickUpper,
    STAmount const& liquidity,
    std::uint32_t nonce,
    beast::Journal const& j)
{
    // Create position key
    auto const positionKey = getConcentratedLiquidityPositionKey(owner, tickLower, tickUpper, nonce);
    auto const positionKeylet = keylet::child(positionKey);

    // Create position ledger object
    auto const positionSle = std::make_shared<SLE>(positionKeylet);
    positionSle->setFieldAccount(sfAccount, owner);
    positionSle->setFieldU32(sfTickLower, tickLower);
    positionSle->setFieldU32(sfTickUpper, tickUpper);
    positionSle->setFieldAmount(sfLiquidity, liquidity);
    positionSle->setFieldAmount(sfFeeGrowthInside0LastX128, STAmount{0});
    positionSle->setFieldAmount(sfFeeGrowthInside1LastX128, STAmount{0});
    positionSle->setFieldAmount(sfTokensOwed0, STAmount{0});
    positionSle->setFieldAmount(sfTokensOwed1, STAmount{0});
    positionSle->setFieldU32(sfPositionNonce, nonce);

    view.insert(positionSle);

    // Add position to owner's directory
    auto const ownerDir = keylet::ownerDir(owner);
    auto const page = dirAdd(
        view, 
        ownerDir, 
        positionKeylet.key, 
        false, 
        describeOwnerDir(owner), 
        j);
    
    if (!page)
    {
        JLOG(j.debug()) << "AMM Concentrated Create: failed to add position to directory";
        return tecDIR_FULL;
    }

    JLOG(j.debug()) << "AMM Concentrated Create: created position " << positionKey;

    return tesSUCCESS;
}

TER
AMMConcentratedCreate::initializeTick(
    ApplyView& view,
    std::int32_t tick,
    STAmount const& liquidityNet,
    beast::Journal const& j)
{
    // Create tick key
    auto const tickKey = getConcentratedLiquidityTickKey(tick);
    auto const tickKeylet = keylet::child(tickKey);

    // Create tick ledger object
    auto const tickSle = std::make_shared<SLE>(tickKeylet);
    tickSle->setFieldU32(sfTickLower, tick);  // Reuse field for tick index
    tickSle->setFieldAmount(sfLiquidityGross, STAmount{0});
    tickSle->setFieldAmount(sfLiquidityNet, liquidityNet);
    tickSle->setFieldAmount(sfFeeGrowthOutside0X128, STAmount{0});
    tickSle->setFieldAmount(sfFeeGrowthOutside1X128, STAmount{0});
    tickSle->setFieldU8(sfTickInitialized, true);

    view.insert(tickSle);

    JLOG(j.debug()) << "AMM Concentrated Create: initialized tick " << tick;

    return tesSUCCESS;
}

}  // namespace ripple
