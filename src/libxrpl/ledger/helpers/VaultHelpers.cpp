#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>  // IWYU pragma: keep
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
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
fixedBaseScale(SLE::ConstRef vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::fixedBaseScale : valid Vault sle");
    Asset const asset = vault->at(sfAsset);
    // sfScale is never written for an integral asset (VaultCreate only sets
    // it when scale != 0, and integral assets always get scale 0), so it
    // must not be read here; vaultBaseScale ignores the scale argument for
    // an integral asset anyway.
    if (asset.integral())
        return vaultBaseScale(asset, 0);
    return vaultBaseScale(asset, vault->at(sfScale));
}

// Thin, vault-specific wrapper over posteriorAssetScale: used by
// getPosteriorVaultScale and creditToPosteriorAvailableScale.
[[nodiscard]] int
posteriorScale(SLE::ConstRef vault, Number const& reference, Number const& delta)
{
    return detail::posteriorAssetScale(
        getVaultVersion(vault), vault->at(sfAsset), fixedBaseScale(vault), reference, delta);
}

// FixedPrecision-only counterpart to clampToAssetsTotalScale, for cash
// outflows (VaultWithdraw, VaultClawback): rounds the magnitude of delta
// toward zero at AssetsAvailable's own posterior scale, the balance the cash
// actually leaves, instead of the derived AssetsTotal cache. Same
// tecPRECISION_LOSS and integral-asset and magnitude-never-exceeds-delta
// rules as clampToAssetsTotalScale. Not part of the public interface; reached
// only through clampVaultOutflow.
//
// For an outflow it is safe to round the delta directly, unlike the credit
// case: amount on the posterior grid of AssetsAvailable always leaves
// AssetsAvailable minus amount representable, since the posterior grid is
// exactly the grid AssetsAvailable itself will canonicalize to after the
// subtraction.
[[nodiscard]] std::expected<STAmount, TER>
clampToAvailableScale(SLE::ConstRef vault, STAmount const& delta)
{
    XRPL_ASSERT(
        delta.asset() == vault->at(sfAsset),
        "xrpl::clampToAvailableScale : delta and vault asset match");
    XRPL_ASSERT(
        getVaultVersion(vault) == VaultVersion::FixedPrecision,
        "xrpl::clampToAvailableScale : FixedPrecision Vault");
    XRPL_ASSERT(delta.negative(), "xrpl::clampToAvailableScale : outflow delta is negative");

    Asset const asset = vault->at(sfAsset);
    STAmount magnitude = delta.negative() ? -delta : delta;
    if (asset.integral())
        return magnitude;

    STAmount const rounded = roundToScale(
        delta,
        posteriorScale(vault, vault->at(sfAssetsAvailable), delta),
        Number::RoundingMode::TowardsZero);
    STAmount actualDelta = rounded.negative() ? -rounded : rounded;

    XRPL_ASSERT(
        abs(actualDelta) <= abs(delta),
        "xrpl::clampToAvailableScale : actual delta smaller or equal to calculated delta");

    // Reject changes below scale precision (1 ULP) to prevent share balance changes
    // without corresponding asset movements.
    if (actualDelta <= beast::kZero)
        return std::unexpected(tecPRECISION_LOSS);

    return actualDelta;
}

[[nodiscard]] VaultKind
decodeVaultKind(std::optional<std::uint8_t> vaultKind)
{
    if (vaultKind && *vaultKind == std::to_underlying(VaultKind::ClosedEnded))
        return VaultKind::ClosedEnded;
    return VaultKind::OpenEnded;
}

// Write cached AssetsTotal from getAssetsTotal, as the exact total rounded
// Downward to the Vault asset's 16-digit STAmount representation. Below
// coarsening this equals the exact total; above it, the cache is a
// deliberate floor of the exact value. Internal to adjustVaultBalances,
// the only writer of AssetsAvailable, AssetsDeployed, AssetsTotal,
// YieldUnrealized and LossUnrealized on a FixedPrecision Vault after
// creation; not part of the public interface.
void
syncAssetsTotal(SLE::Ref vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::syncAssetsTotal : valid Vault sle");
    Asset const asset = vault->at(sfAsset);

    // adjustVaultBalances writes AssetsAvailable as an already-canonical
    // STAmount before calling this, so the associateAsset pass a transactor
    // runs afterwards is always a no-op. If this fires, AssetsAvailable was
    // written some other way.
    XRPL_ASSERT(
        (STAmount{asset, vault->at(sfAssetsAvailable)} == vault->at(sfAssetsAvailable)),
        "xrpl::syncAssetsTotal : AssetsAvailable is a 16-digit STAmount value");

    NumberRoundModeGuard const rg(Number::RoundingMode::Downward);
    vault->at(sfAssetsTotal) = STAmount{asset, getAssetsTotal(vault)};
}

}  // namespace

