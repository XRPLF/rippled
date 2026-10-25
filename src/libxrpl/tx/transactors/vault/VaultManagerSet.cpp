#include <xrpl/tx/transactors/vault/VaultManagerSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

std::uint32_t
VaultManagerSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultManagerSetMask;
}

NotTEC
VaultManagerSet::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerSet::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerSet::doApply()
{
    return tecINTERNAL;
}

void
VaultManagerSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
