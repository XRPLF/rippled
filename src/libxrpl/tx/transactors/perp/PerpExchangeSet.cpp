#include <xrpl/tx/transactors/perp/PerpExchangeSet.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <array>
#include <cstdint>

namespace xrpl {

bool
PerpExchangeSet::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return false;

    return !ctx.tx.isFieldPresent(sfDomainID) || ctx.rules.enabled(featurePermissionedDomains);
}

std::uint32_t
PerpExchangeSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
PerpExchangeSet::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx.isFlag(tfInnerBatchTxn))
        return temINVALID_INNER_BATCH;

    if (!validDataLength(tx[~sfData], kMaxDataPayloadLength))
        return temMALFORMED;

    if (!validNumericRange(tx[~sfMakerFee], perp::kMaxTradingFee) ||
        !validNumericRange(tx[~sfTakerFee], perp::kMaxTradingFee) ||
        !validNumericRange(tx[~sfBuilderFeeMax], perp::kMaxTradingFee) ||
        !validNumericRange(tx[~sfOwnerFeeShare], perp::kRateUnity) ||
        !validNumericRange(tx[~sfVaultFeeShare], perp::kRateUnity) ||
        !validNumericRange(tx[~sfCoverRateMinimum], perp::kRateUnity) ||
        !validNumericRange(tx[~sfVaultLossLimit], perp::kRateUnity))
        return temMALFORMED;

    if (tx[~sfOwnerFeeShare].value_or(0) + tx[~sfVaultFeeShare].value_or(0) > perp::kRateUnity)
        return temMALFORMED;

    if (auto const exchangeID = tx[~sfExchangeID])
    {
        // An update may change only the mutable fields.
        std::array<SField const*, 6> const fixed{
            &sfAsset, &sfScale, &sfDomainID, &sfVaultID, &sfVaultLossWindow, &sfParameterDelay};
        for (auto const* field : fixed)
        {
            if (tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        if (*exchangeID == beast::kZero)
            return temMALFORMED;
        return tesSUCCESS;
    }

    if (!tx.isFieldPresent(sfAsset) || !tx.isFieldPresent(sfParameterDelay))
        return temMALFORMED;

    if (tx[sfParameterDelay] < perp::kMinParameterDelay.count())
        return temMALFORMED;

    // A backstop vault and its loss window come together.
    if (tx.isFieldPresent(sfVaultID) != tx.isFieldPresent(sfVaultLossWindow))
        return temMALFORMED;
    if (auto const vaultID = tx[~sfVaultID];
        vaultID && (*vaultID == beast::kZero || tx[sfVaultLossWindow] == 0))
        return temMALFORMED;

    if (auto const domainID = tx[~sfDomainID]; domainID && *domainID == beast::kZero)
        return temMALFORMED;

    if (auto const scale = tx[~sfScale])
    {
        auto const asset = tx[sfAsset];
        if (asset.native() || asset.holds<MPTIssue>())
            return temMALFORMED;

        auto const maximumScale = getVaultDefaultVersion(ctx.rules) == VaultVersion::FixedPrecision
            ? kFixedPrecisionVaultMaximumIouScale
            : kLegacyVaultMaximumIouScale;
        if (*scale > maximumScale)
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
PerpExchangeSet::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
PerpExchangeSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpExchangeSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
