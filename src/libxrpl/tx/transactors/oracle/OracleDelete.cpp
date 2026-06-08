#include <xrpl/tx/transactors/oracle/OracleDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
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
        ctx.view.read(keylet::oracle(ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID]));
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
OracleDelete::deleteOracle(
    ApplyView& view,
    SLE::ref sle,
    AccountID const& account,
    beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(keylet::ownerDir(account), (*sle)[sfOwnerNode], sle->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Oracle from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const count = sle->getFieldArray(sfPriceDataSeries).size() > 5 ? -2 : -1;

    adjustOwnerCount(view, sleOwner, count, j);

    view.erase(sle);

    return tesSUCCESS;
}

TER
OracleDelete::doApply()
{
    if (auto sle = ctx_.view().peek(keylet::oracle(accountID_, ctx_.tx[sfOracleDocumentID])))
        return deleteOracle(ctx_.view(), sle, accountID_, j_);

    return tecINTERNAL;  // LCOV_EXCL_LINE
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
