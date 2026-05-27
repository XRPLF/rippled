#include <xrpl/tx/transactors/token/MPTokenIssuanceDestroy.h>

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
    auto const sleMPT = ctx.view.read(keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID]));
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
    auto const mpt = view().peek(keylet::mptIssuance(ctx_.tx[sfMPTokenIssuanceID]));
    if (accountID_ != mpt->getAccountID(sfIssuer))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(keylet::ownerDir(accountID_), (*mpt)[sfOwnerNode], mpt->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    view().erase(mpt);

    adjustOwnerCount(view(), view().peek(keylet::account(accountID_)), -1, j_);

    return tesSUCCESS;
}

void
MPTokenIssuanceDestroy::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
