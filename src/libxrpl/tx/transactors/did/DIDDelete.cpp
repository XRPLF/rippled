#include <xrpl/tx/transactors/did/DIDDelete.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/DIDEntry.h>
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
DIDDelete::doApply()
{
    DIDEntryW sleDID(accountID_, ctx_.view(), ctx_.journal);
    if (!sleDID)
        return tecNO_ENTRY;

    return sleDID.removeFromLedger(accountID_);
}

void
DIDDelete::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
