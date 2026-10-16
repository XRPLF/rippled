#include <xrpl/tx/transactors/coupon/CouponClaim.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CouponHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <variant>

namespace xrpl {

NotTEC
CouponClaim::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfMPTokenIssuanceID] == beast::kZero)
        return temMALFORMED;

    if (auto const amount = ctx.tx[~sfAmount]; amount && *amount <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
CouponClaim::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    AccountID const holder = ctx.tx[sfAccount];

    auto const schedule = ctx.view.read(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tecNO_ENTRY;

    if (!ctx.view.exists(keylet::mptoken(issuanceID, holder)))
        return tecNO_ENTRY;

    auto const& couponAsset = schedule->at(sfCouponAsset);

    if (auto const amount = ctx.tx[~sfAmount]; amount && amount->asset() != couponAsset)
        return tecNO_PERMISSION;

    // Delivery eligibility on the holder's side only. The payer's side was
    // settled when CouponPay funded the pool.
    if (!couponAsset.native() && holder != couponAsset.getIssuer())
    {
        if (auto const ter = requireAuth(ctx.view, couponAsset, holder); !isTesSuccess(ter))
            return ter;
        if (isFrozen(ctx.view, holder, couponAsset))
            return couponAsset.holds<MPTIssue>() ? tecLOCKED : tecFROZEN;
    }

    return tesSUCCESS;
}

TER
CouponClaim::doApply()
{
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto schedule = view().peek(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto mptoken = view().peek(keylet::mptoken(issuanceID, accountID_));
    if (!mptoken)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Bring the holder current before reading what they are owed.
    if (auto const ter = couponSettleMPToken(schedule, mptoken, j_); !isTesSuccess(ter))
        return ter;

    STAmount const zero{schedule->at(sfCouponAsset), 0};
    auto const accruedField = mptoken->at(~sfCouponAccrued);
    STAmount const accrued = accruedField ? *accruedField : zero;
    if (accrued <= beast::kZero)
        return tecNO_PERMISSION;

    auto const requested = ctx_.tx[~sfAmount];
    STAmount const pay = std::min(requested ? *requested : accrued, accrued);
    STAmount const pool = schedule->at(sfPoolAmount);
    if (pool < pay)
        return tefINTERNAL;  // LCOV_EXCL_LINE — the pool always covers accruals.

    AccountID const payer = schedule->at(sfAccount);
    auto const& couponAsset = pay.asset();

    // Release from the pool. XRP is credited directly; an IOU comes back off
    // its issuer's trust line with the transfer fee deducted and an MPT is
    // unlocked, the same release the token escrow performs.
    if (couponAsset.native())
    {
        auto const sle = view().peek(keylet::account(accountID_));
        if (!sle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        sle->at(sfBalance) = sle->at(sfBalance) + pay;
        view().update(sle);
    }
    else
    {
        auto const issuerID = couponAsset.getIssuer();
        auto sleDest = view().peek(keylet::account(accountID_));
        if (!sleDest)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        Rate const lockedRate = transferRate(view(), issuerID);
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.getApplyViewContext(),
                        lockedRate,
                        sleDest,
                        preFeeBalance_,
                        pay,
                        issuerID,
                        payer,
                        accountID_,
                        true,
                        j_);
                },
                couponAsset.value());
            !isTesSuccess(ret))
            return ret;
    }

    STAmount const remaining = accrued - pay;
    if (remaining <= beast::kZero)
    {
        mptoken->makeFieldAbsent(sfCouponAccrued);
        schedule->at(sfClaimantCount) = schedule->at(sfClaimantCount) - 1;
    }
    else
    {
        mptoken->at(sfCouponAccrued) = remaining;
    }
    view().update(mptoken);

    schedule->at(sfPoolAmount) = pool - pay;
    view().update(schedule);

    ctx_.deliver(pay);

    return tesSUCCESS;
}

void
CouponClaim::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CouponClaim::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
