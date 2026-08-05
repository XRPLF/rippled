#include <xrpl/tx/transactors/contract/ContractClawback.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

NotTEC
ContractClawback::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
    if (flags & tfUniversalMask)
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