namespace detail {

[[nodiscard]] int
liveScale(Number const& reference, Asset const& asset, int baseScale)
{
    if (reference == beast::kZero)
        return baseScale;
    // Round Downward, not the ambient mode: the live exponent must match the
    // exponent syncAssetsTotal actually stores (also Downward), regardless of
    // what rounding mode the caller's own arithmetic is using.
    NumberRoundModeGuard const rg(Number::RoundingMode::Downward);
    return std::max(baseScale, scale(reference, asset));
}

[[nodiscard]] int
posteriorAssetScale(
    VaultVersion version,
    Asset const& asset,
    int baseScale,
    Number const& reference,
    Number const& delta)
{
    // ToNearest for the sum and for scale()'s canonicalization of it, as the
    // Legacy/CashBasis clamp always did; liveScale sets its own mode.
    NumberRoundModeGuard const rg(Number::RoundingMode::ToNearest);
    Number const posterior = reference + delta;

    switch (version)
    {
        case VaultVersion::Legacy:
        case VaultVersion::CashBasis:
            return scale(posterior, asset);
        case VaultVersion::FixedPrecision:
            return liveScale(posterior, asset, baseScale);
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::detail::posteriorAssetScale : valid VaultVersion");
    return Number::kMinExponent - 1;
    // LCOV_EXCL_STOP
}

[[nodiscard]] STAmount
creditToPosteriorScale(
    Asset const& asset,
    Number const& reference,
    int atScale,
    Number const& raw,
    Number::RoundingMode roundingMode)
{
    // Floor the SUM (reference + raw), not just raw, at atScale. See
    // creditToPosteriorAvailableScale's doc: a delta floored on its own grid
    // can still leave a 17-digit sum once the reference has crossed a power
    // of ten.
    Number const flooredSum = roundToAsset(asset, reference + raw, atScale, roundingMode);
    // flooredSum - reference can carry 17 significant digits (the sum is on
    // the posterior grid, the reference on the finer one); build the
    // STAmount under roundingMode, not the caller's ambient mode, so the
    // credit this returns never exceeds raw.
    NumberRoundModeGuard const rg(roundingMode);
    return STAmount{asset, flooredSum - reference};
}

[[nodiscard]] int
getPosteriorVaultScale(SLE::ConstRef vault, STAmount const& delta)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT,
        "xrpl::detail::getPosteriorVaultScale : valid Vault sle");
    XRPL_ASSERT(
        delta.asset() == vault->at(sfAsset),
        "xrpl::detail::getPosteriorVaultScale : delta and Vault asset match");
    return posteriorScale(vault, getAssetsTotal(vault), delta);
}

}  // namespace detail

[[nodiscard]] Number
getAssetsTotal(SLE::ConstRef vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getAssetsTotal : valid Vault sle");

    if (getVaultVersion(vault) != VaultVersion::FixedPrecision)
        return vault->at(sfAssetsTotal);

    // AssetsAvailable is at -Scale (or coarser) and AssetsDeployed is always at
    // -Scale, so the sum is exact while it fits 19 digits. Fix the mode so a
    // sum that does not fit rounds the same way for every caller.
    NumberRoundModeGuard const rg(Number::RoundingMode::Downward);
    return vault->at(sfAssetsAvailable) + vault->at(sfAssetsDeployed);
}

