#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/tx/detail/CancelCheck.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
CancelCheck::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
CancelCheck::preclaim(PreclaimContext const& ctx)
{
    auto const sleCheck = ctx.view.read(keylet::check(ctx.tx[sfCheckID]));
    if (!sleCheck)
    {
        JLOG(ctx.j.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    using duration = NetClock::duration;
    using timepoint = NetClock::time_point;
    auto const optExpiry = (*sleCheck)[~sfExpiration];

    // Expiration is defined in terms of the close time of the parent
    // ledger, because we definitively know the time that it closed but
    // we do not know the closing time of the ledger that is under
    // construction.
    if (!optExpiry ||
        (ctx.view.parentCloseTime() < timepoint{duration{*optExpiry}}))
    {
        // If the check is not yet expired, then only the creator or the
        // destination may cancel the check.
        AccountID const acctId{ctx.tx[sfAccount]};
        if (acctId != (*sleCheck)[sfAccount] &&
            acctId != (*sleCheck)[sfDestination])
        {
            JLOG(ctx.j.warn()) << "Check is not expired and canceler is "
                                  "neither check source nor destination.";
            return tecNO_PERMISSION;
        }
    }
    return tesSUCCESS;
}

TER
CancelCheck::doApply()
{
    auto const sleCheck = view().peek(keylet::check(ctx_.tx[sfCheckID]));
    if (!sleCheck)
    {
        // Error should have been caught in preclaim.
        JLOG(j_.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    AccountID const srcId{sleCheck->getAccountID(sfAccount)};
    AccountID const dstId{sleCheck->getAccountID(sfDestination)};
    auto viewJ = ctx_.app.journal("View");

    // If the check is not written to self (and it shouldn't be), remove the
    // check from the destination account root.
    if (srcId != dstId)
    {
        std::uint64_t const page{(*sleCheck)[sfDestinationNode]};
        if (!view().dirRemove(
                keylet::ownerDir(dstId), page, sleCheck->key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete check from destination.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }
    {
        std::uint64_t const page{(*sleCheck)[sfOwnerNode]};
        if (!view().dirRemove(
                keylet::ownerDir(srcId), page, sleCheck->key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete check from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // If we succeeded, update the check owner's reserve.
    auto const sleSrc = view().peek(keylet::account(srcId));
    adjustOwnerCount(view(), sleSrc, -1, viewJ);

    // Remove check from ledger.
    view().erase(sleCheck);
    return tesSUCCESS;
}

}  // namespace ripple
