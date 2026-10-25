#include <xrpl/tx/transactors/vault/VaultManagerDelete.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
VaultManagerDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerDelete::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerDelete::doApply()
{
    return tecINTERNAL;
}

void
VaultManagerDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
