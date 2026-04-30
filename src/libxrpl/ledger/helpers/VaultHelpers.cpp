#include <xrpl/ledger/helpers/VaultHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

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
    if (truncate == TruncateShares::yes)
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

    // Delta verification needs the raw asset balance, not the spendable
    // amount. For XRP, accountHolds (and shFULL_BALANCE) always subtracts
    // reserve via xrpLiquid; removing an empty MPToken during withdrawal
    // drops owner count and frees up reserve, inflating the post-balance by
    // one incremental-reserve. Read sfBalance directly for XRP to dodge
    // that. IOU / MPT trust lines have no reserve concept, so accountHolds
    // is fine for them.
    auto const holds = [&](AccountID const& acct) -> STAmount {
        if (vaultAsset.native())
        {
            auto const sle = view.read(keylet::account(acct));
            return sle ? sle->getFieldAmount(sfBalance) : STAmount{vaultAsset};
        }
        return accountHolds(
            view,
            acct,
            vaultAsset,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j);
    };

    // Pre-fixCleanup3_2_0 the helper performed no delta verification: silent
    // rounding by associateAsset on the STNumber accounting fields would be
    // caught (if at all) by the deposit invariants at finalize time, with
    // tecINVARIANT_FAILED. Post-amendment we snapshot the pre-state and
    // compare deltas at the end of the helper, returning tecPRECISION_LOSS
    // before invariants run.
    bool const verifyDeltas = view.rules().enabled(fixCleanup3_2_0);

    Number const beforeAssetsTotal = *vault->at(sfAssetsTotal);
    Number const beforeAssetsAvailable = *vault->at(sfAssetsAvailable);
    STAmount const beforeVaultBalance = verifyDeltas ? holds(vaultAccount) : STAmount{vaultAsset};
    STAmount const beforeDepositorBalance = verifyDeltas ? holds(depositor) : STAmount{vaultAsset};

    vault->at(sfAssetsTotal) += assetsDeposited;
    vault->at(sfAssetsAvailable) += assetsDeposited;
    view.update(vault);

    // A deposit must not push the vault over its limit.
    auto const maximum = *vault->at(sfAssetsMaximum);
    if (maximum != 0 && *vault->at(sfAssetsTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    // Transfer assets from depositor to vault.
    if (auto const ter =
            accountSend(view, depositor, vaultAccount, assetsDeposited, j, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Sanity check: depositor must not be left with a negative balance.
    if (holds(depositor) < beast::zero)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "depositToVault: negative balance of account assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Round the vault's STNumber accounting fields to the asset's precision.
    // Post-amendment this must run before delta verification so any silent
    // rounding here is observed as a delta mismatch rather than left to fire
    // as an invariant after commit.
    associateAsset(*vault, vaultAsset);

    if (verifyDeltas)
    {
        // Verify every observable balance changed by exactly
        // `assetsDeposited`. Issuer-as-depositor is a special case: an issuer
        // payment mints funds rather than moving the issuer's own balance, so
        // skip the depositor-side check there. This mirrors the carve-out in
        // the deposit invariant.
        Number const expected = assetsDeposited;

        if (*vault->at(sfAssetsTotal) - beforeAssetsTotal != expected ||
            *vault->at(sfAssetsAvailable) - beforeAssetsAvailable != expected)
        {
            JLOG(j.debug()) << "depositToVault: vault accounting delta mismatch.";
            return tecPRECISION_LOSS;
        }

        if (Number{holds(vaultAccount) - beforeVaultBalance} != expected)
        {
            JLOG(j.debug()) << "depositToVault: vault balance delta mismatch.";
            return tecPRECISION_LOSS;
        }

        bool const issuerDeposit = !vaultAsset.native() && depositor == vaultAsset.getIssuer();
        if (!issuerDeposit && Number{beforeDepositorBalance - holds(depositor)} != expected)
        {
            JLOG(j.debug()) << "depositToVault: depositor balance delta mismatch.";
            return tecPRECISION_LOSS;
        }
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

    // Delta verification needs the raw asset balance, not the spendable
    // amount. For XRP, accountHolds (and shFULL_BALANCE) always subtracts
    // reserve via xrpLiquid; removing an empty MPToken during withdrawal
    // drops owner count and frees up reserve, inflating the post-balance by
    // one incremental-reserve. Read sfBalance directly for XRP to dodge
    // that. IOU / MPT trust lines have no reserve concept, so accountHolds
    // is fine for them.
    auto const holds = [&](AccountID const& acct) -> STAmount {
        if (vaultAsset.native())
        {
            auto const sle = view.read(keylet::account(acct));
            return sle ? sle->getFieldAmount(sfBalance) : STAmount{vaultAsset};
        }
        return accountHolds(
            view,
            acct,
            vaultAsset,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j);
    };

    // Pre-fixCleanup3_2_0 the helper performed no delta verification: silent
    // rounding by associateAsset on the STNumber accounting fields, or by
    // STAmount canonicalization on the destination's trust line, would be
    // caught (if at all) by the withdrawal invariants at finalize time, with
    // tecINVARIANT_FAILED. Post-amendment we snapshot the pre-state and
    // compare deltas at the end of the helper, returning tecPRECISION_LOSS
    // before invariants run.
    bool const verifyDeltas = view.rules().enabled(fixCleanup3_2_0);

    Number const beforeAssetsTotal = *vault->at(sfAssetsTotal);
    Number const beforeAssetsAvailable = *vault->at(sfAssetsAvailable);
    STAmount const beforeVaultBalance = verifyDeltas ? holds(vaultAccount) : STAmount{vaultAsset};
    STAmount const beforeDestinationBalance =
        verifyDeltas ? holds(destination) : STAmount{vaultAsset};

    [[maybe_unused]] Number const lossUnrealized = *vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (beforeAssetsTotal - beforeAssetsAvailable),
        "xrpl::withdrawFromVault : loss and assets do balance");

    // The vault must have enough assets on hand. The vault may hold assets
    // that it has already pledged. That is why we look at AssetAvailable
    // instead of the pseudo-account balance.
    if (beforeAssetsAvailable < Number{assetsWithdrawn})
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

    if (auto const ter = doWithdraw(
            view, tx, depositor, destination, vaultAccount, preFeeBalance, assetsWithdrawn, j);
        !isTesSuccess(ter))
        return ter;

    if (verifyDeltas)
    {
        // Verify every observable balance changed by exactly
        // `assetsWithdrawn`. Issuer-as-destination is a special case: sending
        // an IOU to the issuer destroys it rather than crediting the issuer's
        // balance, so skip the destination-side check there. This mirrors
        // the carve-out in the withdrawal invariant.
        Number const expected = assetsWithdrawn;

        if (beforeAssetsTotal - *vault->at(sfAssetsTotal) != expected ||
            beforeAssetsAvailable - *vault->at(sfAssetsAvailable) != expected)
        {
            JLOG(j.debug()) << "withdrawFromVault: vault accounting delta mismatch.";
            return tecPRECISION_LOSS;
        }

        if (Number{beforeVaultBalance - holds(vaultAccount)} != expected)
        {
            JLOG(j.debug()) << "withdrawFromVault: vault balance delta mismatch.";
            return tecPRECISION_LOSS;
        }

        bool const issuerWithdrawal = !vaultAsset.native() && destination == vaultAsset.getIssuer();
        if (!issuerWithdrawal && Number{holds(destination) - beforeDestinationBalance} != expected)
        {
            JLOG(j.debug()) << "withdrawFromVault: destination balance delta mismatch.";
            return tecPRECISION_LOSS;
        }
    }

    return tesSUCCESS;
}

}  // namespace xrpl
