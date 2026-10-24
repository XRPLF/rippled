#include <xrpl/tx/transactors/contract/ContractClawback.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
ContractClawback::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
    if ((flags & tfUniversalMask) != 0u)
    {
        JLOG(ctx.j.trace()) << "ContractClawback: tfUniversalMask is not allowed.";
        return temINVALID_FLAG;
    }

    return tesSUCCESS;
}

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

}  // namespace xrpl
