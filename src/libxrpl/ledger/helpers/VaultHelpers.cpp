#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

namespace xrpl {

[[nodiscard]] Number
vaultAccruedInterest(ReadView const& view, SLE::const_ref vault)
{
    Number const unearned = vault->at(sfUnearnedInterest);
    if (unearned <= Number{})
        return Number{};

    Number const rate = vault->at(sfAccrualRate);
    if (rate <= Number{})
        return Number{};

    // A rate that was never stamped has no measurable elapsed period; accruing
    // from the epoch would recognize the whole budget at once.
    std::uint32_t const stamped = vault->at(sfLastAccrualTime);
    if (stamped == 0)
        return Number{};

    auto const now = view.parentCloseTime().time_since_epoch().count();
    if (now <= stamped)
        return Number{};

    // Round down: never recognize more than certainly earned.
    NumberRoundModeGuard const guard(Number::RoundingMode::Downward);
    Number const earned = rate * Number{now - stamped};
    return earned >= unearned ? unearned : earned;
}

void
accrueVault(ApplyView& view, SLE::ref vault)
{
    Number const earned = vaultAccruedInterest(view, vault);
    if (earned > Number{})
    {
        vault->at(sfAssetsTotal) += earned;
        vault->at(sfUnearnedInterest) -= earned;
    }
    vault->at(sfLastAccrualTime) = view.parentCloseTime().time_since_epoch().count();
}

/* The vault's assets including interest earned since the last settlement.
 *
 * sfAssetsTotal only holds interest that has been recognized, so between loan
 * events it lags by the amount accrued since sfLastAccrualTime. Pricing adds
 * that back rather than writing it, which keeps sfAssetsTotal equal to the
 * vault's cash-plus-receivables and leaves the deposit/withdraw invariants
 * (which require sfAssetsTotal to move only with the vault balance) intact.
 *
 * Before featureLendingProtocolV1_1 the whole of a loan's interest is
 * recognized at origination, so sfAssetsTotal stands alone.
 */
static Number
netAssetsTotal(ReadView const& view, SLE::const_ref vault)
{
    Number const assetTotal = vault->at(sfAssetsTotal);
    if (!view.rules().enabled(featureLendingProtocolV1_1))
        return assetTotal;

    return assetTotal + vaultAccruedInterest(view, vault);
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    ReadView const& view,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    // Inside a struck window every deal converts at the one price.
    if (auto const struck = struckPriceInForce(view, vault))
        return STAmount{vault->at(sfShareMPTID), (Number{assets} / *struck).truncate()};

    Number const assetTotal = netAssetsTotal(view, vault);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal <= Number{})
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
sharesToAssetsDeposit(
    ReadView const& view,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    if (auto const struck = struckPriceInForce(view, vault))
        return STAmount{vault->at(sfAsset), Number{shares} * *struck};

    Number const assetTotal = netAssetsTotal(view, vault);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal <= Number{})
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
    Number const assetsTotal = vault->at(sfAssetsTotal);

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
        "xrpl::clampToAssetsTotalScale : actual delta smaller or equal to calculated delta");

    // Reject changes below scale precision (1 ULP) to prevent share balance changes
    // without corresponding asset movements.
    if (actualDelta <= beast::kZero)
        return std::unexpected(tecPRECISION_LOSS);

    return actualDelta;
}

[[nodiscard]] Number
assetsTotalForWithdrawal(ReadView const& view, SLE::const_ref vault, WaiveUnrealizedLoss waive)
{
    Number assetTotal = netAssetsTotal(view, vault);
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
    ReadView const& view,
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

    if (auto const struck = struckPriceInForce(view, vault))
    {
        Number struckShares = Number{assets} / *struck;
        if (truncate == TruncateShares::Yes)
            struckShares = struckShares.truncate();
        return STAmount{vault->at(sfShareMPTID), struckShares};
    }

    Number const assetTotal = assetsTotalForWithdrawal(view, vault, waive);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal <= Number{})
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
    ReadView const& view,
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

    if (auto const struck = struckPriceInForce(view, vault))
        return STAmount{vault->at(sfAsset), Number{shares} * *struck};

    Number const assetTotal = assetsTotalForWithdrawal(view, vault, waive);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal <= Number{})
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
    if (version > std::to_underlying(VaultVersion::CashBasis))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::getVaultVersion : invalid vault version");
        return VaultVersion::Legacy;
        // LCOV_EXCL_STOP
    }
    return static_cast<VaultVersion>(version);
}

