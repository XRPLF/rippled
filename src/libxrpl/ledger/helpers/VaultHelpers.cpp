#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

namespace xrpl {

namespace {

[[nodiscard]] int
fixedBaseScale(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::fixedBaseScale : valid Vault sle");
    if (vault->at(sfAsset).integral())
        return 0;
    return -static_cast<int>(vault->at(sfScale));
}

[[nodiscard]] int
liveScale(Number const& reference, Asset const& asset, int baseScale)
{
    if (reference == beast::kZero)
        return baseScale;
    return std::max(baseScale, scale(reference, asset));
}

[[nodiscard]] VaultKind
decodeVaultKind(std::optional<std::uint8_t> vaultKind)
{
    if (vaultKind && *vaultKind == std::to_underlying(VaultKind::ClosedEnded))
        return VaultKind::ClosedEnded;
    return VaultKind::OpenEnded;
}

}  // namespace

[[nodiscard]] int
getVaultScale(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultScale : valid Vault sle");

    switch (getVaultVersion(vault))
    {
        case VaultVersion::Legacy:
        case VaultVersion::CashBasis:
            return scale(vault->at(sfAssetsTotal), vault->at(sfAsset));
        case VaultVersion::FixedPrecision:
            return liveScale(vault->at(sfAssetsTotal), vault->at(sfAsset), fixedBaseScale(vault));
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::getVaultScale : valid VaultVersion");
    return Number::kMinExponent - 1;
    // LCOV_EXCL_STOP
}

[[nodiscard]] int
getVaultBaseScale(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultBaseScale : valid Vault sle");

    switch (getVaultVersion(vault))
    {
        case VaultVersion::Legacy:
        case VaultVersion::CashBasis:
            return getVaultScale(vault);
        case VaultVersion::FixedPrecision:
            return fixedBaseScale(vault);
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::getVaultBaseScale : valid VaultVersion");
    return Number::kMinExponent - 1;
    // LCOV_EXCL_STOP
}

[[nodiscard]] int
getPosteriorVaultScale(SLE::const_ref vault, STAmount const& delta)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::getPosteriorVaultScale : valid Vault sle");
    XRPL_ASSERT(
        delta.asset() == vault->at(sfAsset),
        "xrpl::getPosteriorVaultScale : delta and Vault asset match");

    Number const posterior = [&] {
        NumberRoundModeGuard const rg(Number::RoundingMode::ToNearest);
        return vault->at(sfAssetsTotal) + delta;
    }();

    switch (getVaultVersion(vault))
    {
        case VaultVersion::Legacy:
        case VaultVersion::CashBasis:
            return scale(posterior, vault->at(sfAsset));
        case VaultVersion::FixedPrecision:
            return liveScale(posterior, vault->at(sfAsset), fixedBaseScale(vault));
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::getPosteriorVaultScale : valid VaultVersion");
    return Number::kMinExponent - 1;
    // LCOV_EXCL_STOP
}

[[nodiscard]] STAmount
roundToVaultScale(SLE::const_ref vault, STAmount const& amount, Number::RoundingMode roundingMode)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::roundToVaultScale : valid Vault sle");
    XRPL_ASSERT(
        amount.asset() == vault->at(sfAsset),
        "xrpl::roundToVaultScale : amount and Vault asset match");
    if (amount.integral())
        return amount;
    return roundToScale(amount, getVaultScale(vault), roundingMode);
}

[[nodiscard]] STAmount
roundToPosteriorVaultScale(
    SLE::const_ref vault,
    STAmount const& amount,
    Number::RoundingMode roundingMode)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::roundToPosteriorVaultScale : valid Vault sle");
    XRPL_ASSERT(
        amount.asset() == vault->at(sfAsset),
        "xrpl::roundToPosteriorVaultScale : amount and Vault asset match");
    if (amount.integral())
        return amount;
    return roundToScale(amount, getPosteriorVaultScale(vault, amount), roundingMode);
}

[[nodiscard]] Number
getVaultOpenLimit(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultOpenLimit : valid Vault sle");
    XRPL_ASSERT(
        getVaultVersion(vault) == VaultVersion::FixedPrecision,
        "xrpl::getVaultOpenLimit : FixedPrecision Vault");
    return Number{9, 15 + getVaultBaseScale(vault)};
}

[[nodiscard]] TER
checkOptionalVaultInflow(SLE::const_ref vault, STAmount const& amount)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::checkOptionalVaultInflow : valid Vault sle");
    XRPL_ASSERT(
        amount.asset() == vault->at(sfAsset),
        "xrpl::checkOptionalVaultInflow : amount and Vault asset match");
    XRPL_ASSERT(!amount.negative(), "xrpl::checkOptionalVaultInflow : non-negative amount");
    if (getVaultVersion(vault) != VaultVersion::FixedPrecision)
        return tesSUCCESS;