[[nodiscard]] TER
adjustVaultBalances(SLE::Ref vault, VaultBalanceChange const& change, beast::Journal j)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::adjustVaultBalances : valid Vault sle");
    XRPL_ASSERT(
        getVaultVersion(vault) == VaultVersion::FixedPrecision,
        "xrpl::adjustVaultBalances : FixedPrecision Vault");
    Asset const asset = vault->at(sfAsset);
    STAmount const cash = change.cash.value_or(STAmount{asset, 0});
    XRPL_ASSERT(cash.asset() == asset, "xrpl::adjustVaultBalances : cash and Vault asset match");
    // This is the mode the ledger's own transfers (trust line / MPT balance
    // updates) normalize in; computing AA' with any other ambient mode would
    // not reproduce what the real transfer just did to the backing balance.
    XRPL_ASSERT(
        Number::getround() == Number::RoundingMode::ToNearest,
        "xrpl::adjustVaultBalances : ambient rounding mode is ToNearest");

    // AA' is the exact trust-line/MPT arithmetic the ledger uses for the
    // transfer itself: a Number sum re-canonicalized to the asset's 16-digit
    // STAmount precision. MPT and XRP sums are already integral, so this is
    // a no-op rounding for them.
    //
    // AssetsDeployed, LossUnrealized and YieldUnrealized move no cash, so they stay
    // exact Number sums; the asserts below check each fits 16 digits, rather
    // than letting an STAmount conversion round it silently.
    STAmount const availableAfter{asset, Number(vault->at(sfAssetsAvailable)) + Number(cash)};
    Number const assetsDeployedAfter = Number(vault->at(sfAssetsDeployed)) + change.deployed;
    Number const lossUnrealizedAfter = Number(vault->at(sfLossUnrealized)) + change.loss;
    Number const assetsReservedAfter = Number(vault->at(sfAssetsReserved)) + change.reserved;

    Number yieldUnrealizedAfter = Number(vault->at(sfYieldUnrealized)) + change.yield;

    if (availableAfter < beast::kZero || assetsDeployedAfter < beast::kZero ||
        lossUnrealizedAfter < beast::kZero || assetsReservedAfter < beast::kZero)
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "adjustVaultBalances: a balance would become negative."
                        << " AssetsAvailable: " << Number(availableAfter)
                        << ", AssetsDeployed: " << assetsDeployedAfter
                        << ", LossUnrealized: " << lossUnrealizedAfter
                        << ", AssetsReserved: " << assetsReservedAfter;
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    // The impairment rule: a Loan can only be impaired for as much as it
    // still owes. Reachable only through LoanManage's impair.
    if (lossUnrealizedAfter > assetsDeployedAfter)
        return tecLIMIT_EXCEEDED;

    if (yieldUnrealizedAfter < beast::kZero)
    {
        JLOG(j.warn()) << "adjustVaultBalances: YieldUnrealized would become negative: "
                       << yieldUnrealizedAfter << "; clamping to zero.";
        yieldUnrealizedAfter = kNumZero;
    }

    // Every caller derives principal from a Loan's own PrincipalOutstanding so the new total must
    // stay representable at the asset's own 16-digit precision too.
    XRPL_ASSERT(
        (STAmount{asset, assetsDeployedAfter} == assetsDeployedAfter),
        "xrpl::adjustVaultBalances : AssetsDeployed is a 16-digit STAmount value");
    XRPL_ASSERT(
        (STAmount{asset, lossUnrealizedAfter} == lossUnrealizedAfter),
        "xrpl::adjustVaultBalances : LossUnrealized is a 16-digit STAmount value");
    XRPL_ASSERT(
        (STAmount{asset, yieldUnrealizedAfter} == yieldUnrealizedAfter),
        "xrpl::adjustVaultBalances : YieldUnrealized is a 16-digit STAmount value");
    XRPL_ASSERT(
        (STAmount{asset, assetsReservedAfter} == assetsReservedAfter),
        "xrpl::adjustVaultBalances : AssetsReserved is a 16-digit STAmount value");

    vault->at(sfAssetsAvailable) = availableAfter;
    vault->at(sfAssetsDeployed) = assetsDeployedAfter;
    vault->at(sfLossUnrealized) = lossUnrealizedAfter;
    vault->at(sfYieldUnrealized) = yieldUnrealizedAfter;
    vault->at(sfAssetsReserved) = assetsReservedAfter;
    syncAssetsTotal(vault);

    return tesSUCCESS;
}

