#include <xrpl/tx/transactors/vault/VaultManagerClaim.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
VaultManagerClaim::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerClaim::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerClaim::doApply()
{
    return tecINTERNAL;
}

void
VaultManagerClaim::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerClaim::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
