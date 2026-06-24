#include <xrpl/ledger/helpers/VaultHelpers.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep

#include <cstdint>

namespace xrpl::vault {

namespace detail {

// Deposit uses NAV = assetsTotal - interestUnrealized (excludes loss).
// Withdraw uses NAV = assetsTotal - interestUnrealized - lossUnrealized.
// See XLS-0065 §3.1.7.1 "Share Valuation" for rationale.

STAmount
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets)
{
    Number const assetTotal = vault->at(sfAssetsTotal);
    auto const scale = vault->at(sfScale);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return STAmount{
            shares.asset(),
            Number(assets.mantissa(), assets.exponent() + scale).truncate(),
        };

    Number const interestUnrealized = vault->at(sfInterestUnrealized);
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    auto const netAssetValue = assetTotal - interestUnrealized;
    XRPL_ASSERT(netAssetValue > 0, "xrpl::vault::detail::assetsToSharesDeposit : positive NAV");

    shares = ((shareTotal * assets) / netAssetValue).truncate();
    return shares;
}

STAmount
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    Number const assetTotal = vault->at(sfAssetsTotal);
    auto const scale = vault->at(sfScale);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return STAmount{
            assets.asset(),
            shares.mantissa(),
            shares.exponent() - scale,
            false,
        };

    Number const interestUnrealized = vault->at(sfInterestUnrealized);
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    auto const netAssetValue = assetTotal - interestUnrealized;
    XRPL_ASSERT(netAssetValue > 0, "xrpl::vault::detail::sharesToAssetsDeposit : positive NAV");

    assets = (netAssetValue * shares) / shareTotal;
    return assets;
}

STAmount
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate,
    WaiveUnrealizedLoss waive)
{
    Number const assetTotal = vault->at(sfAssetsTotal);
    Number const interestUnrealized = vault->at(sfInterestUnrealized);
    Number const lossUnrealized =
        waive == WaiveUnrealizedLoss::No ? vault->at(sfLossUnrealized) : Number{0};
    Number const netAssetValue = assetTotal - interestUnrealized - lossUnrealized;

    STAmount shares{vault->at(sfShareMPTID)};
    if (netAssetValue == 0)
        return shares;
    XRPL_ASSERT(netAssetValue > 0, "xrpl::vault::detail::assetsToSharesWithdraw : positive NAV");

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    Number result = (shareTotal * assets) / netAssetValue;
    if (truncate == TruncateShares::Yes)
        result = result.truncate();

    shares = result;
    return shares;
}

STAmount
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive)
{
    Number const assetTotal = vault->at(sfAssetsTotal);
    Number const interestUnrealized = vault->at(sfInterestUnrealized);
    Number const lossUnrealized =
        waive == WaiveUnrealizedLoss::No ? vault->at(sfLossUnrealized) : Number{0};
    Number const netAssetValue = assetTotal - interestUnrealized - lossUnrealized;

    STAmount assets{vault->at(sfAsset)};
    if (netAssetValue == 0)
        return assets;
    XRPL_ASSERT(netAssetValue > 0, "xrpl::vault::detail::sharesToAssetsWithdraw : positive NAV");

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (netAssetValue * shares) / shareTotal;
    return assets;
}

}  // namespace detail

namespace {
namespace v1 {

STAmount
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets)
{
    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return STAmount{
            shares.asset(),
            Number(assets.mantissa(), assets.exponent() + vault->at(sfScale)).truncate()};

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    shares = ((shareTotal * assets) / assetTotal).truncate();
    return shares;
}

STAmount
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return STAmount{
            assets.asset(), shares.mantissa(), shares.exponent() - vault->at(sfScale), false};

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

STAmount
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate,
    WaiveUnrealizedLoss waive)
{
    Number assetTotal = vault->at(sfAssetsTotal);
    if (waive == WaiveUnrealizedLoss::No)
        assetTotal -= vault->at(sfLossUnrealized);
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

STAmount
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive)
{
    Number assetTotal = vault->at(sfAssetsTotal);
    if (waive == WaiveUnrealizedLoss::No)
        assetTotal -= vault->at(sfLossUnrealized);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return assets;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

}  // namespace v1