[[nodiscard]] int
getVaultScale(SLE::ConstRef vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultScale : valid Vault sle");

    Number const assetsTotal = getAssetsTotal(vault);
    switch (getVaultVersion(vault))
    {
        case VaultVersion::Legacy:
        case VaultVersion::CashBasis:
            return scale(assetsTotal, vault->at(sfAsset));
        case VaultVersion::FixedPrecision:
            return detail::liveScale(assetsTotal, vault->at(sfAsset), fixedBaseScale(vault));
    }
    // LCOV_EXCL_START
    UNREACHABLE("xrpl::getVaultScale : valid VaultVersion");
    return Number::kMinExponent - 1;
    // LCOV_EXCL_STOP
}

[[nodiscard]] int
getVaultBaseScale(SLE::ConstRef vault)
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

[[nodiscard]] STAmount
roundToPosteriorVaultScale(
    SLE::ConstRef vault,
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
    return roundToScale(amount, detail::getPosteriorVaultScale(vault, amount), roundingMode);
}

[[nodiscard]] STAmount
creditToPosteriorAvailableScale(
    SLE::ConstRef vault,
    Number const& raw,
    Number::RoundingMode roundingMode)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT,
        "xrpl::creditToPosteriorAvailableScale : valid Vault sle");
    Asset const asset = vault->at(sfAsset);
    if (asset.integral())
    {
        NumberRoundModeGuard const rg(roundingMode);
        return STAmount{asset, raw};
    }

    Number const reference = vault->at(sfAssetsAvailable);
    int const scale = posteriorScale(vault, reference, raw);
    return detail::creditToPosteriorScale(asset, reference, scale, raw, roundingMode);
}

[[nodiscard]] Number
getVaultOpenLimit(SLE::ConstRef vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultOpenLimit : valid Vault sle");
    XRPL_ASSERT(
        getVaultVersion(vault) == VaultVersion::FixedPrecision,
        "xrpl::getVaultOpenLimit : FixedPrecision Vault");
    return Number{9, 15 + getVaultBaseScale(vault)};
}

[[nodiscard]] TER
checkOptionalVaultInflow(SLE::ConstRef vault, STAmount const& amount)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::checkOptionalVaultInflow : valid Vault sle");
    XRPL_ASSERT(
        amount.asset() == vault->at(sfAsset),
        "xrpl::checkOptionalVaultInflow : amount and Vault asset match");
    XRPL_ASSERT(!amount.negative(), "xrpl::checkOptionalVaultInflow : non-negative amount");
    if (amount.negative())
        return tefINTERNAL;  // LCOV_EXCL_LINE
    if (getVaultVersion(vault) != VaultVersion::FixedPrecision)
        return tesSUCCESS;

    STAmount const rounded =
        roundToPosteriorVaultScale(vault, amount, Number::RoundingMode::TowardsZero);
    int const baseScale = getVaultBaseScale(vault);
    // Keep this explicit even though a non-negative YieldUnrealized makes the
    // Open-zone capacity ceiling reject every coarsening transition too. The
    // protocol defines both conditions independently.
    if (detail::getPosteriorVaultScale(vault, rounded) != baseScale)
        return tecLIMIT_EXCEEDED;

    if (vaultOpenZoneCapacity(vault, rounded) > getVaultOpenLimit(vault))
        return tecLIMIT_EXCEEDED;
    return tesSUCCESS;
}

[[nodiscard]] Number
vaultOpenZoneCapacity(SLE::ConstRef vault, Number const& roundedAmount)
{
    XRPL_ASSERT(
        vault && vault->getType() == ltVAULT, "xrpl::vaultOpenZoneCapacity : valid Vault sle");
    XRPL_ASSERT(
        getVaultVersion(vault) == VaultVersion::FixedPrecision,
        "xrpl::vaultOpenZoneCapacity : FixedPrecision Vault");
    NumberRoundModeGuard const rg(Number::RoundingMode::TowardsZero);
    return getAssetsTotal(vault) + vault->at(sfYieldUnrealized) + roundedAmount;
}

