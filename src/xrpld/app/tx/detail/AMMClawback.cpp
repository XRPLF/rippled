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

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/tx/detail/AMMClawback.h>
#include <xrpld/app/tx/detail/AMMWithdraw.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>
#include <tuple>

namespace ripple {

NotTEC
AMMClawback::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMMClawback))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;  // LCOV_EXCL_LINE

    if (ctx.tx.getFlags() & tfAMMClawbackMask)
        return temINVALID_FLAG;

    AccountID const issuer = ctx.tx[sfAccount];
    AccountID const holder = ctx.tx[sfHolder];

    if (issuer == holder)
    {
        JLOG(ctx.j.trace())
            << "AMMClawback: holder cannot be the same as issuer.";
        return temMALFORMED;
    }

    std::optional<STAmount> const clawAmount = ctx.tx[~sfAmount];
    auto const asset = ctx.tx[sfAsset];

    if (isXRP(asset))
        return temMALFORMED;

    if (asset.account != issuer)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Asset's account does not "
                               "match Account field.";
        return temBAD_ASSET_ISSUER;
    }

    if (clawAmount && clawAmount->issue() != asset)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Amount's issuer/currency subfield "
                               "does not match Asset field";
        return temBAD_ASSET_AMOUNT;
    }

    if (clawAmount && *clawAmount <= beast::zero)
        return temBAD_AMOUNT;

    return preflight2(ctx);
}

