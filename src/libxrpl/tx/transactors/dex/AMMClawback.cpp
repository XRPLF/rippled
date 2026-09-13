#include <xrpl/tx/transactors/dex/AMMClawback.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/dex/AMMWithdraw.h>

#include <cstdint>
#include <optional>
#include <tuple>

namespace xrpl {

std::uint32_t
AMMClawback::getFlagsMask(PreflightContext const& ctx)
{
    return tfAMMClawbackMask;
}

bool
AMMClawback::checkExtraFeatures(xrpl::PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMMClawback))
        return false;

    std::optional<STAmount> const clawAmount = ctx.tx[~sfAmount];

    return ctx.rules.enabled(featureMPTokensV2) ||
        (!(clawAmount && clawAmount->holds<MPTIssue>()) && !ctx.tx[sfAsset].holds<MPTIssue>() &&
         !ctx.tx[sfAsset2].holds<MPTIssue>());
}

NotTEC
AMMClawback::preflight(PreflightContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    AccountID const holder = ctx.tx[sfHolder];

    if (issuer == holder)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: holder cannot be the same as issuer.";
        return temMALFORMED;
    }

    std::optional<STAmount> const clawAmount = ctx.tx[~sfAmount];
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];

    if (isXRP(asset))
        return temMALFORMED;

    if (ctx.tx.isFlag(tfClawTwoAssets) && asset.getIssuer() != asset2.getIssuer())
    {
        JLOG(ctx.j.trace()) << "AMMClawback: tfClawTwoAssets can only be enabled when two "
                               "assets in the AMM pool are both issued by the issuer";
        return temINVALID_FLAG;
    }

    if (asset.getIssuer() != issuer)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Asset's account does not "
                               "match Account field.";
        return temMALFORMED;
    }

    if (clawAmount && clawAmount->asset() != asset)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Amount's asset subfield "
                               "does not match Asset field";
        return temBAD_AMOUNT;
    }

    if (clawAmount && *clawAmount <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
AMMClawback::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const sleIssuer = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    if (!sleIssuer)
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    if (!ctx.view.read(keylet::account(ctx.tx[sfHolder])))
        return terNO_ACCOUNT;

    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);
    auto const ammSle = ctx.view.read(keylet::amm(asset, asset2, curveType));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Clawback: Invalid asset pair.";
        return terNO_AMM;
    }

    if (!ctx.view.rules().enabled(featureMPTokensV2))
    {
        // If AllowTrustLineClawback is not set or NoFreeze is set, return no
        // permission
        if (!sleIssuer->isFlag(lsfAllowTrustLineClawback) || sleIssuer->isFlag(lsfNoFreeze))
        {
            return tecNO_PERMISSION;
        }
    }

    auto const checkClawAsset = [&](Asset const asset) -> bool {
        return asset.visit(
            [&](Issue const& issue) {
                if (issue.native())
                    return false;  // LCOV_EXCL_LINE

                return sleIssuer->isFlag(lsfAllowTrustLineClawback) &&
                    !sleIssuer->isFlag(lsfNoFreeze);
            },
            [&](MPTIssue const& issue) {
                auto const sleIssuance = ctx.view.read(keylet::mptokenIssuance(issue.getMptID()));

                return sleIssuance && sleIssuance->isFlag(lsfMPTCanClawback) &&
                    sleIssuance->getAccountID(sfIssuer) == ctx.tx[sfAccount];
            });
    };

    if (!checkClawAsset(asset))
        return tecNO_PERMISSION;

    if (ctx.tx.isFlag(tfClawTwoAssets) && !checkClawAsset(asset2))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
AMMClawback::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const ter = applyGuts(sb);
    if (isTesSuccess(ter))
        sb.apply(ctx_.rawView());

    return ter;
}

