#include <xrpl/tx/transactors/vault/VaultMark.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
VaultMark::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultMark::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultMark::doApply()
{
    return tecINTERNAL;
}

void
VaultMark::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultMark::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