TER
AMMClawback::preclaim(PreclaimContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    AccountID const holder = ctx.tx[sfHolder];
    AccountID const ammAccount = ctx.tx[sfAMMAccount];

    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    auto const sleHolder = ctx.view.read(keylet::account(holder));
    auto const sleAMMAccount = ctx.view.read(keylet::account(ammAccount));

    if (!sleAMMAccount)
    {
        JLOG(ctx.j.debug())
            << "AMMClawback: AMMAccount provided does not exist.";
        return terNO_AMM;
    }

    std::uint32_t const issuerFlagsIn = sleIssuer->getFieldU32(sfFlags);

    // If AllowTrustLineClawback is not set or NoFreeze is set, return no
    // permission
    if (!(issuerFlagsIn & lsfAllowTrustLineClawback) ||
        (issuerFlagsIn & lsfNoFreeze))
        return tecNO_PERMISSION;

    auto const ammID = sleAMMAccount->getFieldH256(sfAMMID);
    if (!ammID)
    {
        JLOG(ctx.j.trace())
            << "AMMClawback: AMMAccount field is not an AMM account.";
        return terNO_AMM;
    }

    auto const sleAMM = ctx.view.read(keylet::amm(ammID));
    if (!sleAMM)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    STIssue const& asset = sleAMM->getFieldIssue(sfAsset);
    STIssue const& asset2 = sleAMM->getFieldIssue(sfAsset2);

    if (ctx.tx[sfAsset] != asset && ctx.tx[sfAsset] != asset2)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Asset being clawed back does not "
                               "match either asset in the AMM pool.";
        return tecNO_PERMISSION;
    }

    auto const flags = ctx.tx.getFlags();
    if (flags & tfClawTwoAssets)
    {
        if (asset.issue().account != asset2.issue().account)
        {
            JLOG(ctx.j.trace())
                << "AMMClawback: tfClawTwoAssets can only be enabled when two "
                   "assets in the AMM pool are both issued by the issuer";
            return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
AMMClawback::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const ter = applyGuts(sb);
    if (ter == tesSUCCESS)
        sb.apply(ctx_.rawView());

    return ter;
}

TER
AMMClawback::applyGuts(Sandbox& sb)
{
    std::optional<STAmount> const clawAmount = ctx_.tx[~sfAmount];
    AccountID const ammAccount = ctx_.tx[sfAMMAccount];
    AccountID const issuer = ctx_.tx[sfAccount];
    AccountID const holder = ctx_.tx[sfHolder];
    Issue const asset = ctx_.tx[sfAsset];

    auto const sleAMMAccount = ctx_.view().read(keylet::account(ammAccount));

    // should not happen. checked in preclaim.
    if (!sleAMMAccount)
        return terNO_AMM;  // LCOV_EXCL_LINE

    auto const ammID = sleAMMAccount->getFieldH256(sfAMMID);
    if (!ammID)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto ammSle = sb.peek(keylet::amm(ammID));
    if (!ammSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const tfee = getTradingFee(ctx_.view(), *ammSle, ammAccount);
    Issue const& issue1 = ammSle->getFieldIssue(sfAsset).issue();
    Issue const& issue2 = ammSle->getFieldIssue(sfAsset2).issue();

    Issue otherIssue = issue1;
    if (asset == issue1)
        otherIssue = issue2;

    auto const expected = ammHolds(
        sb,
        *ammSle,
        asset,
        otherIssue,
        FreezeHandling::fhZERO_IF_FROZEN,
        ctx_.journal);

    if (!expected)
        return expected.error();  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    TER result;
    STAmount newLPTokenBalance;
    STAmount amountWithdraw;
    std::optional<STAmount> amount2Withdraw;

    auto const holdLPtokens = ammLPHolds(sb, *ammSle, holder, j_);
    if (holdLPtokens == beast::zero)
        return tecINTERNAL;

    if (!clawAmount)
    {
        std::tie(result, newLPTokenBalance, amountWithdraw, amount2Withdraw) =
            AMMWithdraw::equalWithdrawTokens(
                sb,
                *ammSle,
                holder,
                ammAccount,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                holdLPtokens,
                holdLPtokens,
                tfee,
                ctx_.journal,
                ctx_.tx,
                true);
    }
    else
        std::tie(result, newLPTokenBalance, amountWithdraw, amount2Withdraw) =
            equalWithdrawMatchingOneAmount(
                sb,
                *ammSle,
                holder,
                ammAccount,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *clawAmount,
                tfee);

    if (result != tesSUCCESS)
        return result;  // LCOV_EXCL_LINE

    auto const res = deleteAMMAccountIfEmpty(
        sb, ammSle, newLPTokenBalance, issue1, issue2, j_);
    if (!res.second)
        return res.first;  // LCOV_EXCL_LINE

    JLOG(ctx_.journal.trace())
        << "AMM Withdraw during AMMClawback: lptoken new balance: "
        << to_string(newLPTokenBalance.iou())
        << " old balance: " << to_string(lptAMMBalance.iou());

    auto const ter = rippleCredit(sb, holder, issuer, amountWithdraw, true, j_);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE

    // if the issuer issues both assets and sets flag tfClawTwoAssets, we
    // will claw the paired asset as well. We already checked if
    // tfClawTwoAssets is enabled, the two assets have to be issued by the
    // same issuer.
    auto const flags = ctx_.tx.getFlags();
    if (flags & tfClawTwoAssets)

        return rippleCredit(sb, holder, issuer, *amount2Withdraw, true, j_);

    return tesSUCCESS;
}

std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
AMMClawback::equalWithdrawMatchingOneAmount(
    Sandbox& sb,
    SLE const& ammSle,
    AccountID const& holder,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    std::uint16_t tfee)
{
    auto frac = Number{amount} / amountBalance;
    auto const amount2Withdraw = amount2Balance * frac;

    auto const lpTokensWithdraw =
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);
    auto const holdLPtokens = ammLPHolds(sb, ammSle, holder, j_);
    if (lpTokensWithdraw > holdLPtokens)
        // if lptoken balance less than what the issuer intended to clawback,
        // clawback all the tokens
        return AMMWithdraw::equalWithdrawTokens(
            sb,
            ammSle,
            holder,
            ammAccount,
            amountBalance,
            amount2Balance,
            lptAMMBalance,
            holdLPtokens,
            holdLPtokens,
            tfee,
            ctx_.journal,
            ctx_.tx,
            true);

    return withdraw(
        sb,
        ammAccount,
        holder,
        ammSle,
        amountBalance,
        amount,
        toSTAmount(amount2Balance.issue(), amount2Withdraw),
        lptAMMBalance,
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac),
        tfee,
        ctx_.journal,
        ctx_.tx,
        false);
}

}  // namespace ripple
