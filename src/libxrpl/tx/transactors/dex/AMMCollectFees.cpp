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
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

// v3 fee-growth-inside formula:
//   feeGrowthInside = feeGrowthGlobal - feeGrowthBelow(lower) - feeGrowthAbove(upper)
// where feeGrowthBelow/Above invert the stored "outside" snapshot when the
// current price is on the opposite side of the tick from where it was when
// the snapshot was taken. See v3-core Tick.sol::getFeeGrowthInside.
static Number
feeGrowthBelow(
    std::int32_t currentTick,
    std::int32_t tick,
    Number const& feeGrowthGlobal,
    Number const& feeGrowthOutside)
{
    if (currentTick >= tick)
        return feeGrowthOutside;
    return feeGrowthGlobal - feeGrowthOutside;
}

static Number
feeGrowthAbove(
    std::int32_t currentTick,
    std::int32_t tick,
    Number const& feeGrowthGlobal,
    Number const& feeGrowthOutside)
{
    if (currentTick < tick)
        return feeGrowthOutside;
    return feeGrowthGlobal - feeGrowthOutside;
}

bool
AMMCollectFees::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureAMMCurves);
}

NotTEC
AMMCollectFees::preflight(PreflightContext const& ctx)
{
    bool const hasPos = ctx.tx.isFieldPresent(sfPositionID);
    bool const hasBin = ctx.tx.isFieldPresent(sfBinID);
    if (hasPos == hasBin)
        return temMALFORMED;  // exactly one is required
    if (!ctx.tx.isFieldPresent(sfAsset) || !ctx.tx.isFieldPresent(sfAsset2))
        return temMALFORMED;
    return tesSUCCESS;
}

TER
AMMCollectFees::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);
    auto const ammKeylet = keylet::amm(asset, asset2, curveType);
    auto const ammSle = ctx.view.read(ammKeylet);
    if (!ammSle)
        return terNO_AMM;

    auto const actualCurve = getCurveType(*ammSle);
    if (actualCurve != CtConcentratedLiquidity && actualCurve != CtBinned)
        return tecAMM_FAILED;
    // Cross-check the curve type matches which addressing field was used.
    bool const hasPos = ctx.tx.isFieldPresent(sfPositionID);
    if (actualCurve == CtConcentratedLiquidity && !hasPos)
        return temMALFORMED;
    if (actualCurve == CtBinned && hasPos)
        return temMALFORMED;
    if (actualCurve == CtBinned && !ctx.view.rules().enabled(featureAMMCurves))
        return temDISABLED;

    return tesSUCCESS;
}

