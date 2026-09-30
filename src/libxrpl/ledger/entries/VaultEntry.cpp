#include <xrpl/ledger/entries/VaultEntry.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>

#include <expected>
#include <utility>

namespace xrpl {

template <typename ViewT>
VaultVersion
VaultEntry<ViewT>::version() const
{
    XRPL_ASSERT(
        *this && (*this)->getType() == ltVAULT, "xrpl::VaultEntry::version : valid Vault sle");
    if (!(*this)->isFieldPresent(sfLEVersion))
        return VaultVersion::Legacy;

    auto const version = (*this)->at(sfLEVersion);
    if (version > std::to_underlying(VaultVersion::CashBasis))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::VaultEntry::version : invalid vault version");
        return VaultVersion::Legacy;
        // LCOV_EXCL_STOP
    }
    return static_cast<VaultVersion>(version);
}

template <typename ViewT>
VaultKind
VaultEntry<ViewT>::kind() const
{
    XRPL_ASSERT(*this && (*this)->getType() == ltVAULT, "xrpl::VaultEntry::kind : valid Vault sle");
    auto const vaultKind = (*this)->at(~sfVaultKind);
    if (vaultKind && *vaultKind == std::to_underlying(VaultKind::ClosedEnded))
        return VaultKind::ClosedEnded;
    return VaultKind::OpenEnded;
}

template <typename ViewT>
VaultPhase
VaultEntry<ViewT>::phase() const
{
    XRPL_ASSERT(
        *this && (*this)->getType() == ltVAULT, "xrpl::VaultEntry::phase : valid Vault sle");
    return getVaultPhase(
        this->readView(),
        (**this)[~sfVaultKind],
        (**this)[~sfSubscriptionDate],
        (**this)[~sfRedemptionDate]);
}

template <typename ViewT>
std::expected<STAmount, TER>
VaultEntry<ViewT>::clampToAssetsTotalScale(STAmount const& delta) const
{
    XRPL_ASSERT(
        delta.asset() == (*this)->at(sfAsset),
        "xrpl::VaultEntry::clampToAssetsTotalScale : delta and vault asset match");

    Asset const asset = (*this)->at(sfAsset);

    STAmount magnitude = delta.negative() ? -delta : delta;
    if (asset.integral())
    {
        return magnitude;
    }
    Number const assetsTotal = (*this)->at(sfAssetsTotal);

    // Calculate the scale after applying the delta using ToNearest rounding.
    // This aligns the delta with scale checks used by vault invariants.
    int const postScale = [&] {
        NumberRoundModeGuard const rg(Number::RoundingMode::ToNearest);
        return scale(assetsTotal + delta, asset);
    }();

    STAmount actualDelta;
    if (delta.negative())
    {
        // For withdrawals (debits), floor the magnitude to the target scale
        // to ensure exact grid alignment without paying out extra assets.
        actualDelta = roundToScale(magnitude, postScale, Number::RoundingMode::Downward);
    }
    else
    {
        // For deposits (credits), derive actualDelta from the floored posterior total.
        // This prevents grid alignment issues from crediting the vault more than deposited.
        //
        // Sum using Downward rounding so intermediate precision doesn't round up
        // and exceed the original requested amount.
        Number const posterior = [&] {
            NumberRoundModeGuard const rg(Number::RoundingMode::Downward);
            return assetsTotal + magnitude;
        }();

        Number const roundedPosterior =
            roundToAsset(asset, posterior, postScale, Number::RoundingMode::Downward);
        actualDelta = STAmount{asset, roundedPosterior - assetsTotal};
    }

    XRPL_ASSERT(
        abs(actualDelta) <= abs(delta),
        "xrpl::VaultEntry::clampToAssetsTotalScale : actual delta smaller or equal to calculated "
        "delta");

    // Reject changes below scale precision (1 ULP) to prevent share balance changes
    // without corresponding asset movements.
    if (actualDelta <= beast::kZero)
        return std::unexpected(tecPRECISION_LOSS);

    return actualDelta;
}

template <typename ViewT>
Number
VaultEntry<ViewT>::assetsTotalForWithdrawal(WaiveUnrealizedLoss waive) const
{
    Number assetTotal = (*this)->at(sfAssetsTotal);
    if (waive == WaiveUnrealizedLoss::No)
        assetTotal -= (*this)->at(sfLossUnrealized);
    return assetTotal;
}

template class VaultEntry<ReadView>;
template class VaultEntry<ApplyView>;

}  // namespace xrpl
