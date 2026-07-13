#include <xrpl/tx/transactors/oracle/OracleDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/OracleHelpers.h>
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
OracleDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
OracleDelete::preclaim(PreclaimContext const& ctx)
{
    auto const sle = OracleEntry<ReadView>{
        ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID], ctx.view, ctx.j};

    if (!sle.exists())
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
OracleDelete::deleteOracle(
    ApplyView& view,
    SLE::ref sle,
    AccountID const& account,
    beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Unlink the Oracle from its owner's directory, decrement the owner's
    // OwnerCount by reserveCount() (1, or 2 for a large price-data series,
    // refunding any reserve sponsor), and erase it. See OracleEntry.
    OracleEntry<ApplyView> oracle{sle, view, j};
    return oracle.destroy();
}

TER
OracleDelete::doApply()
{
    auto const sle = OracleEntry<ApplyView>{accountID_, ctx_.tx[sfOracleDocumentID], ctx_.view()};
    if (!sle.exists())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    return deleteOracle(ctx_.view(), sle.mutableSle(), accountID_, j_);
}

void
OracleDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