TER
AMMCollectFees::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const account = ctx_.tx[sfAccount];
    auto const asset = ctx_.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx_.tx[sfAsset2].get<Issue>();
    auto const curveType = ctx_.tx.isFieldPresent(sfCurveType) ? ctx_.tx.getFieldU8(sfCurveType)
                                                               : std::uint8_t(CtConstantProduct);

    auto ammSle = sb.peek(keylet::amm(asset, asset2, curveType));
    if (!ammSle)
        return tecINTERNAL;

    auto const ammAccount = (*ammSle)[sfAccount];
    auto const ammID = ammSle->key();

    // ─────────────── Binned path ───────────────
    if (curveType == CtBinned)
    {
        auto const binID = ctx_.tx.getFieldI32(sfBinID);
        auto const binKeylet = keylet::ammBin(ammID, binID);
        auto binSle = sb.peek(binKeylet);
        if (!binSle)
            return tecNO_ENTRY;

        // MPT balance is authoritative for shares. LP must hold MPT
        // for this bin's issuance — either via AMMDeposit or via an
        // inbound MPT transfer.
        auto const mptIssuanceID = binSle->getFieldH192(sfMPTokenIssuanceID);
        auto const mptokenSle =
            sb.read(keylet::mptoken(mptIssuanceID, account));
        if (!mptokenSle)
            return tecNO_ENTRY;
        auto const lpShares = mptokenSle->getFieldU64(sfMPTAmount);
        if (lpShares == 0)
            return tecAMM_FAILED;

        Number const fg0Now = Number{binSle->getFieldNumber(sfFeeGrowthBin0)};
        Number const fg1Now = Number{binSle->getFieldNumber(sfFeeGrowthBin1)};

        // Snapshot SLE may be missing if the LP received their MPT via
        // transfer rather than AMMDeposit. Auto-create with snapshot
        // pinned at "now" so this collect call delivers zero (the
        // transferred holder forfeits past fees; future fees collect
        // from this point forward).
        auto const holdingKeylet =
            keylet::ammBinHolding(ammID, account, binID);
        auto holdingSle = sb.peek(holdingKeylet);
        if (!holdingSle)
        {
            holdingSle = std::make_shared<SLE>(holdingKeylet);
            (*holdingSle)[sfAccount] = account;
            (*holdingSle)[sfAMMID] = ammID;
            holdingSle->setFieldI32(sfBinID, binID);
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, fg0Now});
            holdingSle->setFieldNumber(
                sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, fg1Now});
            sb.insert(holdingSle);
            auto const page = sb.dirInsert(
                keylet::ownerDir(account),
                holdingKeylet,
                describeOwnerDir(account));
            if (!page)
                return tecDIR_FULL;
            (*holdingSle)[sfOwnerNode] = *page;
            // Reserve-exempt: net-zero owner-count impact.
            // No collect math runs this turn (snapshot==now).
            sb.update(holdingSle);
            sb.apply(ctx_.rawView());
            return tesSUCCESS;
        }

        Number const fg0Last = Number{holdingSle->getFieldNumber(sfFeeGrowthInsideLast0)};
        Number const fg1Last = Number{holdingSle->getFieldNumber(sfFeeGrowthInsideLast1)};

        Number const sharesN{static_cast<std::int64_t>(lpShares)};
        Number const owed0 = sharesN * (fg0Now - fg0Last);
        Number const owed1 = sharesN * (fg1Now - fg1Last);

        // Decrement the bin's reserves by the collected fee amounts —
        // those tokens go from "shared pool" into the LP's wallet and
        // are no longer part of future swap pricing.
        auto const reserve0 = binSle->getFieldAmount(sfReserve0);
        auto const reserve1 = binSle->getFieldAmount(sfReserve1);
        auto const fees0 =
            owed0 > Number{0} ? toSTAmount(reserve0.asset(), owed0) : STAmount{reserve0.asset(), 0};
        auto const fees1 =
            owed1 > Number{0} ? toSTAmount(reserve1.asset(), owed1) : STAmount{reserve1.asset(), 0};
        if (fees0 > reserve0 || fees1 > reserve1)
            return tecAMM_FAILED;

        if (fees0 > beast::kZero)
        {
            if (auto const ter = accountSend(sb, ammAccount, account, fees0, ctx_.journal);
                !isTesSuccess(ter))
                return ter;
            binSle->setFieldAmount(sfReserve0, reserve0 - fees0);
        }
        if (fees1 > beast::kZero)
        {
            if (auto const ter = accountSend(sb, ammAccount, account, fees1, ctx_.journal);
                !isTesSuccess(ter))
                return ter;
            binSle->setFieldAmount(sfReserve1, reserve1 - fees1);
        }

        // Advance the LP's snapshot to "now" so subsequent swaps grow
        // the gap from this point forward.
        holdingSle->setFieldNumber(
            sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, fg0Now});
        holdingSle->setFieldNumber(
            sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, fg1Now});

        sb.update(holdingSle);
        sb.update(binSle);
        sb.apply(ctx_.rawView());
        return tesSUCCESS;
    }

    // ─────────────── CL path (unchanged) ───────────────
    auto const positionID = ctx_.tx[sfPositionID];
    auto const currentTick = ammSle->getFieldI32(sfCurrentTick);
    auto const feeGrowthGlobal0 = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal0)};
    auto const feeGrowthGlobal1 = Number{ammSle->getFieldNumber(sfFeeGrowthGlobal1)};

    // The tx's sfPositionID is the position SLE's keylet hash (same
    // convention AMMWithdraw uses). Direct lookup — AMMDeposit creates
    // positions at keylet::ammPosition(ammID, owner, seq).
    auto posSle = sb.peek(keylet::ammPosition(positionID));
    if (!posSle || posSle->getType() != ltAMM_POSITION ||
        posSle->getFieldH256(sfAMMID) != ammID)
        return tecNO_ENTRY;

    if ((*posSle)[sfAccount] != account)
        return tecNO_PERMISSION;

    auto const tickLower = posSle->getFieldI32(sfTickLower);
    auto const tickUpper = posSle->getFieldI32(sfTickUpper);
    auto const posLiquidity = posSle->getFieldU64(sfPositionLiquidity);
    auto const insideLast0 = Number{posSle->getFieldNumber(sfFeeGrowthInsideLast0)};
    auto const insideLast1 = Number{posSle->getFieldNumber(sfFeeGrowthInsideLast1)};

    auto const lowerTickSle = sb.read(keylet::ammTick(ammID, tickLower));
    auto const upperTickSle = sb.read(keylet::ammTick(ammID, tickUpper));
    if (!lowerTickSle || !upperTickSle)
        return tecINTERNAL;

    auto const inside0 = feeGrowthGlobal0 -
        feeGrowthBelow(
            currentTick,
            tickLower,
            feeGrowthGlobal0,
            Number{lowerTickSle->getFieldNumber(sfFeeGrowthOutside0)}) -
        feeGrowthAbove(
            currentTick,
            tickUpper,
            feeGrowthGlobal0,
            Number{upperTickSle->getFieldNumber(sfFeeGrowthOutside0)});

    auto const inside1 = feeGrowthGlobal1 -
        feeGrowthBelow(
            currentTick,
            tickLower,
            feeGrowthGlobal1,
            Number{lowerTickSle->getFieldNumber(sfFeeGrowthOutside1)}) -
        feeGrowthAbove(
            currentTick,
            tickUpper,
            feeGrowthGlobal1,
            Number{upperTickSle->getFieldNumber(sfFeeGrowthOutside1)});

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

    auto const fees0 = total0 > Number{0} ? toSTAmount(asset, total0) : STAmount{asset, 0};
    auto const fees1 = total1 > Number{0} ? toSTAmount(asset2, total1) : STAmount{asset2, 0};

    if (fees0 > beast::kZero)
    {
        if (auto const ter = accountSend(sb, ammAccount, account, fees0, ctx_.journal);
            !isTesSuccess(ter))
            return ter;
    }
    if (fees1 > beast::kZero)
    {
        if (auto const ter = accountSend(sb, ammAccount, account, fees1, ctx_.journal);
            !isTesSuccess(ter))
            return ter;
    }

    // Advance the snapshot so the next collect counts only fees accrued
    // from here forward, and reset the residue stash (we paid out the full
    // amount).
    posSle->setFieldNumber(sfFeeGrowthInsideLast0, STNumber{sfFeeGrowthInsideLast0, inside0});
    posSle->setFieldNumber(sfFeeGrowthInsideLast1, STNumber{sfFeeGrowthInsideLast1, inside1});
    posSle->setFieldAmount(sfTokensOwed0, STAmount{asset, 0});
    posSle->setFieldAmount(sfTokensOwed1, STAmount{asset2, 0});
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
AMMCollectFees::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
