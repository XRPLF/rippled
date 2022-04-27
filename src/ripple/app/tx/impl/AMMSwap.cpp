//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/app/tx/impl/AMMSwap.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

TxConsequences
AMMSwap::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMSwap::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMM))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    // Valid combinations are:
    //   AssetIn
    //   AssetOut
    //   AssetIn and LimitSpotPrice
    //   AssetOut and LimitSpotPrice
    //   AssetIn and Slippage
    //   AssetOut and Slippage
    auto const assetIn = ctx.tx[~sfAssetIn];
    auto const assetOut = ctx.tx[~sfAssetOut];
    auto const limitSP = ctx.tx[~sfLimitSpotPrice];
    auto const slippage = ctx.tx[~sfSlippage];
    if ((!assetIn && !assetOut) || (assetIn && assetOut) ||
        (limitSP && slippage))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (auto const res = validAmount(assetIn, limitSP.has_value()))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid AssetIn";
        return *res;
    }
    else if (auto const res = validAmount(assetOut, limitSP.has_value()))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid AssetOut";
        return *res;
    }
    else if (auto const res = validAmount(limitSP))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid LimitSpotPrice";
        return *res;
    }
    // TODO CHECK slippage

    return preflight2(ctx);
}

TER
AMMSwap::preclaim(PreclaimContext const& ctx)
{
    auto const sleAMM = getAMMSle(ctx.view, ctx.tx[sfAMMHash]);
    if (!sleAMM)
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: Invalid AMM account";
        return temBAD_SRC_ACCOUNT;
    }
    auto const assetOut = ctx.tx[~sfAssetOut];
    auto const assetIn = ctx.tx[~sfAssetIn];
    auto const [asset1, asset2, lpTokens] = getAMMBalances(
        ctx.view,
        sleAMM->getAccountID(sfAMMAccount),
        ctx.tx[sfAccount],
        assetIn ? std::optional<Issue>(assetIn->issue()) : std::nullopt,
        assetOut ? std::optional<Issue>(assetOut->issue()) : std::nullopt,
        ctx.j);
    if (asset1 <= beast::zero || asset2 <= beast::zero ||
        lpTokens <= beast::zero)
    {
        JLOG(ctx.j.error())
            << "AMM Deposit: reserves or tokens balance is zero";
        return tecAMM_BALANCE;
    }
    if (assetIn && *assetIn > asset1)
    {
        JLOG(ctx.j.error()) << "AMM Deposit: invalid swap in amount";
        return tecAMM_BALANCE;
    }
    if (assetOut && *assetOut > asset2)
    {
        JLOG(ctx.j.error()) << "AMM Deposit: invalid swap out amount";
        return tecAMM_BALANCE;
    }

    if (isFrozen(ctx.view, assetIn) || isFrozen(ctx.view, assetOut))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit involves frozen asset";
        return tecFROZEN;
    }

    return tesSUCCESS;
}

void
AMMSwap::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMSwap::applyGuts(Sandbox& sb)
{
    auto const assetIn = ctx_.tx[~sfAssetIn];
    auto const assetOut = ctx_.tx[~sfAssetOut];
    auto const limitSP = ctx_.tx[~sfLimitSpotPrice];
    auto const slippage = ctx_.tx[~sfSlippage];
    auto const sleAMM = ctx_.view().peek(keylet::amm(ctx_.tx[sfAMMHash]));
    assert(sleAMM);
    auto const ammAccountID = sleAMM->getAccountID(sfAMMAccount);
    // asset1 corresponds to assetIn and asset2 corresponds to assetOut
    auto const [asset1, asset2, lptAMMBalance] = getAMMBalances(
        sb,
        ammAccountID,
        std::nullopt,
        assetIn ? std::optional<Issue>(assetIn->issue()) : std::nullopt,
        assetOut ? std::optional<Issue>(assetOut->issue()) : std::nullopt,
        ctx_.journal);
    // lpAsset1, lpAsset2 ordered same as above
    auto const [lpAsset1, lpAsset2, lpTokens] = getAMMBalances(
        sb,
        ammAccountID,
        account_,
        assetIn ? std::optional<Issue>(assetIn->issue()) : std::nullopt,
        assetOut ? std::optional<Issue>(assetOut->issue()) : std::nullopt,
        ctx_.journal);

    auto const tfee = sleAMM->getFieldU32(sfTradingFee);
    auto const weight1 = orderWeight(
        sleAMM->getFieldU8(sfAssetWeight), asset1.issue(), asset2.issue());

    TER result = tesSUCCESS;

    if (assetIn)
    {
        if (limitSP)
            result = swapInLimitSP(
                sb,
                ammAccountID,
                asset1,
                asset2,
                *assetIn,
                lpAsset2,
                *limitSP,
                weight1,
                tfee);
        else if (slippage)
            result = swapInSlippage(
                sb,
                ammAccountID,
                asset1,
                asset2,
                *assetIn,
                lpAsset2,
                *slippage,
                weight1,
                tfee);
        else
            result = swapIn(
                sb,
                ammAccountID,
                asset1,
                asset2,
                *assetIn,
                lpAsset2,
                weight1,
                tfee);
    }
    else if (assetOut)
    {
        if (limitSP)
            result = swapOutLimitSP(
                sb,
                ammAccountID,
                asset1,
                asset2,
                *assetOut,
                lpAsset2,
                *limitSP,
                weight1,
                tfee);
        else if (slippage)
            result = swapOutSlippage(
                sb,
                ammAccountID,
                asset1,
                asset2,
                *assetOut,
                lpAsset2,
                *slippage,
                weight1,
                tfee);
        else
            result = swapOut(
                sb,
                ammAccountID,
                asset1,
                asset2,
                *assetOut,
                lpAsset2,
                weight1,
                tfee);
    }

    return {result, result == tesSUCCESS};
}

