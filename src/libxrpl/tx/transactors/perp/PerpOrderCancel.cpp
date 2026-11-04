#include <xrpl/tx/transactors/perp/PerpOrderCancel.h>

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
PerpOrderCancel::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpOrderCancel::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
PerpOrderCancel::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfOfferSequence] == 0)
        return temBAD_SEQUENCE;

    if (auto const owner = ctx.tx[~sfOwner]; owner && *owner == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpOrderCancel::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
PerpOrderCancel::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpOrderCancel::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