[[nodiscard]] Number
getVaultAssetsLentOut(
    Rules const& rules,
    Number const& assetsTotal,
    Number const& assetsAvailable,
    Number const& assetsReserved)
{
    Number lentOut = assetsTotal - assetsAvailable;
    if (rules.enabled(featureLendingProtocolV1_2))
        lentOut -= assetsReserved;
    return lentOut;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(SLE::ConstRef vault, SLE::ConstRef issuance, STAmount const& assets)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = getAssetsTotal(vault);
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
sharesToAssetsDeposit(SLE::ConstRef vault, SLE::ConstRef issuance, STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = getAssetsTotal(vault);
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
clampToAssetsTotalScale(SLE::ConstRef vault, STAmount const& delta)
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
        // FixedPrecision only reaches this branch for credits (VaultDeposit);
        // outflows go through clampVaultOutflow -> clampToAvailableScale,
        // which rounds at AssetsAvailable's own posterior scale instead.
        XRPL_ASSERT(
            !delta.negative(), "xrpl::clampToAssetsTotalScale : FixedPrecision credit only");
        STAmount const rounded =
            roundToPosteriorVaultScale(vault, delta, Number::RoundingMode::TowardsZero);
        actualDelta = rounded.negative() ? -rounded : rounded;
    }
    else
    {
        Number const assetsTotal = getAssetsTotal(vault);

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

[[nodiscard]] std::expected<STAmount, TER>
clampVaultOutflow(SLE::ConstRef vault, STAmount const& delta)
{
    XRPL_ASSERT(delta.negative(), "xrpl::clampVaultOutflow : outflow delta is negative");
    return getVaultVersion(vault) == VaultVersion::FixedPrecision
        ? clampToAvailableScale(vault, delta)
        : clampToAssetsTotalScale(vault, delta);
}

[[nodiscard]] int
vaultBaseScale(Asset const& asset, std::uint8_t scale)
{
    if (asset.integral())
        return 0;
    return -static_cast<int>(scale);
}

[[nodiscard]] bool
isOnVaultBaseGrid(Asset const& asset, Number const& value, int baseScale)
{
    return STAmount{asset, value} == value &&
        roundToAsset(asset, value, baseScale, Number::RoundingMode::TowardsZero) == value;
}

[[nodiscard]] TER
checkAssetsMaximum(SLE::ConstRef vault, Number const& amount)
{
    return isOnVaultBaseGrid(vault->at(sfAsset), amount, getVaultBaseScale(vault))
        ? TER{tesSUCCESS}
        : TER{tecPRECISION_LOSS};
}

[[nodiscard]] Number
assetsTotalForWithdrawal(SLE::ConstRef vault, WaiveUnrealizedLoss waive)
{
    Number assetTotal = getAssetsTotal(vault);
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

[[nodiscard]] Number
vaultDebitDustReference(SLE::ConstRef vault, Number const& assetsTotal)
{
    return getVaultVersion(vault) == VaultVersion::FixedPrecision
        ? Number(vault->at(sfAssetsAvailable))
        : assetsTotal;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::ConstRef vault,
    SLE::ConstRef issuance,
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
    SLE::ConstRef vault,
    SLE::ConstRef issuance,
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
isSoleShareholder(ReadView const& view, AccountID const& account, SLE::ConstRef issuance)
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
decodeVaultVersion(std::optional<std::uint8_t> leVersion)
{
    if (!leVersion)
        return VaultVersion::Legacy;
    if (*leVersion > std::to_underlying(VaultVersion::FixedPrecision))
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::decodeVaultVersion : invalid vault version");
        return VaultVersion::Legacy;
        // LCOV_EXCL_STOP
    }
    return static_cast<VaultVersion>(*leVersion);
}

[[nodiscard]] VaultVersion
getVaultVersion(SLE::ConstRef vault)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::getVaultVersion : valid Vault sle");
    return decodeVaultVersion(vault->at(~sfLEVersion));
}

[[nodiscard]] VaultVersion
vaultVersionFor(Rules const& rules)
{
    // FixedPrecision requires featureLendingProtocolV1_1 (closed-ended Vaults and
    // cash-basis accounting) and fixCleanup3_4_0, so a FixedPrecision Vault always
    // sees both enabled. fixCleanup3_2_0 is already enabled on the network.
    if (rules.enabled(featureLendingProtocolV1_1) && rules.enabled(featureLendingProtocolV1_2) &&
        rules.enabled(fixCleanup3_4_0))
        return VaultVersion::FixedPrecision;
    if (rules.enabled(featureLendingProtocolV1_1))
        return VaultVersion::CashBasis;
    return VaultVersion::Legacy;
}

[[nodiscard]] VaultKind
getVaultKind(SLE::ConstRef vault)
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
getVaultPhase(ReadView const& view, SLE::ConstRef vault)
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
    SLE::ConstRef issuance,
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
