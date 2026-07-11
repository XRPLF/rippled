#include <xrpl/tx/transactors/check/CheckCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
namespace xrpl {

NotTEC
CheckCancel::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
CheckCancel::preclaim(PreclaimContext const& ctx)
{
    CheckEntry<ReadView> sleCheck{keylet::check(ctx.tx[sfCheckID]), ctx.view};
    if (!sleCheck)
    {
        JLOG(ctx.j.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    // Expiration is defined in terms of the close time of the parent
    // ledger, because we definitively know the time that it closed but
    // we do not know the closing time of the ledger that is under
    // construction.
    if (!hasExpired(ctx.view, (*sleCheck)[~sfExpiration]))
    {
        // If the check is not yet expired, then only the creator or the
        // destination may cancel the check.
        AccountID const acctId{ctx.tx[sfAccount]};
        if (acctId != (*sleCheck)[sfAccount] && acctId != (*sleCheck)[sfDestination])
        {
            JLOG(ctx.j.warn()) << "Check is not expired and canceler is "
                                  "neither check source nor destination.";
            return tecNO_PERMISSION;
        }
    }
    return tesSUCCESS;
}

TER
CheckCancel::doApply()
{
    CheckEntry<ApplyView> sleCheck{keylet::check(ctx_.tx[sfCheckID]), view()};
    if (!sleCheck)
    {
        // Error should have been caught in preclaim.
        JLOG(j_.warn()) << "Check does not exist.";
        return tecNO_ENTRY;
    }

    // Unlink the check from the source (and destination) directories, decrement
    // the source's OwnerCount (refunding any reserve sponsor), and erase it. See
    // CheckEntry::ownerDirs().
    return sleCheck.destroy();
}

void
CheckCancel::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CheckCancel::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
