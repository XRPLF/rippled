#include <xrpl/tx/transactors/contract/ContractClawback.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
ContractClawback::preflight(PreflightContext const& ctx)
{
    // ContractClawback is not implemented, so it is refused rather than applied as a no-op.
    return temDISABLED;
}

// Preflight refuses every ContractClawback, so nothing below runs.
// LCOV_EXCL_START
TER
ContractClawback::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
ContractClawback::doApply()
{
    return tesSUCCESS;
}

void
ContractClawback::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
ContractClawback::finalizeInvariants(
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
