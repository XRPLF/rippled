// Fee collection for concentrated liquidity positions. Fee growth accounting
// pattern (global/outside/inside) from Discussion #427 by Roman Thpt (@RomThpt).
// Storage uses XRPL's native Number rather than v3's Q128.128 uint256 — same
// semantics, simpler math, native precision.

#include <xrpl/tx/transactors/dex/AMMCollectFees.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

NotTEC
AMMCollectFees::preflight(PreflightContext const& ctx)
{
    bool const hasPos = ctx.tx.isFieldPresent(sfPositionID);
    bool const hasBin = ctx.tx.isFieldPresent(sfBinID);
    if (hasPos == hasBin)
        return temMALFORMED;  // exactly one is required
    return tesSUCCESS;
}

TER
AMMCollectFees::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const curveType =
        ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType) : kCtConstantProduct;
    auto const ammKeylet = keylet::amm(asset, asset2, curveType);
    auto const ammSle = ctx.view.read(ammKeylet);
    if (!ammSle)
        return terNO_AMM;

    auto const actualCurve = getCurveType(*ammSle);
    if (actualCurve != kCtConcentratedLiquidity && actualCurve != kCtBinned)
        return tecAMM_FAILED;
    // Cross-check the curve type matches which addressing field was used.
    bool const hasPos = ctx.tx.isFieldPresent(sfPositionID);
    if (actualCurve == kCtConcentratedLiquidity && !hasPos)
        return temMALFORMED;
    if (actualCurve == kCtBinned && hasPos)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
