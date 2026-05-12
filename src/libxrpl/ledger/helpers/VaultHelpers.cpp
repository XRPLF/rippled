#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#include <optional>

namespace xrpl {

[[nodiscard]] TER
canApplyToVault(
    ReadView const& view,
    SLE::const_ref vault,
    STAmount const& amount,
    beast::Journal j,
    std::string_view logPrefix)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::canApplyToVault : valid Vault sle");

    if (!view.rules().enabled(fixCleanup3_2_0))
        return tesSUCCESS;
    if (amount == beast::kZERO)
        return tesSUCCESS;

    Asset const vaultAsset = vault->at(sfAsset);

    //  Silent absorption when the request rounds to zero at sfAssetsAvailable's scale.
    int const availScale = scale(vault->at(sfAssetsAvailable), vaultAsset);
    if (amount.isZeroAtScale(availScale))
    {
        JLOG(j.warn()) << logPrefix << ": amount " << amount.getFullText()
                       << " rounds to zero at available scale " << availScale;
        return tecPRECISION_LOSS;
    }

    // Coarser-scale lossless
    int const totalScale = scale(vault->at(sfAssetsTotal), vaultAsset);
    if (totalScale > availScale && !amount.isExactAtScale(totalScale))
    {
        JLOG(j.warn()) << logPrefix << ": amount " << amount.getFullText()
                       << " is not representable at vault total scale " << totalScale
                       << " while available is at scale " << availScale;
        return tecPRECISION_LOSS;
    }

    return tesSUCCESS;
}

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::assetsToSharesDeposit : valid Vault sle");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::assetsToSharesDeposit : valid MPTokenIssuance sle");

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
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::assetsToSharesDeposit : valid Vault sle");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::assetsToSharesDeposit : valid MPTokenIssuance sle");

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

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::assetsToSharesDeposit : valid Vault sle");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::assetsToSharesDeposit : valid MPTokenIssuance sle");

    XRPL_ASSERT(!assets.negative(), "xrpl::assetsToSharesWithdraw : non-negative assets");
    XRPL_ASSERT(
        assets.asset() == vault->at(sfAsset),
        "xrpl::assetsToSharesWithdraw : assets and vault match");
    if (assets.negative() || assets.asset() != vault->at(sfAsset))
        return std::nullopt;  // LCOV_EXCL_LINE

    Number assetTotal = vault->at(sfAssetsTotal);
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

[[nodiscard]] std::optional<STAmount>
sharesToAssetsWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::assetsToSharesDeposit : valid Vault sle");
    XRPL_ASSERT(
        issuance->getType() == ltMPTOKEN_ISSUANCE,
        "xrpl::assetsToSharesDeposit : valid MPTokenIssuance sle");

    XRPL_ASSERT(!shares.negative(), "xrpl::sharesToAssetsWithdraw : non-negative shares");
    XRPL_ASSERT(
        shares.asset() == vault->at(sfShareMPTID),
        "xrpl::sharesToAssetsWithdraw : shares and vault match");
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

