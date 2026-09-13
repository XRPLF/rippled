#include <xrpl/tx/transactors/coupon/CouponScheduleCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <variant>

namespace xrpl {

NotTEC
CouponScheduleCreate::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfMPTokenIssuanceID] == beast::kZero)
        return temMALFORMED;

    // A bond paying coupons in itself is rebasing, not interest.
    if (auto const& couponAsset = ctx.tx[sfCouponAsset]; couponAsset.holds<MPTIssue>() &&
        couponAsset.get<MPTIssue>().getMptID() == ctx.tx[sfMPTokenIssuanceID])
        return temMALFORMED;

    // The declared terms are all-or-nothing: a schedule either publishes a
    // coupon date grid or distributes ad hoc.
    bool const hasAmount = ctx.tx.isFieldPresent(sfCouponAmount);
    bool const hasInterval = ctx.tx.isFieldPresent(sfCouponInterval);
    bool const hasFirst = ctx.tx.isFieldPresent(sfFirstCouponTime);
    if (hasAmount != hasInterval || hasAmount != hasFirst)
        return temMALFORMED;

    if (hasAmount)
    {
        if (ctx.tx[sfCouponAmount] <= beast::kZero)
            return temBAD_AMOUNT;
        if (ctx.tx[sfCouponInterval] == 0 || ctx.tx[sfFirstCouponTime] == 0)
            return temMALFORMED;
    }

    auto const expiration = ctx.tx[~sfExpiration];
    if (expiration && hasFirst && *expiration <= ctx.tx[sfFirstCouponTime])
        return temBAD_EXPIRATION;

    if (auto const notice = ctx.tx[~sfCallNoticePeriod]; notice && *notice == 0)
        return temMALFORMED;

    if (auto const earliestCall = ctx.tx[~sfEarliestCallTime])
    {
        // Call protection is meaningless on a non-callable schedule.
        if (!ctx.tx.isFieldPresent(sfCallNoticePeriod))
            return temMALFORMED;
        if (expiration && *earliestCall >= *expiration)
            return temBAD_EXPIRATION;
    }

    return tesSUCCESS;
}

TER
CouponScheduleCreate::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    AccountID const submitter = ctx.tx[sfAccount];

    // One schedule per issuance: the keylet is the issuance, so a second
    // create collides here.
    if (ctx.view.exists(keylet::couponSchedule(issuanceID)))
        return tecDUPLICATE;

    auto const issuance = ctx.view.read(keylet::mptokenIssuance(issuanceID));
    if (!issuance)
        return tecOBJECT_NOT_FOUND;

    // Confidential balances hide the units a coupon would accrue against.
    if (issuance->isFlag(lsfMPTCanHoldConfidentialBalance))
        return tecNO_PERMISSION;

    auto const& couponAsset = ctx.tx[sfCouponAsset];

    if (!couponAsset.native())
    {
        if (auto const ter = std::visit(
                [&]<typename T>(T const& issue) -> TER {
                    if constexpr (std::is_same_v<T, Issue>)
                    {
                        if (!ctx.view.exists(keylet::account(issue.account)))
                            return tecNO_ISSUER;
                    }
                    else
                    {
                        if (!ctx.view.exists(keylet::mptokenIssuance(issue.getMptID())))
                            return tecOBJECT_NOT_FOUND;
                    }
                    return tesSUCCESS;
                },
                couponAsset.value());
            !isTesSuccess(ter))
            return ter;
    }

    // The payer is the issuance's issuer. For a vault share issuance that is
    // the vault pseudo-account, and the vault owner administers the schedule.
    AccountID const payer = issuance->at(sfIssuer);
    if (payer == submitter)
        return tesSUCCESS;

    auto const sleIssuer = ctx.view.read(keylet::account(payer));
    if (!sleIssuer)
        return tecNO_ISSUER;  // LCOV_EXCL_LINE

    if (auto const vaultID = sleIssuer->at(~sfVaultID))
    {
        auto const vault = ctx.view.read(keylet::vault(*vaultID));
        if (!vault)
            return tecOBJECT_NOT_FOUND;  // LCOV_EXCL_LINE
        if (vault->at(sfOwner) != submitter)
            return tecNO_PERMISSION;
        return tesSUCCESS;
    }

    return tecNO_PERMISSION;
}

TER
CouponScheduleCreate::doApply()
{
    auto const& tx = ctx_.tx;
    auto const issuanceID = tx[sfMPTokenIssuanceID];

    auto const owner = view().peek(keylet::account(accountID_));
    if (!owner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto issuance = view().peek(keylet::mptokenIssuance(issuanceID));
    if (!issuance)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const& couponAsset = tx[sfCouponAsset];
    auto schedule = std::make_shared<SLE>(keylet::couponSchedule(issuanceID));

    schedule->at(sfOwner) = accountID_;
    schedule->at(sfAccount) = issuance->at(sfIssuer);
    schedule->at(sfMPTokenIssuanceID) = issuanceID;
    schedule->setFieldIssue(sfCouponAsset, STIssue{sfCouponAsset, couponAsset});
    schedule->at(sfAccruedPerUnit) = STAmount{couponAsset, 0};
    schedule->at(sfPoolAmount) = STAmount{couponAsset, 0};
    if (auto const couponAmount = tx[~sfCouponAmount])
    {
        schedule->at(sfCouponAmount) = *couponAmount;
        schedule->at(sfCouponInterval) = tx[sfCouponInterval];
        schedule->at(sfFirstCouponTime) = tx[sfFirstCouponTime];
    }
    if (auto const expiration = tx[~sfExpiration])
        schedule->at(sfExpiration) = *expiration;
    if (auto const notice = tx[~sfCallNoticePeriod])
        schedule->at(sfCallNoticePeriod) = *notice;
    if (auto const earliestCall = tx[~sfEarliestCallTime])
        schedule->at(sfEarliestCallTime) = *earliestCall;

    if (auto const ter = dirLink(view(), accountID_, schedule))
        return ter;  // LCOV_EXCL_LINE
    increaseOwnerCount(view(), owner, {}, 1, j_);
    if (preFeeBalance_ < accountReserve(view(), owner, j_))
        return tecINSUFFICIENT_RESERVE;

    view().insert(schedule);

    // Settlement reads this flag to skip issuances that pay no coupons.
    issuance->setFlag(lsfMPTCouponSchedule);
    view().update(issuance);

    return tesSUCCESS;
}

void
CouponScheduleCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CouponScheduleCreate::finalizeInvariants(
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
