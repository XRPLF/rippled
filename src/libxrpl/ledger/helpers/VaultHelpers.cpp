#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
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

[[nodiscard]] std::optional<STAmount>
assetsToSharesDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets)
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
sharesToAssetsDeposit(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& shares)
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

[[nodiscard]] std::optional<STAmount>
assetsToSharesWithdraw(
    std::shared_ptr<SLE const> const& vault,
    std::shared_ptr<SLE const> const& issuance,
    STAmount const& assets,
    TruncateShares truncate)
{
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
    std::shared_ptr<SLE> const& vault,
    AccountID const& depositor,
    STAmount const& assetsDeposited,
    beast::Journal j)
{
    AccountID const vaultAccount = vault->at(sfAccount);
    Asset const vaultAsset = vault->at(sfAsset);

    XRPL_ASSERT(
        assetsDeposited.asset() == vaultAsset, "xrpl::depositToVault : assets and vault match");

    // Pre-fixCleanup3_2_0 the helper performed no delta verification: silent
    // rounding by associateAsset on the STNumber accounting fields would be
    // caught (if at all) by the deposit invariants at finalize time, with
    // tecINVARIANT_FAILED. Post-amendment we snapshot the pre-state and
    // compare deltas at the end of the helper, returning tecPRECISION_LOSS
    // before invariants run.
    Number const beforeAssetsTotal = *vault->at(sfAssetsTotal);
    Number const beforeAssetsAvailable = *vault->at(sfAssetsAvailable);

    vault->at(sfAssetsTotal) += assetsDeposited;
    vault->at(sfAssetsAvailable) += assetsDeposited;
    view.update(vault);

    // A deposit must not push the vault over its limit.
    auto const maximum = *vault->at(sfAssetsMaximum);
    if (maximum != 0 && *vault->at(sfAssetsTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    // Transfer assets from depositor to vault. accountSendExact verifies
    // depositor loss == vault gain, catching IOU canonicalization losses
    // when either trust line crosses the 16-digit edge.
    if (auto const ter = accountSendExact(
            view, depositor, vaultAccount, assetsDeposited, j, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Round the vault's STNumber accounting fields to the asset's precision.
    // Post-amendment this must run before delta verification so any silent
    // rounding here is observed as a delta mismatch rather than left to fire
    // as an invariant after commit.
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

    // Trust-line value-transfer integrity is enforced by accountSendExact
    // above; this remaining check covers SLE-field rounding by
    // associateAsset on the vault's STNumber accounting fields.
    //
    // The two field deltas should agree at the asset's precision. When
    // sfAssetsTotal sits at a different magnitude from sfAssetsAvailable
    // (loans outstanding, sfAssetsTotal == sfAssetsAvailable + outstanding
    // principal) a deposit that fits cleanly on sfAssetsAvailable can still
    // be silently rounded inside roundToAsset on sfAssetsTotal, leaving
    // total understated relative to the vault's actual receipt.
    //
    // Defense-in-depth quantize-then-equal at the coarser of the two
    // fields' pre-state scales (matching VaultInvariant's idiom). Both
    // fields receive `+= assetsDeposited` in Number arithmetic; the
    // tolerance admits sub-coarser-side-ULP canonicalization noise
    // accumulated from prior operations.
    //
    // Note: this check does NOT fire on the sfAssetsTotal-at-edge /
    // sfAssetsAvailable-below-edge bug class (small deposit pushes
    // sfAssetsTotal past 10^16 while sfAssetsAvailable gains cleanly).
    // At the comparison scale, both deltas round to the same coarse
    // bucket and the equality holds; VaultInvariant's "deposit must
    // observably increase vault balance" assertion catches that case at
    // finalize time. See testBugSleDeltaCheckTotalEdgeWithLoan.
    Number const afterAssetsTotal = *vault->at(sfAssetsTotal);
    Number const afterAssetsAvailable = *vault->at(sfAssetsAvailable);
    Number const totalDelta = afterAssetsTotal - beforeAssetsTotal;
    Number const availableDelta = afterAssetsAvailable - beforeAssetsAvailable;

    if (!equalAtAssetScale(
            totalDelta,
            availableDelta,
            firstNonzero(beforeAssetsTotal, afterAssetsTotal),
            firstNonzero(beforeAssetsAvailable, afterAssetsAvailable),
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
    std::shared_ptr<SLE> const& vault,
    AccountID const& depositor,
    AccountID const& destination,
    XRPAmount preFeeBalance,
    STAmount const& assetsWithdrawn,
    STAmount const& sharesRedeemed,
    beast::Journal j)
{
    AccountID const vaultAccount = vault->at(sfAccount);
    Asset const vaultAsset = vault->at(sfAsset);

    XRPL_ASSERT(
        assetsWithdrawn.asset() == vaultAsset, "xrpl::withdrawFromVault : assets and vault match");

    // Pre-fixCleanup3_2_0 the helper performed no delta verification: silent
    // rounding by associateAsset on the STNumber accounting fields would be
    // caught (if at all) by the withdrawal invariants at finalize time, with
    // tecINVARIANT_FAILED. Post-amendment we snapshot the pre-state and
    // compare deltas at the end of the helper, returning tecPRECISION_LOSS
    // before invariants run. Trust-line value transfer is verified inside
    // doWithdraw via accountSendExact, which compares sender-loss to
    // destination-gain at the coarser pre-state grid — sub-ULP-at-receiver
    // canonicalization is admitted as silent absorption (vault loses, the
    // unit returns to the issuer's obligation pool); super-ULP discrepancies
    // are rejected as tecPRECISION_LOSS.
    Number const beforeAssetsTotal = *vault->at(sfAssetsTotal);
    Number const beforeAssetsAvailable = *vault->at(sfAssetsAvailable);

    [[maybe_unused]] Number const lossUnrealized = *vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (beforeAssetsTotal - beforeAssetsAvailable),
        "xrpl::withdrawFromVault : loss and assets do balance");

    // The vault must have enough assets on hand. The vault may hold assets
    // that it has already pledged. That is why we look at AssetAvailable
    // instead of the pseudo-account balance.
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
    // Post-amendment this must run before delta verification so any silent
    // rounding here is observed as a delta mismatch rather than left to fire
    // as an invariant after commit.
    associateAsset(*vault, vaultAsset);

    // doWithdraw uses accountSendExact internally; super-ULP value-transfer
    // mismatches return tecPRECISION_LOSS, sub-ULP-at-receiver canonicalization
    // is admitted as silent absorption.
    if (auto const ter = doWithdraw(
            view, tx, depositor, destination, vaultAccount, preFeeBalance, assetsWithdrawn, j);
        !isTesSuccess(ter))
        return ter;

    if (!view.rules().enabled(fixCleanup3_2_0))
        return tesSUCCESS;

    // accountSendExact above covers trust-line conservation. This remaining
    // check is orthogonal: it catches associateAsset asymmetric rounding
    // on the vault's STNumber accounting fields (sfAssetsTotal vs
    // sfAssetsAvailable). Subtraction from already-canonicalized 16-digit
    // STAmount values is generally clean through the mantissa edge
    // (precision improves on the way down), so this check rarely fires;
    // symmetry with the deposit helper is preserved so future changes
    // (non-zero sfScale, multi-asset operations) don't introduce a
    // silent leak.
    Number const afterAssetsTotal = *vault->at(sfAssetsTotal);
    Number const afterAssetsAvailable = *vault->at(sfAssetsAvailable);
    Number const totalDelta = beforeAssetsTotal - afterAssetsTotal;
    Number const availableDelta = beforeAssetsAvailable - afterAssetsAvailable;

    if (!equalAtAssetScale(
            totalDelta,
            availableDelta,
            firstNonzero(beforeAssetsTotal, afterAssetsTotal),
            firstNonzero(beforeAssetsAvailable, afterAssetsAvailable),
            vaultAsset))
    {
        JLOG(j.error()) << "withdrawFromVault: vault accounting delta mismatch"
                        << " totalDelta=" << to_string(totalDelta)
                        << " availableDelta=" << to_string(availableDelta);
        return tecPRECISION_LOSS;
    }

    return tesSUCCESS;
}

}  // namespace xrpl