// AMMClawback operates on the LP-token-based withdraw path
// (equalWithdrawTokens / equalWithdrawMatchingOneAmount). CL pools have
// no fungible LP token supply (sfLPTokenBalance is always zero) — so for
// CL the clawback short-circuits with tecAMM_BALANCE before any tick or
// bitmap state can be touched. Consequence: CL tick SLEs and tick-bitmap
// SLEs are NOT mutated by this transactor; AMMDeposit/AMMWithdraw remain
// the sole writers of those structures. If a future amendment adds
// CL-aware clawback, that code MUST mirror tick + bitmap maintenance
// the same way AMMWithdraw does.
TER
AMMClawback::applyGuts(Sandbox& sb)
{
    std::optional<STAmount> const clawAmount = ctx_.tx[~sfAmount];
    AccountID const issuer = ctx_.tx[sfAccount];
    AccountID const holder = ctx_.tx[sfHolder];
    Asset const asset = ctx_.tx[sfAsset];
    Asset const asset2 = ctx_.tx[sfAsset2];

    auto const curveType = ctx_.tx.isFieldPresent(sfCurveType) ? ctx_.tx.getFieldU8(sfCurveType)
                                                               : std::uint8_t(CtConstantProduct);
    auto ammSle = sb.peek(keylet::amm(asset, asset2, curveType));
    if (!ammSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ammAccount = (*ammSle)[sfAccount];
    auto const accountSle = sb.read(keylet::account(ammAccount));
    if (!accountSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // ─────────────── CtBinned clawback ───────────────
    // The holder may hold MPT shares across multiple bins of this AMM.
    // Compute the holder's claim on `asset` across all bins; scale the
    // requested clawback into per-bin actions; for each affected bin,
    // burn the proportional MPT, decrement reserves of the clawback
    // asset, transfer the asset from AMM to issuer. The paired asset is
    // also drained pro-rata (single-asset clawback would imbalance the
    // bin's constant-sum invariant on the next swap; we drain both).
    if (curveType == CtBinned)
    {
        auto const& clawAsset = asset;
        Asset const ammAsset0 = (*ammSle)[sfAsset];
        bool const clawIsAsset0 = clawAsset == ammAsset0;
        SF_AMOUNT const& reserveClawField = clawIsAsset0
            ? static_cast<SF_AMOUNT const&>(sfReserve0)
            : static_cast<SF_AMOUNT const&>(sfReserve1);
        SF_AMOUNT const& reservePairField = clawIsAsset0
            ? static_cast<SF_AMOUNT const&>(sfReserve1)
            : static_cast<SF_AMOUNT const&>(sfReserve0);

        // Pass 1: enumerate (binID, lpShares, binReserveClaw,
        // binReservePair, binOutstanding, mptIssuanceID, holdingKeylet)
        // for every bin where the holder has shares; sum the holder's
        // claim on the clawback asset.
        struct BinSlice
        {
            std::int32_t binID;
            std::uint64_t lpShares;
            STAmount binReserveClaw;
            STAmount binReservePair;
            std::uint64_t outstanding;
            uint192 mptIssuanceID;
            uint256 holdingKey;
        };
        std::vector<BinSlice> slices;
        Number holderClaim{0};
        auto const ammID = ammSle->key();
        forEachItem(sb, ammAccount, [&](std::shared_ptr<SLE const> const& s) {
            if (!s || s->getType() != ltAMM_BIN)
                return;
            if (!s->isFieldPresent(sfAMMID) || s->getFieldH256(sfAMMID) != ammID)
                return;
            auto const mptId = s->getFieldH192(sfMPTokenIssuanceID);
            auto const mpt = sb.read(keylet::mptoken(mptId, holder));
            if (!mpt)
                return;
            auto const shares = mpt->getFieldU64(sfMPTAmount);
            if (shares == 0)
                return;
            auto const out = s->getFieldU64(sfOutstandingAmount);
            if (out == 0)
                return;
            auto const r = s->getFieldAmount(reserveClawField);
            auto const rp = s->getFieldAmount(reservePairField);
            BinSlice bs;
            bs.binID = s->getFieldI32(sfBinID);
            bs.lpShares = shares;
            bs.binReserveClaw = r;
            bs.binReservePair = rp;
            bs.outstanding = out;
            bs.mptIssuanceID = mptId;
            bs.holdingKey = keylet::ammBinHolding(ammID, holder, bs.binID).key;
            slices.push_back(bs);
            holderClaim += Number{r} *
                (Number{static_cast<std::int64_t>(shares)} /
                 Number{static_cast<std::int64_t>(out)});
        });

        if (slices.empty() || holderClaim <= Number{0})
            return tecAMM_BALANCE;

        // Clawback target. If sfAmount provided, cap at holderClaim.
        Number targetClaw = clawAmount ? std::min(Number{*clawAmount}, holderClaim) : holderClaim;

        // Pass 2: drain each bin proportionally.
        for (auto const& s : slices)
        {
            auto binSle = sb.peek(keylet::ammBin(ammID, s.binID));
            auto mptokenSle = sb.peek(keylet::mptoken(s.mptIssuanceID, holder));
            auto issSle = sb.peek(keylet::mptokenIssuance(s.mptIssuanceID));
            if (!binSle || !mptokenSle || !issSle)
                return tecINTERNAL;

            Number const binClaim = Number{s.binReserveClaw} *
                (Number{static_cast<std::int64_t>(s.lpShares)} /
                 Number{static_cast<std::int64_t>(s.outstanding)});
            // Fraction of this bin slice's claim that the clawback eats.
            Number const frac = binClaim > Number{0}
                ? (targetClaw * (binClaim / holderClaim)) / binClaim
                : Number{0};
            if (frac <= Number{0})
                continue;

            // Shares to burn from holder in this bin.
            std::uint64_t const sharesBurn = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(Number{static_cast<std::int64_t>(s.lpShares)} * frac));
            if (sharesBurn == 0)
                continue;

            // Bin reserves taken out: proportional on both sides so the
            // bin's price invariant is preserved.
            Number const burnFrac = Number{static_cast<std::int64_t>(sharesBurn)} /
                Number{static_cast<std::int64_t>(s.outstanding)};
            STAmount const drainClaw =
                toSTAmount(s.binReserveClaw.asset(), Number{s.binReserveClaw} * burnFrac);
            STAmount const drainPair =
                toSTAmount(s.binReservePair.asset(), Number{s.binReservePair} * burnFrac);

            // Send the clawback asset to the issuer; the paired asset
            // goes back to the holder (the LP's share of the other side
            // doesn't belong to the issuer, but the bin can't keep it
            // around without breaking the per-bin sum invariant).
            if (drainClaw > beast::kZero)
            {
                if (auto const ter = accountSend(
                        sb, ammAccount, issuer, drainClaw, ctx_.journal, {}, WaiveTransferFee::Yes);
                    !isTesSuccess(ter))
                    return ter;
            }
            if (drainPair > beast::kZero)
            {
                if (auto const ter = accountSend(
                        sb, ammAccount, holder, drainPair, ctx_.journal, {}, WaiveTransferFee::Yes);
                    !isTesSuccess(ter))
                    return ter;
            }

            // Update bin reserves + outstanding (both bin and issuance).
            binSle->setFieldAmount(reserveClawField, s.binReserveClaw - drainClaw);
            binSle->setFieldAmount(reservePairField, s.binReservePair - drainPair);
            binSle->setFieldU64(sfOutstandingAmount, s.outstanding - sharesBurn);
            sb.update(binSle);
            (*mptokenSle)[sfMPTAmount] = s.lpShares - sharesBurn;
            sb.update(mptokenSle);
            auto const issOut = issSle->getFieldU64(sfOutstandingAmount);
            (*issSle)[sfOutstandingAmount] = issOut >= sharesBurn ? issOut - sharesBurn : 0;
            sb.update(issSle);
        }
        return tesSUCCESS;
    }

    if (sb.rules().enabled(fixAMMClawbackRounding))
    {
        // retrieve LP token balance inside the amendment gate to avoid inconsistent error behavior
        auto const lpTokenBalance = ammLPHolds(sb, *ammSle, holder, j_);
        if (lpTokenBalance == beast::kZero)
            return tecAMM_BALANCE;

        if (auto const res = verifyAndAdjustLPTokenBalance(sb, lpTokenBalance, ammSle, holder);
            !res)
            return res.error();  // LCOV_EXCL_LINE
    }

    auto const expected = ammHolds(
        sb,
        *ammSle,
        asset,
        asset2,
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ctx_.journal);

    if (!expected)
        return expected.error();  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    TER result;
    STAmount newLPTokenBalance;
    STAmount amountWithdraw;
    std::optional<STAmount> amount2Withdraw;

    // calling a second time on purpose since `verifyAndAdjustLPTokenBalance` rounds and may adjust
    // the balance
    auto const holdLPtokens = ammLPHolds(sb, *ammSle, holder, j_);
    if (holdLPtokens == beast::kZero)
        return tecAMM_BALANCE;

    if (!clawAmount)
    {
        // Because we are doing a two-asset withdrawal,
        // tfee is actually not used, so pass tfee as 0.
        std::tie(result, newLPTokenBalance, amountWithdraw, amount2Withdraw) =
            AMMWithdraw::equalWithdrawTokens(
                sb,
                *ammSle,
                holder,
                issuer,
                ammAccount,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                holdLPtokens,
                holdLPtokens,
                0,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                ReserveHandling::IgnoreReserve,
                WithdrawAll::Yes,
                preFeeBalance_,
                ctx_.journal);
    }
    else
    {
        std::tie(result, newLPTokenBalance, amountWithdraw, amount2Withdraw) =
            equalWithdrawMatchingOneAmount(
                sb,
                *ammSle,
                holder,
                ammAccount,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                holdLPtokens,
                *clawAmount);
    }

    if (!isTesSuccess(result))
        return result;

    if (sb.rules().enabled(fixCleanup3_3_0) && sb.rules().enabled(fixAMMv1_3))
    {
        if (auto const ter =
                checkAMMPrecisionLoss(sb, ammAccount, asset, asset2, newLPTokenBalance, j_);
            !isTesSuccess(ter))
        {
            return ter;
        }
    }

    auto const res = AMMWithdraw::deleteAMMAccountIfEmpty(
        sb, ammSle, newLPTokenBalance, asset, asset2, j_, curveType);
    if (!res.second)
        return res.first;  // LCOV_EXCL_LINE

    JLOG(ctx_.journal.trace()) << "AMM Withdraw during AMMClawback: lptoken new balance: "
                               << to_string(newLPTokenBalance.iou())
                               << " old balance: " << to_string(lptAMMBalance.iou());

    auto sendAmount = [&](STAmount const& saAmount) -> TER {
        bool const checkIssuer = saAmount.holds<Issue>();
        return directSendNoFee(sb, holder, issuer, saAmount, checkIssuer, j_);
    };

    auto const ter = sendAmount(amountWithdraw);
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    // if the issuer issues both assets and sets flag tfClawTwoAssets, we
    // will claw the paired asset as well. We already checked if
    // tfClawTwoAssets is enabled, the two assets have to be issued by the
    // same issuer.
    if (!amount2Withdraw)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (ctx_.tx.isFlag(tfClawTwoAssets))
        return sendAmount(*amount2Withdraw);

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
    STAmount const& holdLPtokens,
    STAmount const& amount)
{
    // The clawback issuer signs for its own asset only. Threaded into the
    // withdrawal so a recreated MPToken is auto-authorized only for the
    // clawback issuer's asset, never for a paired asset from another issuer.
    // preflight guarantees sfAccount is the clawed asset's issuer (it rejects
    // the tx as temMALFORMED when sfAsset's issuer != sfAccount), so this is
    // the issuer, not just any signer.
    AccountID const issuer = ctx_.tx[sfAccount];

    auto frac = Number{amount} / amountBalance;
    auto amount2Withdraw = amount2Balance * frac;

    auto const lpTokensWithdraw = toSTAmount(lptAMMBalance.asset(), lptAMMBalance * frac);
    auto const& rules = sb.rules();
    // Pre-fixCleanup3_4_0 only a strictly greater computed LP amount takes
    // the withdraw-all path. Equality left the last holder unable to be
    // fully clawed. The amendment treats equality as withdraw-all.
    if (rules.enabled(fixCleanup3_4_0) ? lpTokensWithdraw >= holdLPtokens
                                       : lpTokensWithdraw > holdLPtokens)
    {
        return AMMWithdraw::equalWithdrawTokens(
            sb,
            ammSle,
            holder,
            issuer,
            ammAccount,
            amountBalance,
            amount2Balance,
            lptAMMBalance,
            holdLPtokens,
            holdLPtokens,
            0,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            ReserveHandling::IgnoreReserve,
            WithdrawAll::Yes,
            preFeeBalance_,
            ctx_.journal);
    }

    if (rules.enabled(fixAMMClawbackRounding))
    {
        auto tokensAdj = getRoundedLPTokens(rules, lptAMMBalance, frac, IsDeposit::No);

        // LCOV_EXCL_START
        if (tokensAdj == beast::kZero)
            return {tecAMM_INVALID_TOKENS, STAmount{}, STAmount{}, std::nullopt};
        // LCOV_EXCL_STOP

        frac = adjustFracByTokens(rules, lptAMMBalance, tokensAdj, frac);
        auto amount2Rounded = getRoundedAsset(rules, amount2Balance, frac, IsDeposit::No);

        auto amountRounded = getRoundedAsset(rules, amountBalance, frac, IsDeposit::No);

        // The requested clawback amount is likely too small and results in
        // one-sided pool withdrawal due to round off. Fail so the issuer can
        // clawback a larger amount.
        if (rules.enabled(fixCleanup3_4_0) &&
            (amountRounded == beast::kZero || amount2Rounded == beast::kZero))
            return {tecAMM_FAILED, STAmount{}, STAmount{}, STAmount{}};

        return AMMWithdraw::withdraw(
            sb,
            ammSle,
            ammAccount,
            issuer,
            holder,
            amountBalance,
            amountRounded,
            amount2Rounded,
            lptAMMBalance,
            tokensAdj,
            0,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            ReserveHandling::IgnoreReserve,
            WithdrawAll::No,
            preFeeBalance_,
            ctx_.journal);
    }

    // Because we are doing a two-asset withdrawal,
    // tfee is actually not used, so pass tfee as 0.
    return AMMWithdraw::withdraw(
        sb,
        ammSle,
        ammAccount,
        issuer,
        holder,
        amountBalance,
        amount,
        toSTAmount(amount2Balance.asset(), amount2Withdraw),
        lptAMMBalance,
        toSTAmount(lptAMMBalance.asset(), lptAMMBalance * frac),
        0,
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ReserveHandling::IgnoreReserve,
        WithdrawAll::No,
        preFeeBalance_,
        ctx_.journal);
}

void
AMMClawback::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMClawback::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