// v2 vault state validation — checks ledger invariants.
// Returns tecINTERNAL and logs on invalid state. Returns tesSUCCESS if valid.
TER
validateVaultState(SLE::const_ref vault, SLE::const_ref issuance, beast::Journal j)
{
    Number const assetsTotal = vault->at(sfAssetsTotal);
    if (assetsTotal == 0)
        return tesSUCCESS;

    auto const interestUnrealized = vault->at(sfInterestUnrealized);
    auto const lossUnrealized = vault->at(sfLossUnrealized);
    auto const assetsMaximum = vault->at(sfAssetsMaximum);

    // Cannot exceed asset cap
    if (assetsMaximum > 0 && assetsTotal > assetsMaximum)
    {
        JLOG(j.error()) << "vault state: assets total exceeds maximum"
                        << " (assetsTotal=" << assetsTotal << ", assetsMaximum=" << assetsMaximum
                        << ")";
        return tecINTERNAL;
    }

    if (vault->at(sfAssetsAvailable) > assetsTotal)
    {
        JLOG(j.error()) << "vault state: assets available exceeds assets total"
                        << " (assetsAvailable=" << vault->at(sfAssetsAvailable)
                        << ", assetsTotal=" << assetsTotal << ")";
        return tecINTERNAL;
    }

    if (interestUnrealized < 0)
    {
        JLOG(j.error()) << "vault state: interest unrealized is negative"
                        << " (interestUnrealized=" << interestUnrealized << ")";
        return tecINTERNAL;
    }

    // Interest can only accrue on lent assets (assetsTotal - assetsAvailable).
    Number const lentAssets = assetsTotal - vault->at(sfAssetsAvailable);
    if (interestUnrealized > lentAssets)
    {
        JLOG(j.error()) << "vault state: interest unrealized exceeds lent assets"
                        << " (interestUnrealized=" << interestUnrealized
                        << ", lentAssets=" << lentAssets << ")";
        return tecINTERNAL;
    }

    // Deposit NAV excludes loss; withdrawal NAV excludes both.
    // Deposit NAV <= 0 means all vault value is unrealized interest, which should be impossible.
    Number const depositNAV = assetsTotal - interestUnrealized;
    if (depositNAV <= 0)
    {
        JLOG(j.error()) << "vault state: deposit NAV <= 0"
                        << " (assetsTotal=" << assetsTotal
                        << ", interestUnrealized=" << interestUnrealized << ")";
        return tecINTERNAL;
    }

    Number const withdrawNAV = depositNAV - lossUnrealized;
    if (withdrawNAV < 0)
    {
        JLOG(j.error()) << "vault state: withdrawal NAV < 0"
                        << " (assetsTotal=" << assetsTotal
                        << ", interestUnrealized=" << interestUnrealized
                        << ", lossUnrealized=" << lossUnrealized << ")";
        return tecINTERNAL;
    }

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    if (withdrawNAV > 0 && shareTotal <= 0)
    {
        JLOG(j.error()) << "vault state: no outstanding shares with positive NAV"
                        << " (NAV=" << withdrawNAV << ", shareTotal=" << shareTotal << ")";
        return tecINTERNAL;
    }

    return tesSUCCESS;
}

STAmount
assetsToSharesDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets)
{
    if (rules.enabled(featureLendingProtocolV1_1))
        return detail::assetsToSharesDeposit(vault, issuance, assets);
    return v1::assetsToSharesDeposit(vault, issuance, assets);
}

STAmount
sharesToAssetsDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares)
{
    if (rules.enabled(featureLendingProtocolV1_1))
        return detail::sharesToAssetsDeposit(vault, issuance, shares);
    return v1::sharesToAssetsDeposit(vault, issuance, shares);
}

STAmount
assetsToSharesWithdraw(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate = TruncateShares::No)
{
    if (rules.enabled(featureLendingProtocolV1_1))
        return detail::assetsToSharesWithdraw(vault, issuance, assets, truncate);
    return v1::assetsToSharesWithdraw(vault, issuance, assets, truncate, WaiveUnrealizedLoss::No);
}

STAmount
sharesToAssetsWithdraw(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares)
{
    if (rules.enabled(featureLendingProtocolV1_1))
        return detail::sharesToAssetsWithdraw(vault, issuance, shares, WaiveUnrealizedLoss::No);
    return v1::sharesToAssetsWithdraw(vault, issuance, shares, WaiveUnrealizedLoss::No);
}

}  // anonymous namespace

