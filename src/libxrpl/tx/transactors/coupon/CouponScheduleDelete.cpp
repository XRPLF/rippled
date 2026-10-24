#include <xrpl/tx/transactors/coupon/CouponScheduleDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <optional>
#include <variant>

namespace xrpl {

NotTEC
CouponScheduleDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfMPTokenIssuanceID] == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
CouponScheduleDelete::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    auto const schedule = ctx.view.read(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tecNO_ENTRY;

    if (ctx.tx[sfAccount] != schedule->at(sfOwner))
        return tecNO_PERMISSION;

    auto const issuance = ctx.view.read(keylet::mptokenIssuance(issuanceID));
    if (!issuance)
        return tecOBJECT_NOT_FOUND;  // LCOV_EXCL_LINE

    if (issuance->at(sfOutstandingAmount) > 0 || schedule->at(sfClaimantCount) > 0)
        return tecHAS_OBLIGATIONS;

    return tesSUCCESS;
}

TER
CouponScheduleDelete::doApply()
{
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const k = keylet::couponSchedule(issuanceID);
    auto const schedule = view().peek(k);
    if (!schedule)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(keylet::ownerDir(accountID_), schedule->at(sfOwnerNode), k.key, false))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "CouponScheduleDelete: failed to remove dir link.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    decreaseOwnerCount(view(), accountID_, std::nullopt, 1, j_);

    if (auto const ter = releaseResidual(schedule); !isTesSuccess(ter))
        return ter;
    view().erase(schedule);

    // Settlement can stop looking at this issuance.
    if (auto const issuance = view().peek(keylet::mptokenIssuance(issuanceID)))
    {
        issuance->clearFlag(lsfMPTCouponSchedule);
        view().update(issuance);
    }

    return tesSUCCESS;
}

TER
CouponScheduleDelete::releaseResidual(SLE::ConstRef schedule)
{
    STAmount const residual = schedule->at(sfPoolAmount);
    if (residual <= beast::kZero)
        return tesSUCCESS;

    // The residual goes to the administrator submitting this delete: the payer of a direct
    // issuance, or the owner of a vault, whose shares are all gone by now, so the residual
    // cannot return to an unowned vault balance.
    AccountID const payer = schedule->at(sfAccount);
    auto const sleReceiver = view().peek(keylet::account(accountID_));
    if (!sleReceiver)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // The pool's custody unwinds the way it was taken: XRP is credited, an IOU
    // comes back from its issuer and an MPT is unlocked.
    if (residual.native())
    {
        sleReceiver->at(sfBalance) = sleReceiver->at(sfBalance) + residual;
        view().update(sleReceiver);
        return tesSUCCESS;
    }

    return std::visit(
        [&]<typename T>(T const&) {
            return escrowUnlockApplyHelper<T>(
                ctx_.getApplyViewContext(),
                kParityRate,
                sleReceiver,
                preFeeBalance_,
                residual,
                residual.getIssuer(),
                payer,
                accountID_,
                true,
                j_);
        },
        residual.asset().value());
}

void
CouponScheduleDelete::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
    // No transaction-specific invariants yet (future work).
}

bool
CouponScheduleDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
