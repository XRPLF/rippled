#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>

namespace xrpl::vault {

// Pure math — v2 only.
namespace math::v2 {

[[nodiscard]] Number
assetsToSharesDeposit(
    Number const& assetTotal,
    Number const& shareTotal,
    std::int32_t scale,
    Number const& assets)
{
    if (assetTotal == 0)
        return Number(assets.mantissa(), assets.exponent() + scale).truncate();
    return ((shareTotal * assets) / assetTotal).truncate();
}

[[nodiscard]] Number
sharesToAssetsDeposit(
    Number const& assetTotal,
    Number const& shareTotal,
    std::int32_t scale,
    STAmount const& shares)
{
    if (assetTotal == 0)
        return Number(shares.mantissa(), shares.exponent() - scale);
    return (assetTotal * shares) / shareTotal;
}

[[nodiscard]] Number
assetsToSharesWithdraw(
    Number const& assetTotal,
    Number const& lossUnrealized,
    Number const& shareTotal,
    Number const& assets,
    TruncateShares truncate)
{
    Number effectiveAssets = assetTotal - lossUnrealized;
    if (effectiveAssets == 0)
        return Number(0);
    Number result = (shareTotal * assets) / effectiveAssets;
    if (truncate == TruncateShares::yes)
        result = result.truncate();
    return result;
}

[[nodiscard]] Number
sharesToAssetsWithdraw(
    Number const& assetTotal,
    Number const& lossUnrealized,
    Number const& shareTotal,
    STAmount const& shares)
{
    Number effectiveAssets = assetTotal - lossUnrealized;
    if (effectiveAssets == 0)
        return Number(0);
    return (effectiveAssets * shares) / shareTotal;
}

}  // namespace math::v2

// SLE-based implementations, v1 untouched, v2 delegates to math::v2.
namespace {

namespace v2 {

std::optional<STAmount>
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::vault::v2::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::vault::v2::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount shares{vault->at(sfShareMPTID)};
    shares = math::v2::assetsToSharesDeposit(
        vault->at(sfAssetsTotal), issuance->at(sfOutstandingAmount), vault->at(sfScale), assets);
    return shares;
}

std::optional<STAmount>
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::vault::v2::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::vault::v2::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount assets{vault->at(sfAsset)};
    assets = math::v2::sharesToAssetsDeposit(
        vault->at(sfAssetsTotal), issuance->at(sfOutstandingAmount), vault->at(sfScale), shares);
    return assets;
}

std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate)
{
    XRPL_ASSERT(
        !assets.negative(), "xrpl::vault::v2::assetsToSharesWithdraw : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::vault::v2::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount shares{vault->at(sfShareMPTID)};
    shares = math::v2::assetsToSharesWithdraw(
        vault->at(sfAssetsTotal),
        vault->at(sfLossUnrealized),
        issuance->at(sfOutstandingAmount),
        assets,
        truncate);
    return shares;
}

std::optional<STAmount>
sharesToAssetsWithdraw(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    XRPL_ASSERT(
        !shares.negative(), "xrpl::vault::v2::sharesToAssetsWithdraw : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::vault::v2::sharesToAssetsWithdraw : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    STAmount assets{vault->at(sfAsset)};
    assets = math::v2::sharesToAssetsWithdraw(
        vault->at(sfAssetsTotal),
        vault->at(sfLossUnrealized),
        issuance->at(sfOutstandingAmount),
        shares);
    return assets;
}

}  // namespace v2

// v1 math is intentionally not factored out like v2. Since Single Asset Vault
// is already released, refactoring v1 risks introducing behavioral changes in
// production code that we cannot gate behind an amendment.
namespace v1 {

std::optional<STAmount>
assetsToSharesDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& assets)
{
    XRPL_ASSERT(!assets.negative(), "xrpl::vault::v1::assetsToSharesDeposit : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::vault::v1::assetsToSharesDeposit : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

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

std::optional<STAmount>
sharesToAssetsDeposit(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    XRPL_ASSERT(!shares.negative(), "xrpl::vault::v1::sharesToAssetsDeposit : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::vault::v1::sharesToAssetsDeposit : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number const assetTotal = vault->at(sfAssetsTotal);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return STAmount{
            assets.asset(), shares.mantissa(), shares.exponent() - vault->at(sfScale), false};

    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

std::optional<STAmount>
assetsToSharesWithdraw(
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate)
{
    XRPL_ASSERT(
        !assets.negative(), "xrpl::vault::v1::assetsToSharesWithdraw : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::vault::v1::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number assetTotal = vault->at(sfAssetsTotal);
    assetTotal -= vault->at(sfLossUnrealized);
    STAmount shares{vault->at(sfShareMPTID)};
    if (assetTotal == 0)
        return shares;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    Number result = (shareTotal * assets) / assetTotal;
    if (truncate == TruncateShares::yes)
        result = result.truncate();
    shares = result;
    return shares;
}

std::optional<STAmount>
sharesToAssetsWithdraw(SLE::const_ref vault, SLE::const_ref issuance, STAmount const& shares)
{
    XRPL_ASSERT(
        !shares.negative(), "xrpl::vault::v1::sharesToAssetsWithdraw : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::vault::v1::sharesToAssetsWithdraw : shares and vault match");
    if (shares.negative() || shares.asset() != vault->at(sfShareMPTID))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number assetTotal = vault->at(sfAssetsTotal);
    assetTotal -= vault->at(sfLossUnrealized);
    STAmount assets{vault->at(sfAsset)};
    if (assetTotal == 0)
        return assets;
    Number const shareTotal = issuance->at(sfOutstandingAmount);
    assets = (assetTotal * shares) / shareTotal;
    return assets;
}

}  // namespace v1

}  // anonymous namespace

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets)
{
    if (rules.enabled(fixLendingProtocolV1_1))
        return v2::assetsToSharesDeposit(vault, issuance, assets);
    return v1::assetsToSharesDeposit(vault, issuance, assets);
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsDeposit(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares)
{
    if (rules.enabled(fixLendingProtocolV1_1))
        return v2::sharesToAssetsDeposit(vault, issuance, shares);
    return v1::sharesToAssetsDeposit(vault, issuance, shares);
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& assets,
    TruncateShares truncate)
{
    if (rules.enabled(fixLendingProtocolV1_1))
        return v2::assetsToSharesWithdraw(vault, issuance, assets, truncate);
    return v1::assetsToSharesWithdraw(vault, issuance, assets, truncate);
}

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    Rules const& rules,
    SLE::const_ref vault,
    SLE::const_ref issuance,
    STAmount const& shares)
{
    if (rules.enabled(fixLendingProtocolV1_1))
        return v2::sharesToAssetsWithdraw(vault, issuance, shares);
    return v1::sharesToAssetsWithdraw(vault, issuance, shares);
}

}  // namespace xrpl::vault
