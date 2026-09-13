#include <xrpl/tx/transactors/coupon/CouponPay.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <variant>

namespace xrpl {

namespace {

// The coupon total for one payment: per-unit amount times units outstanding,
// rounded up so the pool is never short of what holders can settle.
STAmount
couponTotal(STAmount const& perUnit, std::uint64_t outstanding)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    return STAmount{perUnit.asset(), Number(outstanding) * Number(perUnit)};
}

}  // namespace

NotTEC
CouponPay::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfMPTokenIssuanceID] == beast::kZero)
        return temMALFORMED;

    auto const amount = ctx.tx[sfAmount];
    if (amount <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
CouponPay::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];

    auto const schedule = ctx.view.read(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tecNO_ENTRY;

    // Only the payer named on the schedule may declare a coupon. For a vault
    // share issuance that is the vault pseudo-account, not the administrator.
    if (ctx.tx[sfAccount] != schedule->at(sfAccount))
        return tecNO_PERMISSION;

    auto const amount = ctx.tx[sfAmount];
    if (amount.asset() != schedule->at(sfCouponAsset))
        return tecWRONG_ASSET;

    auto const closeTime = ctx.view.header().parentCloseTime.time_since_epoch().count();
    if (auto const expiration = schedule->at(~sfExpiration); expiration && closeTime >= *expiration)
        return tecEXPIRED;

    auto const issuance = ctx.view.read(keylet::mptokenIssuance(issuanceID));
    if (!issuance)
        return tecOBJECT_NOT_FOUND;

    std::uint64_t const outstanding = issuance->at(sfOutstandingAmount);
    if (outstanding == 0)
        return tecNO_PERMISSION;

    // The pool must be funded in full at declaration, so a claim can never
    // fail for want of funds.
    STAmount const total = couponTotal(amount, outstanding);
    AccountID const payer = ctx.tx[sfAccount];
    if (total.native())
    {
        if (xrpLiquid(ctx.view, payer, 0, ctx.j) < total.xrp())
            return tecINSUFFICIENT_FUNDS;
    }
    else if (payer != total.getIssuer())
    {
        STAmount const spendable = std::visit(
            [&]<typename T>(T const& issue) {
                if constexpr (std::is_same_v<T, Issue>)
                    return accountHolds(
                        ctx.view,
                        payer,
                        issue.currency,
                        issue.account,
                        FreezeHandling::ZeroIfFrozen,
                        ctx.j);
                else
                    return accountHolds(
                        ctx.view,
                        payer,
                        issue,
                        FreezeHandling::ZeroIfFrozen,
                        AuthHandling::IgnoreAuth,
                        ctx.j);
            },
            total.asset().value());
        if (spendable < total)
            return tecINSUFFICIENT_FUNDS;
    }

    return tesSUCCESS;
}

TER
CouponPay::doApply()
{
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto schedule = view().peek(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const issuance = view().read(keylet::mptokenIssuance(issuanceID));
    if (!issuance)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const perUnit = ctx_.tx[sfAmount];
    STAmount const total = couponTotal(perUnit, issuance->at(sfOutstandingAmount));

    // Move the coupon out of the payer's spendable balance. XRP is debited
    // directly; an IOU parks on its issuer's trust line and an MPT moves into
    // LockedAmount, the same custody the token escrow uses.
    if (total.native())
    {
        auto const sle = view().peek(keylet::account(accountID_));
        if (!sle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        auto const balance = sle->at(sfBalance);
        if (balance < total)
            return tecINSUFFICIENT_FUNDS;
        sle->at(sfBalance) = balance - total;
        view().update(sle);
    }
    else if (total.holds<MPTIssue>())
    {
        if (auto const ter = lockEscrowMPT(view(), accountID_, total, j_); !isTesSuccess(ter))
            return ter;
    }
    else
    {
        auto const issuerID = total.getIssuer();
        if (issuerID == accountID_)
            return tecNO_PERMISSION;
        if (auto const ter = directSendNoFee(view(), accountID_, issuerID, total, true, j_);
            !isTesSuccess(ter))
            return ter;
    }

    STAmount const accruedPerUnit = schedule->at(sfAccruedPerUnit);
    STAmount const pool = schedule->at(sfPoolAmount);
    if (!canAdd(accruedPerUnit, perUnit) || !canAdd(pool, total))
        return tecPRECISION_LOSS;

    schedule->at(sfAccruedPerUnit) = accruedPerUnit + perUnit;
    schedule->at(sfPoolAmount) = pool + total;
    schedule->at(sfCouponCount) = schedule->at(sfCouponCount) + 1;
    schedule->at(sfLastCouponTime) =
        static_cast<std::uint32_t>(view().header().parentCloseTime.time_since_epoch().count());
    view().update(schedule);

    return tesSUCCESS;
}

void
CouponPay::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CouponPay::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
