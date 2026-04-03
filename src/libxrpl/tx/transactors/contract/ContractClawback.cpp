#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/contract/ContractClawback.h>

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

}  // namespace xrpl
