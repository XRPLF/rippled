#include <xrpl/tx/transactors/coupon/CouponScheduleSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

NotTEC
CouponScheduleSet::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfMPTokenIssuanceID] == beast::kZero)
        return temMALFORMED;

    if (ctx.tx[sfExpiration] == 0)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
CouponScheduleSet::preclaim(PreclaimContext const& ctx)
{
    auto const schedule = ctx.view.read(keylet::couponSchedule(ctx.tx[sfMPTokenIssuanceID]));
    if (!schedule)
        return tecNO_ENTRY;

    if (ctx.tx[sfAccount] != schedule->at(sfOwner))
        return tecNO_PERMISSION;

    // Non-callable schedules are fully immutable.
    auto const notice = schedule->at(~sfCallNoticePeriod);
    if (!notice)
    {
        JLOG(ctx.j.debug()) << "CouponScheduleSet: schedule is not callable.";
        return tecNO_PERMISSION;
    }

    std::int64_t const newExpiration = ctx.tx[sfExpiration];
    std::int64_t const now = ctx.view.header().parentCloseTime.time_since_epoch().count();

    // The notice floor: a call never takes effect with less warning than
    // the schedule declared at creation.
    if (newExpiration < now + static_cast<std::int64_t>(*notice))
        return tecNO_PERMISSION;

    // Call protection.
    if (auto const earliestCall = schedule->at(~sfEarliestCallTime);
        earliestCall && newExpiration < static_cast<std::int64_t>(*earliestCall))
        return tecNO_PERMISSION;

    // Shorten-only: a call can never extend the coupon liability. A
    // perpetual callable schedule may have Expiration set for the first
    // time.
    if (auto const expiration = schedule->at(~sfExpiration);
        expiration && newExpiration >= static_cast<std::int64_t>(*expiration))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
CouponScheduleSet::doApply()
{
    auto schedule = view().peek(keylet::couponSchedule(ctx_.tx[sfMPTokenIssuanceID]));
    if (!schedule)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    schedule->at(sfExpiration) = ctx_.tx[sfExpiration];
    view().update(schedule);

    return tesSUCCESS;
}

void
CouponScheduleSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CouponScheduleSet::finalizeInvariants(
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