[[nodiscard]] std::expected<ExchangeResult, TER>
computeDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::vault::computeDeposit : vault SLE");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE, "xrpl::vault::computeDeposit : issuance SLE");
    if (assets.negative())
    {
        JLOG(j.error()) << "computeDeposit: negative assets";
        return std::unexpected(tecINTERNAL);
    }
    if (assets.asset() != vault->at(sfAsset))
    {
        JLOG(j.error()) << "computeDeposit: asset mismatch";
        return std::unexpected(tecINTERNAL);
    }
    if (rules.enabled(featureLendingProtocolV1_1))
    {
        if (auto const ter = validateVaultState(vault, issuance, j))
            return std::unexpected(ter);
    }
    try
    {
        auto const shares = assetsToSharesDeposit(rules, vault, issuance, assets);
        if (shares == beast::zero)
            return std::unexpected(tecPRECISION_LOSS);

        auto const assetsOut = sharesToAssetsDeposit(rules, vault, issuance, shares);
        if (assetsOut > assets)
        {
            // LCOV_EXCL_START
            JLOG(j.error()) << "computeDeposit: would take more than offered.";
            return std::unexpected(tecINTERNAL);
            // LCOV_EXCL_STOP
        }

        return ExchangeResult{assetsOut, shares};
    }
    catch (std::overflow_error const&)
    {
        JLOG(j.debug()) << "computeDeposit: overflow error with"
                        << " scale=" << vault->at(sfScale)
                        << ", assetsTotal=" << vault->at(sfAssetsTotal)
                        << ", sharesTotal=" << issuance->at(sfOutstandingAmount)
                        << ", amount=" << assets;
        return std::unexpected(tecPATH_DRY);
    }
}

[[nodiscard]] std::expected<ExchangeResult, TER>
computeWithdrawByAssets(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    beast::Journal j)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::vault::computeWithdrawByAssets : vault SLE");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::vault::computeWithdrawByAssets : issuance SLE");
    if (assets.negative())
    {
        JLOG(j.error()) << "computeWithdrawByAssets: negative assets";
        return std::unexpected(tecINTERNAL);
    }
    if (assets.asset() != vault->at(sfAsset))
    {
        JLOG(j.error()) << "computeWithdrawByAssets: asset mismatch";
        return std::unexpected(tecINTERNAL);
    }
    if (rules.enabled(featureLendingProtocolV1_1))
    {
        if (auto const ter = validateVaultState(vault, issuance, j))
            return std::unexpected(ter);
    }
    try
    {
        auto const shares = assetsToSharesWithdraw(rules, vault, issuance, assets);
        if (shares == beast::zero)
            return std::unexpected(tecPRECISION_LOSS);

        auto const assetsOut = sharesToAssetsWithdraw(rules, vault, issuance, shares);
        return ExchangeResult{assetsOut, shares};
    }
    catch (std::overflow_error const&)
    {
        JLOG(j.debug()) << "computeWithdrawByAssets: overflow error with"
                        << " scale=" << vault->at(sfScale)
                        << ", assetsTotal=" << vault->at(sfAssetsTotal)
                        << ", sharesTotal=" << issuance->at(sfOutstandingAmount)
                        << ", amount=" << assets;
        return std::unexpected(tecPATH_DRY);
    }
}

[[nodiscard]] std::expected<ExchangeResult, TER>
computeWithdrawByShares(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    beast::Journal j)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::vault::computeWithdrawByShares : vault SLE");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::vault::computeWithdrawByShares : issuance SLE");
    if (shares.negative())
    {
        JLOG(j.error()) << "computeWithdrawByShares: negative shares";
        return std::unexpected(tecINTERNAL);
    }
    if (shares.asset() != vault->at(sfShareMPTID))
    {
        JLOG(j.error()) << "computeWithdrawByShares: share asset mismatch";
        return std::unexpected(tecINTERNAL);
    }
    if (rules.enabled(featureLendingProtocolV1_1))
    {
        if (auto const ter = validateVaultState(vault, issuance, j))
            return std::unexpected(ter);
    }
    try
    {
        auto const assets = sharesToAssetsWithdraw(rules, vault, issuance, shares);
        return ExchangeResult{assets, shares};
    }
    catch (std::overflow_error const&)
    {
        JLOG(j.debug()) << "computeWithdrawByShares: overflow error with"
                        << " scale=" << vault->at(sfScale)
                        << ", assetsTotal=" << vault->at(sfAssetsTotal)
                        << ", sharesTotal=" << issuance->at(sfOutstandingAmount)
                        << ", shares=" << shares;
        return std::unexpected(tecPATH_DRY);
    }
}

