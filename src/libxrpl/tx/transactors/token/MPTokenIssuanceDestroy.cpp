#include <xrpl/tx/transactors/token/MPTokenIssuanceDestroy.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/MPTokenIssuanceEntry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
MPTokenIssuanceDestroy::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
MPTokenIssuanceDestroy::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    MPTokenIssuanceEntryR const sleMPT(ctx.tx[sfMPTokenIssuanceID], ctx.view);
    if (!sleMPT)
        return tecOBJECT_NOT_FOUND;

    // ensure it is issued by the tx submitter
    if ((*sleMPT)[sfIssuer] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    // ensure it has no outstanding balances
    if ((*sleMPT)[sfOutstandingAmount] != 0)
        return tecHAS_OBLIGATIONS;

    if ((*sleMPT)[~sfLockedAmount].value_or(0) != 0)
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}

TER
MPTokenIssuanceDestroy::doApply()
{
    MPTokenIssuanceEntryW mpt(ctx_.tx[sfMPTokenIssuanceID], view(), j_);
    if (accountID_ != mpt->getAccountID(sfIssuer))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(keylet::ownerDir(accountID_), (*mpt)[sfOwnerNode], mpt->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(view(), accountID_, mpt.mutableRawSle(), 1, j_);
    mpt.erase();

    return tesSUCCESS;
}

void
MPTokenIssuanceDestroy::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
    // No transaction-specific invariants yet (future work).
}

bool
MPTokenIssuanceDestroy::finalizeInvariants(
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
