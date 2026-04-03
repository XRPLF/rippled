#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/contract/ContractUserDelete.h>

namespace xrpl {

NotTEC
ContractUserDelete::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
    if (flags & tfUniversalMask)
    {
        JLOG(ctx.j.trace()) << "ContractUserDelete: tfUniversalMask is not allowed.";
        return temINVALID_FLAG;
    }

    return tesSUCCESS;
}

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

}  // namespace xrpl