[[nodiscard]] std::expected<ExchangeResult, TER>
computeClawback(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& clawbackAmount,
    Number const& assetsAvailable,
    beast::Journal j)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::vault::computeClawback : vault SLE");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE, "xrpl::vault::computeClawback : issuance SLE");
    if (clawbackAmount.negative())
    {
        JLOG(j.error()) << "computeClawback: negative clawbackAmount";
        return std::unexpected(tecINTERNAL);
    }
    if (clawbackAmount.asset() != vault->at(sfAsset))
    {
        JLOG(j.error()) << "computeClawback: asset mismatch";
        return std::unexpected(tecINTERNAL);
    }
    if (rules.enabled(featureLendingProtocolV1_1))
    {
        if (auto const ter = validateVaultState(vault, issuance, j))
            return std::unexpected(ter);
    }
    try
    {
        auto sharesDestroyed = assetsToSharesWithdraw(rules, vault, issuance, clawbackAmount);
        auto assetsRecovered = sharesToAssetsWithdraw(rules, vault, issuance, sharesDestroyed);

        // Clamp to maximum.
        if (assetsRecovered > assetsAvailable)
        {
            assetsRecovered = STAmount{assetsRecovered.asset(), assetsAvailable};
            // Note, it is important to truncate the number of shares,
            // otherwise the corresponding assets might breach the
            // AssetsAvailable
            sharesDestroyed = assetsToSharesWithdraw(
                rules, vault, issuance, assetsRecovered, TruncateShares::Yes);
            assetsRecovered = sharesToAssetsWithdraw(rules, vault, issuance, sharesDestroyed);

            if (assetsRecovered > assetsAvailable)
            {
                // LCOV_EXCL_START
                JLOG(j.error()) << "computeClawback: invalid rounding of shares.";
                return std::unexpected(tecINTERNAL);
                // LCOV_EXCL_STOP
            }
        }

        return ExchangeResult{assetsRecovered, sharesDestroyed};
    }
    catch (std::overflow_error const&)
    {
        JLOG(j.debug()) << "computeClawback: overflow error with"
                        << " scale=" << vault->at(sfScale)
                        << ", assetsTotal=" << vault->at(sfAssetsTotal)
                        << ", sharesTotal=" << issuance->at(sfOutstandingAmount)
                        << ", amount=" << clawbackAmount;
        return std::unexpected(tecPATH_DRY);
    }
}

[[nodiscard]] TER
borrowFromVault(
    ApplyView& view,
    SLE::ref vault,
    Number const& amount,
    Number const& yield,
    beast::Journal j)
{
    XRPL_ASSERT(vault && vault->getType() == ltVAULT, "xrpl::vault::borrowFromVault : vault SLE");

    if (amount <= 0 || yield < 0)
    {
        JLOG(j.error()) << "borrowFromVault: invalid input"
                        << " (amount=" << amount << ", yield=" << yield << ")";
        return tecINTERNAL;
    }

    // Cannot borrow more than available assets
    if (vault->at(sfAssetsAvailable) < amount)
    {
        JLOG(j.error()) << "borrowFromVault: insufficient available assets"
                        << " (available=" << *vault->at(sfAssetsAvailable) << ", amount=" << amount
                        << ")";
        return tecINTERNAL;
    }

    // Update vault state
    if (view.rules().enabled(featureLendingProtocolV1_1))
        vault->at(sfInterestUnrealized) += yield;
    vault->at(sfAssetsAvailable) -= amount;
    vault->at(sfAssetsTotal) += yield;

    if (view.rules().enabled(featureLendingProtocolV1_1))
    {
        std::shared_ptr<SLE const> issuance =
            view.read(keylet::mptIssuance(vault->at(sfShareMPTID)));
        if (!issuance)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (auto const ter = validateVaultState(vault, issuance, j))
            return ter;
    }

    view.update(vault);
    return tesSUCCESS;
}
[[nodiscard]] bool
isSoleShareholder(ReadView const& view, AccountID const& account, SLE::const_ref issuance)
{
    XRPL_ASSERT(
        issuance && issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::vault::isSoleShareholder : valid issuance SLE");

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

[[nodiscard]] STAmount
sharesToAssetsWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares,
    WaiveUnrealizedLoss waive)
{
    return detail::sharesToAssetsWithdraw(vault, issuance, shares, waive);
}

}  // namespace xrpl::vault