    STAmount const rounded =
        roundToPosteriorVaultScale(vault, amount, Number::RoundingMode::TowardsZero);
    int const baseScale = getVaultBaseScale(vault);
    // Keep this explicit even though a non-negative YieldUnrealized makes the
    // Open-zone capacity ceiling reject every coarsening transition too. The
    // protocol defines both conditions independently.
    if (getPosteriorVaultScale(vault, rounded) != baseScale)
        return tecLIMIT_EXCEEDED;

    Number const capacity = [&] {
        NumberRoundModeGuard const rg(Number::RoundingMode::TowardsZero);
        return vault->at(sfAssetsTotal) + vault->at(sfYieldUnrealized) + rounded;
    }();
    if (capacity > getVaultOpenLimit(vault))
        return tecLIMIT_EXCEEDED;
    return tesSUCCESS;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
    {
        return STAmount{
            shares.asset(),
            Number(assets.mantissa(), assets.exponent() + vault->at(sfScale)).truncate()};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    shares = ((shareTotal * assets) / assetTotal).truncate();
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
    {
        return STAmount{
            assets.asset(), shares.mantissa(), shares.exponent() - vault->at(sfScale), false};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

[[nodiscard]] std::expected<STAmount, TER>
clampToAssetsTotalScale(SLE::const_ref vault, STAmount const& delta)
{
    XRPL_ASSERT(
        delta.asset() == vault->at(sfAsset),
        "xrpl::clampToAssetsTotalScale : delta and vault asset match");

    Asset const asset = vault->at(sfAsset);

    STAmount magnitude = delta.negative() ? -delta : delta;
    if (asset.integral())
    {
        return magnitude;
    }

    STAmount actualDelta;
    if (getVaultVersion(vault) == VaultVersion::FixedPrecision)
    {
        STAmount const rounded =
            roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero);
        actualDelta = rounded.negative() ? -rounded : rounded;
    }
    else
    {
        Number const assetsTotal = vault->at(sfAssetsTotal);

        // Calculate the scale after applying the delta using ToNearest rounding.
        // This aligns the delta with scale checks used by vault invariants.
        int const postScale = [&] {
            NumberRoundModeGuard const rg(Number::RoundingMode::ToNearest);
            return scale(assetsTotal + delta, asset);
        }();

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
    }

    XRPL_ASSERT(
        abs(actualDelta) <= abs(delta),
        "xrpl::clampToAssetsTotalScale : actual delta smaller or equal to calculated delta");

    // Reject changes below scale precision (1 ULP) to prevent share balance changes
    // without corresponding asset movements.
    if (actualDelta <= beast::kZero)
        return std::unexpected(tecPRECISION_LOSS);

    return actualDelta;
}

[[nodiscard]] Number
assetsTotalForWithdrawal(SLE::const_ref vault, WaiveUnrealizedLoss waive)
{
    Number assetTotal = vault->at(sfAssetsTotal);
    if (waive == WaiveUnrealizedLoss::No)
        assetTotal -= vault->at(sfLossUnrealized);
    return assetTotal;
}

[[nodiscard]] bool
debitIsNonZeroDust(Asset const& asset, Number const& total, Number const& amount)
{
    if (amount == 0)
        return false;
    return STAmount{asset, total - amount} == STAmount{asset, total};
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate,
    WaiveUnrealizedLoss waive)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesWithdraw : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = assetsTotalForWithdrawal(vault, waive);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return shares;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    Number result = (shareTotal * assets) / assetTotal;
    if (truncate == TruncateShares::Yes)
        result = result.truncate();
    shares = result;
    return shares;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsWithdraw : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsWithdraw : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = assetsTotalForWithdrawal(vault, waive);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return assets;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

[[nodiscard]] bool
isSoleShareholder(ReadView const& view, AccountID const& account, SLE::const_ref issuance)
{
    XRPL_ASSERT(
        issuance && issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::isSoleShareholder : valid issuance SLE");

    std::uint64_t const outstanding = issuance->at(sfOutstandingAmount);
    if (outstanding == 0)
        return false;

    auto const shareMPTID =
        makeMptID(issuance->getFieldU32(sfSequence), issuance->getAccountID(sfIssuer));
    auto const sleToken = view.read(keylet::mptoken(shareMPTID, account));
    if (!sleToken)
        return false;  // LCOV_EXCL_LINE

    return sleToken->getFieldU64(sfMPTAmount) == outstanding;
}

[[nodiscard]] VaultVersion
getVaultVersion(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultVersion : valid Vault sle");
    if (!vault->isFieldPresent(sfLEVersion))
        return VaultVersion::Legacy;

    auto const version = vault->at(sfLEVersion);
    if (version > std::to_underlying(VaultVersion::FixedPrecision))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::getVaultVersion : invalid vault version");
        return VaultVersion::Legacy;
        // LCOV_EXCL_STOP
    }
    return static_cast<VaultVersion>(version);
}

[[nodiscard]] VaultKind
getVaultKind(SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultKind : valid Vault sle");
    return decodeVaultKind(vault->at(~sfVaultKind));
}

[[nodiscard]] VaultKind
getVaultKind(STTx const& tx)
{
    return decodeVaultKind(tx[~sfVaultKind]);
}

[[nodiscard]] bool
isValidVaultKind(STTx const& tx)
{
    auto const kindField = tx[~sfVaultKind];
    if (!kindField)
        return true;
    return *kindField == std::to_underlying(VaultKind::OpenEnded) ||
        *kindField == std::to_underlying(VaultKind::ClosedEnded);
}

[[nodiscard]] bool
isValidClosedEndedGap(std::uint32_t sub, std::uint32_t red)
{
    auto const s = static_cast<std::int64_t>(sub);
    auto const r = static_cast<std::int64_t>(red);
    return r >= s + kMinInvestmentPeriod && r < s + kMaxInvestmentPeriod;
}

[[nodiscard]] VaultPhase
getVaultPhase(ReadView const& view, SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultPhase : valid Vault sle");
    return getVaultPhase(
        view, (*vault)[~sfVaultKind], (*vault)[~sfSubscriptionDate], (*vault)[~sfRedemptionDate]);
}

[[nodiscard]] VaultPhase
getVaultPhase(
    ReadView const& view,
    std::optional<std::uint8_t> vaultKind,
    std::optional<std::uint32_t> subscriptionDate,
    std::optional<std::uint32_t> redemptionDate)
{
    if (!vaultKind || *vaultKind != std::to_underlying(VaultKind::ClosedEnded))
        return VaultPhase::NoPhase;

    // Subscription includes now == SubscriptionDate; Investment starts
    // strictly after SubscriptionDate.
    if (!hasExpired(view, subscriptionDate, ExpiryComparison::Exclusive))
        return VaultPhase::Subscription;
    if (!hasExpired(view, redemptionDate))
        return VaultPhase::Investment;
    return VaultPhase::Redemption;
}

[[nodiscard]] TER
checkVaultDomain(
    ReadView const& view,
    SLE::const_ref issuance,
    AccountID const& subject,
    SuppressExpired suppressExpired)
{
    XRPL_ASSERT(
        issuance && issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::checkVaultDomain : valid issuance SLE");

    auto const maybeDomainID = issuance->at(~sfDomainID);
    if (!maybeDomainID)
        return tecNO_AUTH;

    auto const err = credentials::validDomain(view, *maybeDomainID, subject);
    if (err == tecEXPIRED && suppressExpired == SuppressExpired::Yes)
        return tesSUCCESS;

    return err;
}

}  // namespace xrpl
