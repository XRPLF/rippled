#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

namespace xrpl {

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    Number const& classTotal)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount shares{MPTIssue{makeMptID(issuance->at(sfSequence), issuance->at(sfIssuer))}};
    if (classTotal == 0)
    {
        return STAmount{
            shares.asset(),
            Number(assets.mantissa(), assets.exponent() + vault->at(sfScale)).truncate()};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    shares = ((shareTotal * assets) / classTotal).truncate();
    return shares;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets)
{
    return assetsToSharesDeposit(vault, issuance, assets, vault->at(sfAssetsTotal));
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    Number const& classTotal)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsDeposit : non-negative shares");
    MPTIssue const share{makeMptID(issuance->at(sfSequence), issuance->at(sfIssuer))};
    XRPL_ASSERT(shares.asset() == share, "xrpl::sharesToAssetsDeposit : shares and issuance match");
    if (shares.negative() || shares.asset() != share)
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount assets{vault->at(sfAsset)};
    if (classTotal == 0)
    {
        return STAmount{
            assets.asset(), shares.mantissa(), shares.exponent() - vault->at(sfScale), false};
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (classTotal * shares) / shareTotal;
    return assets;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    return sharesToAssetsDeposit(vault, issuance, shares, vault->at(sfAssetsTotal));
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
    Number const& classTotal)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesWithdraw : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount shares{MPTIssue{makeMptID(issuance->at(sfSequence), issuance->at(sfIssuer))}};
    if (classTotal == 0)
        return shares;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    Number result = (shareTotal * assets) / classTotal;
    if (truncate == TruncateShares::Yes)
        result = result.truncate();
    shares = result;
    return shares;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate,
    WaiveUnrealizedLoss waive)
{
    return assetsToSharesWithdraw(
        vault, issuance, assets, truncate, assetsTotalForWithdrawal(vault, waive));
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    Number const& classTotal)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsWithdraw : non-negative shares");
    MPTIssue const share{makeMptID(issuance->at(sfSequence), issuance->at(sfIssuer))};
    XRPL_ASSERT(
        shares.asset() == share, "xrpl::sharesToAssetsWithdraw : shares and issuance match");
    if (shares.negative() || shares.asset() != share)
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount assets{vault->at(sfAsset)};
    if (classTotal == 0)
        return assets;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (classTotal * shares) / shareTotal;
    return assets;
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive)
{
    return sharesToAssetsWithdraw(vault, issuance, shares, assetsTotalForWithdrawal(vault, waive));
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
    return VaultKind::OpenEnded;
}

}  // namespace

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

