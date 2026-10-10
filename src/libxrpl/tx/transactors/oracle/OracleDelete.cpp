#include <xrpl/tx/transactors/oracle/OracleDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/entries/OracleEntry.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
OracleDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
OracleDelete::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.exists(keylet::account(ctx.tx.getAccountID(sfAccount))))
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    auto const sle =
        OracleEntryR(ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID], ctx.view);
    if (!sle)
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: Oracle does not exist.";
        return tecNO_ENTRY;
    }

    if (ctx.tx.getAccountID(sfAccount) != sle->getAccountID(sfOwner))
    {
        // this can't happen because of the above check
        // LCOV_EXCL_START
        JLOG(ctx.j.debug()) << "Oracle Delete: invalid account.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }
    return tesSUCCESS;
}

TER
OracleDelete::doApply()
{
    if (auto oracle = OracleEntryW(accountID_, ctx_.tx[sfOracleDocumentID], ctx_.view(), j_))
        return oracle.removeFromLedger(accountID_);

    return tecINTERNAL;  // LCOV_EXCL_LINE
}

void
OracleDelete::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
    // No transaction-specific invariants yet (future work).
}

bool
OracleDelete::finalizeInvariants(
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
