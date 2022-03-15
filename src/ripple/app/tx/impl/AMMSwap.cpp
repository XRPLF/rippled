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
    //   AssetIn and MaxSP
    //   AssetOut and MaxSP
    //   AssetIn and Slippage
    //   AssetIn and MaxSP and Slippage
    auto const assetIn = ctx.tx[~sfAssetIn];
    auto const assetOut = ctx.tx[~sfAssetOut];
    auto const maxSP = ctx.tx[~sfMaxSP];
    auto const slippage = ctx.tx[~sfSlippage];
    if ((!assetIn && !assetOut) || (assetIn && assetOut) ||
        (assetOut && slippage))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "swap fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (auto const res = validAmount(assetIn, maxSP.has_value()))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid AssetIn";
        return *res;
    }
    else if (auto const res = validAmount(assetOut, maxSP.has_value()))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid AssetOut";
        return *res;
    }
    else if (auto const res = validAmount(maxSP))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid MaxSP";
        return *res;
    }
    // TODO CHECK slippage

    return preflight2(ctx);
}

TER
AMMSwap::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.read(keylet::account(ctx.tx[sfAMMAccount])))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit: Invalid AMM account";
        return temBAD_SRC_ACCOUNT;
    }
    auto const assetOut = ctx.tx[~sfAssetOut];
    auto const [asset1, asset2, lpTokens] = getAMMBalances(
        ctx.view,
        ctx.tx[sfAMMAccount],
        ctx.tx[sfAccount],
        assetOut ? std::optional<Issue>(assetOut->issue()) : std::nullopt,
        std::nullopt,
        ctx.j);
    if (asset1 <= beast::zero || asset2 <= beast::zero ||
        lpTokens <= beast::zero)
    {
        JLOG(ctx.j.error())
            << "AMM Deposit: reserves or tokens balance is zero";
        return tecAMM_BALANCE;
    }
    if (assetOut && *assetOut > asset1)
    {
        JLOG(ctx.j.error()) << "AMM Deposit: invalid swap out amount";
        return tecAMM_BALANCE;
    }

    if (isFrozen(ctx.view, ctx.tx[~sfAssetIn]) ||
        isFrozen(ctx.view, ctx.tx[~sfAssetOut]))
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
    // TODO if slippage and maxSP then there should be another var sfAsset which
    // could be either in or out
    auto const assetIn = ctx_.tx[~sfAssetIn];
    auto const assetOut = ctx_.tx[~sfAssetOut];
    auto const maxSP = ctx_.tx[~sfMaxSP];
    auto const slippage = ctx_.tx[~sfSlippage];
    auto const ammAccount = ctx_.tx[sfAMMAccount];
    auto const issue = [&]() -> std::optional<Issue> {
        if (assetIn)
            return assetIn->issue();
        else if (assetOut)
            return assetOut->issue();
        return std::nullopt;
    }();
    // asset1, asset2 are ordered by issue; i.e. asset1.issue == issue
    // if assetIn is provided then asset1 corresponds to assetIn, otherwise
    // assetOut
    auto const [asset1, asset2, lptAMMBalance] = getAMMBalances(
        sb, ammAccount, std::nullopt, issue, std::nullopt, ctx_.journal);
    // lpAsset1, lpAsset2 ordered same as above
    auto const [lpAsset1, lpAsset2, lpTokens] = getAMMBalances(
        sb, ammAccount, account_, issue, std::nullopt, ctx_.journal);

    auto const sle = sb.read(keylet::account(ctx_.tx[sfAMMAccount]));
    assert(sle);
    auto const tfee = sle->getFieldU32(sfTradingFee);
    auto const weight = sle->getFieldU8(sfAssetWeight);

    TER result = tesSUCCESS;

    if (assetIn)
    {
        if (maxSP && slippage)
            result = swapSlippageMaxSP(
                sb,
                ammAccount,
                asset1,
                asset2,
                lpAsset1,
                lpAsset2,
                *assetIn,
                *slippage,
                *maxSP,
                weight,
                tfee);
        else if (maxSP)
            result = swapInMaxSP(
                sb,
                ammAccount,
                asset1,
                asset2,
                lpAsset2,
                assetIn->issue(),
                *maxSP,
                weight,
                tfee);
        else if (slippage)
            result = swapInSlippage(
                sb,
                ammAccount,
                asset1,
                asset2,
                lpAsset2,
                *assetIn,
                *slippage,
                weight,
                tfee);
        else
            result = swapIn(
                sb,
                ammAccount,
                asset1,
                asset2,
                lpAsset2,
                *assetIn,
                weight,
                tfee);
    }
    else if (assetOut)
    {
        if (maxSP)
            result = swapOutMaxSP(
                sb,
                ammAccount,
                asset1,
                asset2,
                lpAsset1,
                assetOut->issue(),
                *maxSP,
                weight,
                tfee);
        else
            result = swapOut(
                sb,
                ammAccount,
                asset1,
                asset2,
                lpAsset1,
                *assetOut,
                weight,
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
    STAmount const& lpAsset2,
    STAmount const& assetIn,
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
    STAmount const& lpAsset2,
    STAmount const& assetOut,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const assetIn =
        swapAssetOut(asset1Balance, asset2Balance, assetOut, weight1, tfee);
    return swapAssets(view, ammAccount, assetIn, assetOut, lpAsset2);
}

TER
AMMSwap::swapInMaxSP(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lpAsset2,
    Issue const& assetInIssue,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const assetIn =
        changeSpotPrice(asset1Balance, asset2Balance, maxSP, weight1, tfee);
    if (!assetIn)
        return tecAMM_FAILED_SWAP;
    return swapIn(
        view,
        ammAccount,
        asset1Balance,
        asset2Balance,
        lpAsset2,
        *assetIn,
        weight1,
        tfee);
}

TER
AMMSwap::swapOutMaxSP(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lpAsset2,
    Issue const& assetOutIssue,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const assetOut =
        changeSpotPrice(asset1Balance, asset2Balance, maxSP, weight1, tfee);
    if (!assetOut)
        return tecAMM_FAILED_SWAP;
    return swapOut(
        view,
        ammAccount,
        asset1Balance,
        asset2Balance,
        lpAsset2,
        *assetOut,
        weight1,
        tfee);
}

TER
AMMSwap::swapInSlippage(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lpAsset,
    STAmount const& assetIn,
    std::uint16_t slippage,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    STAmount const saSlippage{noIssue(), slippage, -3};
    auto const ss = slippageSlope(asset1Balance, weight1, tfee);
    auto const assetInUpd = [&] {
        if (auto const s = multiply(assetIn, ss, noIssue()); s <= saSlippage)
            return assetIn;
        return divide(saSlippage, ss, assetIn.issue());
    }();
    return swapIn(
        view,
        ammAccount,
        asset1Balance,
        asset2Balance,
        lpAsset,
        assetInUpd,
        weight1,
        tfee);
}

TER
AMMSwap::swapSlippageMaxSP(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lpAsset1,
    STAmount const& lpAsset2,
    STAmount const& asset,
    std::uint16_t slippage,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const sp = calcSpotPrice(asset1Balance, asset2Balance, weight1, tfee);
    // The asset to be swapped out, such that after
    // the trade the SP of this asset is no more than maxSP
    if (sp <= STAmount{noIssue(), maxSP.mantissa(), maxSP.exponent()})
        return swapOutMaxSP(
            view,
            ammAccount,
            asset1Balance,
            asset2Balance,
            lpAsset1,
            asset.issue(),
            maxSP,
            weight1,
            tfee);

    // The asset to be swapped in, such that after the trade the SP of this
    // asset is no less than maxSP and the slippage for the trade does not
    // exceed slippage
    auto const assetIn =
        changeSpotPrice(asset1Balance, asset2Balance, maxSP, weight1, tfee);
    if (!assetIn)
        return tecAMM_FAILED_SWAP;

    STAmount saSlippage{noIssue(), slippage, -3};
    auto const ss = slippageSlope(asset1Balance, weight1, tfee);
    if (auto const s = multiply(*assetIn, ss, noIssue()); s <= saSlippage)
        return swapIn(
            view,
            ammAccount,
            asset1Balance,
            asset2Balance,
            lpAsset2,
            *assetIn,
            weight1,
            tfee);

    // Figure out assetIn given the slippage
    auto const newAssetIn = divide(saSlippage, ss, asset.issue());
    auto const tosq = divide(newAssetIn, asset1Balance, noIssue());
    if (auto const newSP =
            multiply(multiply(tosq, tosq, noIssue()), sp, maxSP.issue());
        newSP >= maxSP)
        return swapIn(
            view,
            ammAccount,
            asset1Balance,
            asset2Balance,
            lpAsset2,
            newAssetIn,
            weight1,
            tfee);

    return tecAMM_FAILED_SWAP;
}

}  // namespace ripple
