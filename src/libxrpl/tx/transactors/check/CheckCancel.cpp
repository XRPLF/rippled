#include <xrpl/tx/transactors/check/CheckCancel.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
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
    if (ctx.rules.enabled(fixCleanup3_3_0) && ctx.tx[sfCheckID] == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
CheckCancel::preclaim(PreclaimContext const& ctx)
{
    auto const sleCheck = ctx.view.read(keylet::check(ctx.tx[sfCheckID]));
    if (!sleCheck)
    {
        return {tecNO_ENTRY, "Check does not exist."};
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
            return {
                tecNO_PERMISSION,
                "Check is not expired and canceler is neither check source nor destination."};
        }
    }
    return tesSUCCESS;
}

TER
CheckCancel::doApply()
{
    auto const sleCheck = view().peek(keylet::check(ctx_.tx[sfCheckID]));
    if (!sleCheck)
    {
        // Error should have been caught in preclaim.
        return {tecNO_ENTRY, "Check does not exist."};
    }

    AccountID const srcId{sleCheck->getAccountID(sfAccount)};
    AccountID const dstId{sleCheck->getAccountID(sfDestination)};
    auto viewJ = ctx_.registry.get().getJournal("View");

    // If the check is not written to self (and it shouldn't be), remove the
    // check from the destination account root.
    if (srcId != dstId)
    {
        std::uint64_t const page{(*sleCheck)[sfDestinationNode]};
        if (!view().dirRemove(keylet::ownerDir(dstId), page, sleCheck->key(), true))
        {
            // LCOV_EXCL_START
            return {tefBAD_LEDGER, "Unable to delete check from destination."};
            // LCOV_EXCL_STOP
        }
    }
    {
        std::uint64_t const page{(*sleCheck)[sfOwnerNode]};
        if (!view().dirRemove(keylet::ownerDir(srcId), page, sleCheck->key(), true))
        {
            // LCOV_EXCL_START
            return {tefBAD_LEDGER, "Unable to delete check from owner."};
            // LCOV_EXCL_STOP
        }
    }

    // If we succeeded, update the check owner's reserve.
    decreaseOwnerCountForObject(view(), srcId, sleCheck, 1, viewJ);

    // Remove check from ledger.
    view().erase(sleCheck);
    return tesSUCCESS;
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
