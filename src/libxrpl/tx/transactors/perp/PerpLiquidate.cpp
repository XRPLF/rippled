#include <xrpl/tx/transactors/perp/PerpLiquidate.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

bool
PerpLiquidate::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpLiquidate::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpLiquidateMask;
}

NotTEC
PerpLiquidate::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx.isFlag(tfPerpLiquidateToBook) == tx.isFlag(tfPerpLiquidateTakeover))
        return temINVALID_FLAG;

    if (tx[sfMarginAccountID] == beast::kZero || tx[sfMarketID] == beast::kZero)
        return temMALFORMED;

    if (auto const size = tx[~sfSize]; size && size->signum() <= 0)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpLiquidate::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
PerpLiquidate::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpLiquidate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
