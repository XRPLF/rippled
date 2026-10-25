#include <xrpl/tx/transactors/vault/VaultManagerCover.h>

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
VaultManagerCover::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultManagerCoverMask;
}

NotTEC
VaultManagerCover::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerCover::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultManagerCover::doApply()
{
    return tecINTERNAL;
}

void
VaultManagerCover::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultManagerCover::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