[[nodiscard]] std::optional<VaultClass>
vaultClassOf(SLE::const_ref vault, MPTID const& share)
{
    if (share == vault->at(sfShareMPTID))
        return VaultClass{.share = share, .tranche = std::nullopt};
    if (!vault->isFieldPresent(sfTranches))
        return std::nullopt;
    auto const& tranches = vault->getFieldArray(sfTranches);
    for (std::size_t i = 0; i < tranches.size(); ++i)
    {
        if (tranches[i][sfShareMPTID] == share)
            return VaultClass{.share = share, .tranche = i};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<VaultClass>
vaultClassForTx(SLE::const_ref vault, STTx const& tx)
{
    std::optional<VaultClass> fromAmount;
    if (auto const amount = tx[~sfAmount]; amount && amount->holds<MPTIssue>())
        fromAmount = vaultClassOf(vault, amount->get<MPTIssue>().getMptID());

    if (auto const share = tx[~sfShareMPTID])
    {
        auto const named = vaultClassOf(vault, *share);
        if (!named || (fromAmount && fromAmount->share != named->share))
            return std::nullopt;
        return named;
    }
    if (fromAmount)
        return fromAmount;
    return VaultClass{.share = vault->at(sfShareMPTID), .tranche = std::nullopt};
}

[[nodiscard]] Number
trancheClaim(STObject const& tranche, std::uint32_t accrualTime, NetClock::time_point now)
{
    Number const principal = tranche[sfPrincipalOutstanding];
    Number claim = principal + tranche[sfInterestAccrued];
    auto const elapsed = now.time_since_epoch().count();
    if (elapsed > accrualTime)
    {
        TenthBips32 const rate{tranche[sfInterestRate]};
        claim += tenthBipsOfValue(principal * Number(elapsed - accrualTime), rate) / kSecondsInYear;
    }
    return claim;
}

[[nodiscard]] std::vector<Number>
vaultClassValues(SLE::const_ref vault, NetClock::time_point now, Number const& gross)
{
    std::vector<Number> values;
    Number remaining = gross > beast::kZero ? gross : Number{};
    if (vault->isFieldPresent(sfTranches))
    {
        std::uint32_t const accrualTime = (*vault)[~sfTrancheAccrualTime].value_or(0);
        for (auto const& tranche : vault->getFieldArray(sfTranches))
        {
            Number const value = std::min(trancheClaim(tranche, accrualTime, now), remaining);
            values.push_back(value);
            remaining -= value;
        }
    }
    values.push_back(remaining);
    return values;
}

[[nodiscard]] Number
vaultClassValue(
    SLE::const_ref vault,
    VaultClass const& vaultClass,
    NetClock::time_point now,
    Number const& gross)
{
    auto const values = vaultClassValues(vault, now, gross);
    return vaultClass.tranche ? values[*vaultClass.tranche] : values.back();
}

void
settleTranches(SLE::ref vault, NetClock::time_point now)
{
    if (!vault->isFieldPresent(sfTranches))
        return;
    SLE const& readable = *vault;
    std::uint32_t const accrualTime = readable[~sfTrancheAccrualTime].value_or(0);
    auto tranches = vault->getFieldArray(sfTranches);
    for (auto& tranche : tranches)
    {
        tranche[sfInterestAccrued] =
            trancheClaim(tranche, accrualTime, now) - tranche[sfPrincipalOutstanding];
    }
    vault->setFieldArray(sfTranches, tranches);
    auto const nowSeconds = static_cast<std::uint32_t>(now.time_since_epoch().count());
    vault->at(~sfTrancheAccrualTime) = std::max(accrualTime, nowSeconds);
}

[[nodiscard]] std::vector<Number>
trancheMargins(SLE::const_ref vault, NetClock::time_point now)
{
    std::vector<Number> margins;
    if (!vault->isFieldPresent(sfTranches))
        return margins;
    Number const gross = vault->at(sfAssetsTotal) - vault->at(sfLossUnrealized);
    std::uint32_t const accrualTime = (*vault)[~sfTrancheAccrualTime].value_or(0);
    Number claims;
    for (auto const& tranche : vault->getFieldArray(sfTranches))
    {
        claims += trancheClaim(tranche, accrualTime, now);
        TenthBips32 const allowed{kTenthBipsPerUnity.value() - tranche[sfSubordinationMinimum]};
        margins.push_back(tenthBipsOfValue(gross, allowed) - claims);
    }
    return margins;
}

[[nodiscard]] bool
trancheCoverageBreached(
    SLE::const_ref vault,
    std::vector<Number> const& before,
    NetClock::time_point now)
{
    if (vault->at(sfAssetsTotal) <= vault->at(sfAssetsAvailable))
        return false;
    auto const after = trancheMargins(vault, now);
    for (std::size_t i = 0; i < after.size() && i < before.size(); ++i)
    {
        if (after[i] < beast::kZero && after[i] < before[i])
            return true;
    }
    return false;
}

void
addTranchePrincipal(SLE::ref vault, std::size_t index, Number const& delta)
{
    auto tranches = vault->getFieldArray(sfTranches);
    auto& tranche = tranches[index];
    tranche[sfPrincipalOutstanding] = tranche[sfPrincipalOutstanding] + delta;
    vault->setFieldArray(sfTranches, tranches);
}

void
reduceTrancheClaim(
    SLE::ref vault,
    std::size_t index,
    Number const& redeemed,
    Number const& outstanding)
{
    auto tranches = vault->getFieldArray(sfTranches);
    auto& tranche = tranches[index];
    if (redeemed >= outstanding)
    {
        tranche[sfPrincipalOutstanding] = Number{};
        tranche[sfInterestAccrued] = Number{};
    }
    else
    {
        Number const principal = tranche[sfPrincipalOutstanding];
        Number const accrued = tranche[sfInterestAccrued];
        tranche[sfPrincipalOutstanding] = principal - principal * redeemed / outstanding;
        tranche[sfInterestAccrued] = accrued - accrued * redeemed / outstanding;
    }
    vault->setFieldArray(sfTranches, tranches);
}

[[nodiscard]] Number
classTotalForWithdrawal(
    SLE::const_ref vault,
    VaultClass const& vaultClass,
    NetClock::time_point now,
    WaiveUnrealizedLoss waive)
{
    Number const gross = assetsTotalForWithdrawal(vault, waive);
    if (!vault->isFieldPresent(sfTranches))
        return gross;
    return vaultClassValue(vault, vaultClass, now, gross);
}

[[nodiscard]] bool
isSoleHolderOfVault(ReadView const& view, AccountID const& account, SLE::const_ref vault)
{
    std::vector<MPTID> classes{vault->at(sfShareMPTID)};
    if (vault->isFieldPresent(sfTranches))
    {
        for (auto const& tranche : vault->getFieldArray(sfTranches))
            classes.push_back(tranche[sfShareMPTID]);
    }
    bool holdsAny = false;
    for (auto const& share : classes)
    {
        auto const issuance = view.read(keylet::mptokenIssuance(share));
        if (!issuance || issuance->at(sfOutstandingAmount) == 0)
            continue;
        if (!isSoleShareholder(view, account, issuance))
            return false;
        holdsAny = true;
    }
    return holdsAny;
}

}  // namespace xrpl