[[nodiscard]] TER
depositToVault(
    ApplyView& view,
    SLE::ref vault,
    AccountID const& depositor,
    STAmount const& assetsDeposited,
    beast::Journal j)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::depositToVault : valid Vault sle");
    AccountID const vaultAccount = vault->at(sfAccount);
    Asset const vaultAsset = vault->at(sfAsset);

    XRPL_ASSERT(
        assetsDeposited.asset() == vaultAsset, "xrpl::depositToVault : assets and vault match");

    Number const beforeAssetsTotal = *vault->at(sfAssetsTotal);
    Number const beforeAssetsAvailable = *vault->at(sfAssetsAvailable);

    vault->at(sfAssetsTotal) += assetsDeposited;
    vault->at(sfAssetsAvailable) += assetsDeposited;
    view.update(vault);

    // A deposit must not push the vault over its limit.
    auto const maximum = *vault->at(sfAssetsMaximum);
    if (maximum != 0 && *vault->at(sfAssetsTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    if (auto const ter = accountSendExact(
            view, depositor, vaultAccount, assetsDeposited, j, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    associateAsset(*vault, vaultAsset);

    if (!view.rules().enabled(fixCleanup3_2_0))
        return tesSUCCESS;

    // Sanity check
    if (accountHolds(
            view,
            depositor,
            assetsDeposited.asset(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            j) < beast::kZERO)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "depositToVault: negative balance of account assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    Number const afterAssetsTotal = *vault->at(sfAssetsTotal);
    Number const afterAssetsAvailable = *vault->at(sfAssetsAvailable);
    Number const totalDelta = afterAssetsTotal - beforeAssetsTotal;
    Number const availableDelta = afterAssetsAvailable - beforeAssetsAvailable;

    if (!equalAtAssetScale(
            totalDelta,
            availableDelta,
            beforeAssetsTotal.nonZeroOr(afterAssetsTotal),
            beforeAssetsAvailable.nonZeroOr(afterAssetsAvailable),
            vaultAsset))
    {
        JLOG(j.error()) << "depositToVault: vault accounting delta mismatch"
                        << " totalDelta=" << to_string(totalDelta)
                        << " availableDelta=" << to_string(availableDelta);
        return tecPRECISION_LOSS;
    }

    return tesSUCCESS;
}

[[nodiscard]] TER
withdrawFromVault(
    ApplyView& view,
    STTx const& tx,
    SLE::ref vault,
    AccountID const& depositor,
    AccountID const& destination,
    XRPAmount preFeeBalance,
    STAmount const& assetsWithdrawn,
    STAmount const& sharesRedeemed,
    beast::Journal j)
{
    XRPL_ASSERT(vault->getType() == ltVAULT, "xrpl::withdrawFromVault : valid Vault sle");

    AccountID const vaultAccount = vault->at(sfAccount);
    Asset const vaultAsset = vault->at(sfAsset);

    XRPL_ASSERT(
        assetsWithdrawn.asset() == vaultAsset, "xrpl::withdrawFromVault : assets and vault match");

    Number const beforeAssetsTotal = *vault->at(sfAssetsTotal);
    Number const beforeAssetsAvailable = *vault->at(sfAssetsAvailable);

    [[maybe_unused]] Number const lossUnrealized = *vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (beforeAssetsTotal - beforeAssetsAvailable),
        "xrpl::withdrawFromVault : loss and assets do balance");

    // The vault must have enough assets on hand.
    if (beforeAssetsAvailable < assetsWithdrawn)
    {
        JLOG(j.debug()) << "withdrawFromVault: vault doesn't hold enough assets";
        return tecINSUFFICIENT_FUNDS;
    }

    vault->at(sfAssetsTotal) -= assetsWithdrawn;
    vault->at(sfAssetsAvailable) -= assetsWithdrawn;
    view.update(vault);

    // Transfer shares from depositor to vault.
    if (auto const ter =
            accountSend(view, depositor, vaultAccount, sharesRedeemed, j, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Try to remove MPToken for shares, if the account balance is zero. Vault
    // pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    // Keep MPToken if holder is the vault owner.
    if (depositor != vault->at(sfOwner))
    {
        if (auto const ter = removeEmptyHolding(view, depositor, sharesRedeemed.asset(), j);
            isTesSuccess(ter))
        {
            JLOG(j.debug()) << "withdrawFromVault: removed empty MPToken for shares";
        }
        else if (ter != tecHAS_OBLIGATIONS)
        {
            // LCOV_EXCL_START
            JLOG(j.error())  //
                << "withdrawFromVault: failed to remove MPToken for shares "
                << " with result: " << transToken(ter);
            return ter;
            // LCOV_EXCL_STOP
        }
        // else quietly ignore, account balance is not zero
    }

    // Round the vault's STNumber accounting fields to the asset's precision.
    associateAsset(*vault, vaultAsset);

    if (auto const ter = doWithdraw(
            view, tx, depositor, destination, vaultAccount, preFeeBalance, assetsWithdrawn, j);
        !isTesSuccess(ter))
        return ter;

    if (!view.rules().enabled(fixCleanup3_2_0))
        return tesSUCCESS;

    // accountSendExact above covers trust-line conservation. This remaining
    // check is orthogonal: it catches associateAsset asymmetric rounding
    // on the vault's STNumber accounting fields (sfAssetsTotal vs
    // sfAssetsAvailable).
    Number const afterAssetsTotal = *vault->at(sfAssetsTotal);
    Number const afterAssetsAvailable = *vault->at(sfAssetsAvailable);
    Number const totalDelta = beforeAssetsTotal - afterAssetsTotal;
    Number const availableDelta = beforeAssetsAvailable - afterAssetsAvailable;

    if (!equalAtAssetScale(
            totalDelta,
            availableDelta,
            beforeAssetsTotal.nonZeroOr(afterAssetsTotal),
            beforeAssetsAvailable.nonZeroOr(afterAssetsAvailable),
            vaultAsset))
    {
        // Catches post-state edge-cross asymmetric rounding —
        // see depositToVault counterpart.
        JLOG(j.error()) << "withdrawFromVault: vault accounting delta mismatch"
                        << " totalDelta=" << to_string(totalDelta)
                        << " availableDelta=" << to_string(availableDelta);
        return tecPRECISION_LOSS;
    }

    return tesSUCCESS;
}

Expected<ClampedWithdrawal, TER>
clampAssetWithdrawal(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& amount,
    Rules const& rules)
{
    auto maybeShares = assetsToSharesWithdraw(
        vault,
        issuance,
        amount,
        rules.enabled(fixCleanup3_2_0) ? TruncateShares::Yes : TruncateShares::No);
    if (!maybeShares)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    auto const shares = *maybeShares;
    if (shares == beast::kZERO)
        return Unexpected(tecPRECISION_LOSS);

    auto const maybeAssetAmount = sharesToAssetsWithdraw(vault, issuance, shares);
    if (!maybeAssetAmount)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    auto const assetAmount = *maybeAssetAmount;

    if (!rules.enabled(fixCleanup3_2_0))
    {
        return ClampedWithdrawal{
            .assets = assetAmount,
            .shares = shares,
        };
    }

    Asset vaultAsset = vault->at(sfAsset);
    // Since sfAssetsTotal >= sfAssetsAvailable it is guaranteed to have the coarser scale
    int const vaultScale = scale(vault->at(sfAssetsTotal), vaultAsset);

    // Clamp the final asset amount down to the vault's coarser ULP grid
    auto const clampedAssets =
        roundToScale(assetAmount, vaultScale, Number::RoundingMode::TowardsZero);

    // Per-share NAV can fall below one coarse-grid tick (e.g. heavy loan
    // inflation). Burning shares for zero assets destroys value — reject.
    if (clampedAssets == beast::kZERO)
        return Unexpected(tecPRECISION_LOSS);

    maybeShares = assetsToSharesWithdraw(vault, issuance, clampedAssets);
    if (!maybeShares)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    auto const clampedShares = *maybeShares;

    return ClampedWithdrawal{
        .assets = clampedAssets,
        .shares = clampedShares,
    };
}

Expected<ClampedWithdrawal, TER>
clampShareWithdrawal(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares,
    Rules const& rules)
{
    // Shares are integer (MPT) — no share-side truncation needed.
    // Step 1: convert the requested share count to assets at current NAV.
    auto const maybeAssets = sharesToAssetsWithdraw(vault, issuance, shares);
    if (!maybeAssets)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    auto const assetAmount = *maybeAssets;

    // Short-circuit two cases: drained-vault (NAV-conversion yielded zero;
    // let doApply's downstream handling surface tecINSUFFICIENT_FUNDS) and
    // pre-amendment (locked for replay — pass through unchanged).
    if (assetAmount == beast::kZERO || !rules.enabled(fixCleanup3_2_0))
    {
        return ClampedWithdrawal{
            .assets = assetAmount,
            .shares = shares,
        };
    }

    // Step 2: floor assets to sfAssetsTotal's grid so the rail decrements
    // stay in lockstep (same property clampAssetWithdrawal enforces).
    Asset const vaultAsset = vault->at(sfAsset);
    int const vaultScale = scale(vault->at(sfAssetsTotal), vaultAsset);
    auto const clampedAssets =
        roundToScale(assetAmount, vaultScale, Number::RoundingMode::TowardsZero);

    // Step 3: requested share count doesn't even buy 1 ULP at the coarse
    // grid (heavily diluted vault, or sub-grid NAV). Burning shares for
    // zero assets is value destruction — refuse.
    if (clampedAssets == beast::kZERO)
        return Unexpected(tecPRECISION_LOSS);

    // Step 4: re-derive the share count from the clamped asset amount so
    // assets-out and shares-burnt stay paired. The re-derived count is
    // bounded above by the original (clampedAssets ≤ assetAmount), so the
    // user never burns more than they asked to.
    auto const maybeShares =
        assetsToSharesWithdraw(vault, issuance, clampedAssets, TruncateShares::Yes);
    if (!maybeShares)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    // Step 5: re-derived shares could truncate to zero in a heavily-diluted
    // vault where NAV < 1 ULP of total. That would deliver assets for no
    // shares burnt — also value destruction in the opposite direction.
    if (*maybeShares == beast::kZERO)
        return Unexpected(tecPRECISION_LOSS);

    return ClampedWithdrawal{
        .assets = clampedAssets,
        .shares = *maybeShares,
    };
}

}  // namespace xrpl