namespace {

[[nodiscard]] VaultKind
decodeVaultKind(std::optional<std::uint8_t> vaultKind)
{
    if (vaultKind && *vaultKind == std::to_underlying(VaultKind::ClosedEnded))
        return VaultKind::ClosedEnded;
    if (vaultKind && *vaultKind == std::to_underlying(VaultKind::Rolling))
        return VaultKind::Rolling;
    return VaultKind::OpenEnded;
}

}  // namespace

namespace {

/**
 * Seconds from the first window's open to this close time, or nullopt when the
 * vault is not rolling or the first window has not opened yet.
 */
[[nodiscard]] std::optional<std::uint64_t>
sinceFirstWindow(ReadView const& view, SLE::const_ref vault)
{
    if (decodeVaultKind(vault->at(~sfVaultKind)) != VaultKind::Rolling)
        return std::nullopt;

    auto const start = vault->at(~sfSubscriptionDate);
    auto const interval = vault->at(~sfDealingInterval);
    if (!start || !interval || *interval == 0)
        return std::nullopt;

    auto const now = view.header().parentCloseTime.time_since_epoch().count();
    if (now < *start)
        return std::nullopt;

    return static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(*start);
}

}  // namespace

[[nodiscard]] bool
inDealingWindow(ReadView const& view, SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::inDealingWindow : valid Vault sle");

    if (decodeVaultKind(vault->at(~sfVaultKind)) != VaultKind::Rolling)
        return true;

    auto const elapsed = sinceFirstWindow(view, vault);
    if (!elapsed)
        return false;

    return *elapsed % vault->at(sfDealingInterval) < vault->at(sfDealingWindow);
}

[[nodiscard]] std::uint32_t
dealingWindowEnd(ReadView const& view, SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::dealingWindowEnd : valid Vault sle");

    auto const elapsed = sinceFirstWindow(view, vault);
    if (!elapsed)
        return 0;

    std::uint64_t const interval = vault->at(sfDealingInterval);
    std::uint64_t const opened = *elapsed - (*elapsed % interval);
    return static_cast<std::uint32_t>(
        vault->at(sfSubscriptionDate) + opened + vault->at(sfDealingWindow));
}

[[nodiscard]] std::uint8_t
getAccountingMethod(SLE::const_ref vault)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::getAccountingMethod : valid Vault sle");

    if (auto const method = vault->at(~sfAccountingMethod))
        return *method;

    return vault->at(sfLEVersion) == std::to_underlying(VaultVersion::CashBasis)
        ? kVaultAccountingCash
        : kVaultAccountingLegacy;
}

[[nodiscard]] std::optional<Number>
struckPriceInForce(ReadView const& view, SLE::const_ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::struckPriceInForce : valid Vault sle");

    if (decodeVaultKind(vault->at(~sfVaultKind)) != VaultKind::Rolling)
        return std::nullopt;
    if (!inDealingWindow(view, vault))
        return std::nullopt;

    // A stamp from an earlier window does not govern this one.
    if (vault->at(sfStruckUntil) != dealingWindowEnd(view, vault))
        return std::nullopt;

    Number const price = vault->at(sfStruckPrice);
    if (price <= Number{})
        return std::nullopt;

    return price;
}

void
strikeWindowPrice(ApplyView& view, SLE::ref vault, SLE::const_ref issuance)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::strikeWindowPrice : valid Vault sle");

    if (decodeVaultKind(vault->at(~sfVaultKind)) != VaultKind::Rolling)
        return;
    if (!inDealingWindow(view, vault))
        return;

    auto const windowEnd = dealingWindowEnd(view, vault);
    if (vault->at(sfStruckUntil) == windowEnd)
        return;  // already struck for this window

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    if (shareTotal <= Number{})
        return;  // no shares yet, so nothing to price against

    Number const assetTotal = netAssetsTotal(view, vault) - vault->at(sfLossUnrealized);
    if (assetTotal <= Number{})
        return;

    vault->at(sfStruckPrice) = assetTotal / shareTotal;
    vault->at(sfStruckUntil) = windowEnd;
    view.update(vault);
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
        *kindField == std::to_underlying(VaultKind::ClosedEnded) ||
        *kindField == std::to_underlying(VaultKind::Rolling);
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
