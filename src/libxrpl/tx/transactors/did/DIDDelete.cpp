#include <xrpl/tx/transactors/did/DIDDelete.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
DIDDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
DIDDelete::deleteSLE(ApplyContext& ctx, Keylet sleKeylet, AccountID const owner)
{
    WritableSLE const sle{sleKeylet, ctx.view()};
    if (!sle)
        return tecNO_ENTRY;

    return DIDDelete::deleteSLE(ctx.view(), sle.mutableSle(), owner, ctx.journal);
}

TER
DIDDelete::deleteSLE(ApplyView& view, SLE::pointer sle, AccountID const owner, beast::Journal j)
{
    DIDEntry<ApplyView> did{sle, view, j};
    return did.destroy();
}

TER
DIDDelete::doApply()
{
    return deleteSLE(ctx_, keylet::did(accountID_), accountID_);
}

void
DIDDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
DIDDelete::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}
}  // namespace xrpl
