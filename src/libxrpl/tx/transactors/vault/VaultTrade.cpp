#include <xrpl/tx/transactors/vault/VaultTrade.h>

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
VaultTrade::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultTradeMask;
}

NotTEC
VaultTrade::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultTrade::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultTrade::doApply()
{
    return tecINTERNAL;
}

void
VaultTrade::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
VaultTrade::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
