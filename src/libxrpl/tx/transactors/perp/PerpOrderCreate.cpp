#include <xrpl/tx/transactors/perp/PerpOrderCreate.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
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
PerpOrderCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpOrderCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpOrderCreateMask;
}

NotTEC
PerpOrderCreate::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfMarketID] == beast::kZero)
        return temMALFORMED;

    Number const price = tx[sfLimitPrice];
    Number const size = tx[sfSize];
    if (price.signum() <= 0 || size.signum() <= 0 || !perp::fitsQualityKey(price))
        return temMALFORMED;

    if (static_cast<int>(tx.isFlag(tfPerpPostOnly)) +
            static_cast<int>(tx.isFlag(tfImmediateOrCancel)) +
            static_cast<int>(tx.isFlag(tfFillOrKill)) >
        1)
        return temINVALID_FLAG;

    if (tx[~sfOfferSequence] == 0u)
        return temBAD_SEQUENCE;

    if (tx[~sfExpiration] == 0u)
        return temBAD_EXPIRATION;

    // A builder and its fee come together.
    if (tx.isFieldPresent(sfBuilder) != tx.isFieldPresent(sfBuilderFee) ||
        (tx.isFieldPresent(sfBuilder) && tx[sfBuilder] == beast::kZero) ||
        !validNumericRange(tx[~sfBuilderFee], perp::kMaxTradingFee))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpOrderCreate::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
PerpOrderCreate::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpOrderCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
