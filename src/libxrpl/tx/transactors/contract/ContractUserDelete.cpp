#include <xrpl/tx/transactors/contract/ContractUserDelete.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
ContractUserDelete::preflight(PreflightContext const& ctx)
{
    // ContractUserDelete is not implemented, so it is refused rather than applied as a no-op.
    return temDISABLED;
}

// Preflight refuses every ContractUserDelete, so nothing below runs.
// LCOV_EXCL_START
TER
ContractUserDelete::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
ContractUserDelete::doApply()
{
    return tesSUCCESS;
}

void
ContractUserDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
ContractUserDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}
// LCOV_EXCL_STOP

}  // namespace xrpl