TER
AMMSwap::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

TER
AMMSwap::swapAssets(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& assetIn,
    STAmount const& assetOut,
    STAmount const& lpAsset2)
{
    if (assetOut > lpAsset2)
        return tecAMM_BALANCE;

    auto res = accountSend(view, account_, ammAccount, assetIn, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug()) << "AMM Swap: failed to swap in " << assetIn;
        return res;
    }

    res = accountSend(view, ammAccount, account_, assetOut, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Swap: failed to swap out " << assetOut;
        return res;
    }

    return tesSUCCESS;
}

TER
AMMSwap::swapIn(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetIn,
    STAmount const& lpAsset2,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const assetOut =
        swapAssetIn(asset1Balance, asset2Balance, assetIn, weight1, tfee);
    return swapAssets(view, ammAccount, assetIn, assetOut, lpAsset2);
}

TER
AMMSwap::swapOut(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetOut,
    STAmount const& lpAsset2,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const assetIn = swapAssetOut(
        asset2Balance, asset1Balance, assetOut, 100 - weight1, tfee);
    return swapAssets(view, ammAccount, assetIn, assetOut, lpAsset2);
}

TER
AMMSwap::swapInLimitSP(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetIn,
    STAmount const& lpAsset2,
    STAmount const& limitSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const assetInDeposit =
        changeSpotPrice(asset1Balance, asset2Balance, limitSP, weight1, tfee);
    if (!assetInDeposit)
        return tecAMM_FAILED_SWAP;
    if (assetIn == beast::zero || *assetInDeposit <= assetIn)
        return swapIn(
            view,
            ammAccount,
            asset1Balance,
            asset2Balance,
            *assetInDeposit,
            lpAsset2,
            weight1,
            tfee);
    return swapIn(
        view,
        ammAccount,
        asset1Balance,
        asset2Balance,
        assetIn,
        lpAsset2,
        weight1,
        tfee);
}

TER
AMMSwap::swapOutLimitSP(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetOut,
    STAmount const& lpAsset2,
    STAmount const& limitSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const assetOutDeposit =
        changeSpotPrice(asset2Balance, asset1Balance, limitSP, weight1, tfee);
    if (!assetOutDeposit)
        return tecAMM_FAILED_SWAP;
    if (assetOut == beast::zero || assetOutDeposit >= assetOut)
        return swapOut(
            view,
            ammAccount,
            asset1Balance,
            asset2Balance,
            lpAsset2,
            *assetOutDeposit,
            weight1,
            tfee);
    return tecAMM_FAILED_SWAP;
}

TER
AMMSwap::swapInSlippage(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetIn,
    STAmount const& lpAsset2,
    std::uint16_t slippage,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const slippageSlope =
        averageSlippageIn(asset1Balance, assetIn, weight1, tfee);
    if (assetIn * slippageSlope <= slippage)
        return swapIn(
            view,
            ammAccount,
            asset1Balance,
            asset2Balance,
            assetIn,
            lpAsset2,
            weight1,
            tfee);

    auto const assetInUpd =
        toSTAmount(assetIn.issue(), slippage / slippageSlope);
    return swapIn(
        view,
        ammAccount,
        asset1Balance,
        asset2Balance,
        assetInUpd,
        lpAsset2,
        weight1,
        tfee);
}

TER
AMMSwap::swapOutSlippage(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& assetOut,
    STAmount const& lpAsset2,
    std::uint16_t slippage,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const slippageSlope =
        averageSlippageOut(asset2Balance, assetOut, weight1, tfee);
    if (assetOut * slippageSlope <= slippage)
        return swapOut(
            view,
            ammAccount,
            asset1Balance,
            asset2Balance,
            assetOut,
            lpAsset2,
            weight1,
            tfee);

    auto const assetOutUpd =
        toSTAmount(assetOut.issue(), slippage / slippageSlope);
    return swapOut(
        view,
        ammAccount,
        asset1Balance,
        asset2Balance,
        assetOutUpd,
        lpAsset2,
        weight1,
        tfee);
}

}  // namespace ripple