AMMCollectFees::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const account = ctx_.tx[sfAccount];
    auto const asset = ctx_.tx[sfAsset];
    auto const asset2 = ctx_.tx[sfAsset2];
    auto const curveType =
        ctx_.tx.isFieldPresent(sfCurveType) ? ctx_.tx.getFieldU8(sfCurveType) : kCtConstantProduct;

    auto const ammSle = sb.peek(keylet::amm(asset, asset2, curveType));
    if (!ammSle)
        return tecINTERNAL;

    auto const ammAccount = (*ammSle)[sfAccount];
    auto const ammID = ammSle->key();

    // ─────────────── Binned path ───────────────
    if (curveType == kCtBinned)
    {
        auto const binID = ctx_.tx.getFieldI32(sfBinID);
        auto const binKeylet = keylet::ammBin(ammID, binID);
        auto const binSle = sb.peek(binKeylet);
        if (!binSle)
            return tecNO_ENTRY;

        // MPT balance is authoritative for shares. LP must hold MPT
        // for this bin's issuance — either via AMMDeposit or via an
        // inbound MPT transfer.
        auto const mptIssuanceID = binSle->getFieldH192(sfMPTokenIssuanceID);
        auto const mptokenSle = sb.read(keylet::mptoken(mptIssuanceID, account));
        if (!mptokenSle)
            return tecNO_ENTRY;
        auto const lpShares = mptokenSle->getFieldU64(sfMPTAmount);
        if (lpShares == 0)
            return tecAMM_FAILED;

        // A holder without a snapshot (shares received by transfer) starts
        // one now and is owed nothing yet.
        auto const holdingKeylet = keylet::ammBinHolding(ammID, account, binID);
        auto holdingSle = sb.peek(holdingKeylet);
        if (!holdingSle)
        {
            holdingSle = std::make_shared<SLE>(holdingKeylet);
            (*holdingSle)[sfAccount] = account;
            (*holdingSle)[sfAMMID] = ammID;
            holdingSle->setFieldI32(sfBinID, binID);
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast0,
                STNumber{sfFeeGrowthInsideLast0, Number{binSle->getFieldNumber(sfFeeGrowthBin0)}});
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast1,
                STNumber{sfFeeGrowthInsideLast1, Number{binSle->getFieldNumber(sfFeeGrowthBin1)}});
            holdingSle->setFieldU64(sfShares, lpShares);
            sb.insert(holdingSle);
            auto const page =
                sb.dirInsert(keylet::ownerDir(account), holdingKeylet, describeOwnerDir(account));
            if (!page)
                return tecDIR_FULL;  // LCOV_EXCL_LINE
            (*holdingSle)[sfOwnerNode] = *page;
            sb.update(holdingSle);
            sb.apply(ctx_.rawView());
            return tesSUCCESS;
        }

        if (auto const ter = settleBinFees(
                sb, ammAccount, account, *binSle, *holdingSle, lpShares, ctx_.journal);
            !isTesSuccess(ter))
            return ter;
        holdingSle->setFieldU64(sfShares, lpShares);
        sb.update(holdingSle);
        sb.update(binSle);
        sb.apply(ctx_.rawView());
        return tesSUCCESS;
    }

    // ─────────────── CL path (unchanged) ───────────────
    auto const positionID = ctx_.tx[sfPositionID];

    // The tx's sfPositionID is the position SLE's keylet hash (same
    // convention AMMWithdraw uses). Direct lookup — AMMDeposit creates
    // positions at keylet::ammPosition(ammID, owner, seq).
    auto const posSle = sb.peek(keylet::ammPosition(positionID));
    if (!posSle || posSle->getType() != ltAMM_POSITION || posSle->getFieldH256(sfAMMID) != ammID)
        return tecNO_ENTRY;

    if ((*posSle)[sfAccount] != account)
        return tecNO_PERMISSION;

    auto const tickLower = posSle->getFieldI32(sfTickLower);
    auto const tickUpper = posSle->getFieldI32(sfTickUpper);
    auto const posLiquidity = posSle->getFieldU64(sfPositionLiquidity);
    auto const insideLast0 = Number{posSle->getFieldNumber(sfFeeGrowthInsideLast0)};
    auto const insideLast1 = Number{posSle->getFieldNumber(sfFeeGrowthInsideLast1)};

    auto const inside = clFeeGrowthInside(sb, *ammSle, tickLower, tickUpper);
    if (!inside)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    auto const& [inside0, inside1] = *inside;

    // Fees newly accrued since last snapshot.
    auto const liq = Number(static_cast<std::int64_t>(posLiquidity));
    auto const newly0 = (inside0 - insideLast0) * liq;
    auto const newly1 = (inside1 - insideLast1) * liq;

    // Add to prior residue (kept on the position when a previous collect
    // capped one side below the available amount). Today AMMCollectFees
    // claims everything available; sfTokensOwed0/1 stay zero. The fields
    // exist as forward-compat hooks for a future per-side cap addition.
    auto const stored0 = posSle->isFieldPresent(sfTokensOwed0)
        ? Number{posSle->getFieldAmount(sfTokensOwed0)}
        : Number{0};
    auto const stored1 = posSle->isFieldPresent(sfTokensOwed1)
        ? Number{posSle->getFieldAmount(sfTokensOwed1)}
        : Number{0};

    auto const total0 = stored0 + (newly0 > Number{0} ? newly0 : Number{0});
    auto const total1 = stored1 + (newly1 > Number{0} ? newly1 : Number{0});

    // Fee growth 0 is in token0, the pool's lower asset, whatever order the
    // transaction names the assets in.
    auto const token0 = (*ammSle)[sfAsset];
    auto const token1 = (*ammSle)[sfAsset2];
    auto const fees0 = total0 > Number{0} ? toSTAmount(token0, total0) : STAmount{token0, 0};
    auto const fees1 = total1 > Number{0} ? toSTAmount(token1, total1) : STAmount{token1, 0};

    if (fees0 > beast::kZero)
    {
        if (auto const ter = accountSend(
                sb, ammAccount, account, fees0, ctx_.journal, {}, WaiveTransferFee::Yes);
            !isTesSuccess(ter))
            return ter;
    }
    if (fees1 > beast::kZero)
    {
        if (auto const ter = accountSend(
                sb, ammAccount, account, fees1, ctx_.journal, {}, WaiveTransferFee::Yes);
            !isTesSuccess(ter))
            return ter;
    }

    // Advance the snapshot so the next collect counts only fees accrued
    // from here forward, and reset the residue stash (we paid out the full
    // amount).
    posSle->setFieldNumber(sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, inside0});
    posSle->setFieldNumber(sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, inside1});
    posSle->setFieldAmount(sfTokensOwed0, STAmount{token0, 0});
    posSle->setFieldAmount(sfTokensOwed1, STAmount{token1, 0});
    sb.update(posSle);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

void
AMMCollectFees::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
AMMCollectFees::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
