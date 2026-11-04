#include <xrpl/tx/transactors/perp/PerpRefresh.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

bool
PerpRefresh::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpRefresh::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpRefreshMask;
}

NotTEC
PerpRefresh::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const marketID = tx[~sfMarketID];
    auto const marginAccountID = tx[~sfMarginAccountID];
    if (!marketID && !marginAccountID)
        return temMALFORMED;

    if ((marketID && *marketID == beast::kZero) ||
        (marginAccountID && *marginAccountID == beast::kZero))
        return temMALFORMED;

    // Settling a stale market needs the market.
    if (tx.isFlag(tfPerpSettleStale) && !marketID)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpRefresh::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
PerpRefresh::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpRefresh::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
