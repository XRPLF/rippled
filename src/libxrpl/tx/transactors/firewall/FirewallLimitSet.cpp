#include <xrpl/tx/transactors/firewall/FirewallLimitSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

bool
FirewallLimitSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureFirewall);
}

std::uint32_t
FirewallLimitSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfFirewallLimitSetMask;
}

XRPAmount
FirewallLimitSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx) + calculateCounterpartyFee(view, tx);
}

NotTEC
FirewallLimitSet::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
FirewallLimitSet::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
FirewallLimitSet::doApply()
{
    return tecINTERNAL;
}

void
FirewallLimitSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
FirewallLimitSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
