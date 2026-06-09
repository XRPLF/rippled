#include <xrpl/tx/transactors/vault/VaultClawback.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/invariants/VaultInvariantData.h>

#include <optional>
#include <stdexcept>
#include <utility>

namespace xrpl {
NotTEC
VaultClawback::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "VaultClawback: zero/empty vault ID.";
        return temMALFORMED;
    }

    auto const amount = ctx.tx[~sfAmount];
    if (amount)
    {
        // Note, zero amount is valid, it means "all". It is also the default.
        if (*amount < beast::kZero)
        {
            return temBAD_AMOUNT;
        }
        if (isXRP(amount->asset()))
        {
            JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback XRP.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

[[nodiscard]] STAmount
clawbackAmount(
    SLE::const_ref vault,
    std::optional<STAmount> const& maybeAmount,
    AccountID const& account)
{
    if (maybeAmount)
        return *maybeAmount;

    Asset const share = MPTIssue{vault->at(sfShareMPTID)};
    if (account == vault->at(sfOwner))
        return STAmount{share};

    return STAmount{vault->at(sfAsset)};
}

TER
VaultClawback::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    Asset const vaultAsset = vault->at(sfAsset);
    auto const account = ctx.tx[sfAccount];
    auto const holder = ctx.tx[sfHolder];
    auto const maybeAmount = ctx.tx[~sfAmount];
    auto const mptIssuanceID = vault->at(sfShareMPTID);
    auto const sleShareIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleShareIssuance)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultClawback: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    Asset const share = MPTIssue{mptIssuanceID};

    // Ambiguous case: If Issuer is Owner they must specify the asset
    if (!maybeAmount && !vaultAsset.native() && vaultAsset.getIssuer() == vault->at(sfOwner))
    {
        JLOG(ctx.j.debug()) << "VaultClawback: must specify amount when issuer is owner.";
        return tecWRONG_ASSET;
    }

    auto const amount = clawbackAmount(vault, maybeAmount, account);

    // There is a special case that allows the VaultOwner to use clawback to
    // burn shares when Vault assets total and available are zero, but
    // shares remain. However, that case is handled in doApply() directly,
    // so here we just enforce checks.
    if (amount.asset() == share)
    {
        // Only the Vault Owner may clawback shares
        if (account != vault->at(sfOwner))
        {
            JLOG(ctx.j.debug()) << "VaultClawback: only vault owner can clawback shares.";
            return tecNO_PERMISSION;
        }

        auto const assetsTotal = vault->at(sfAssetsTotal);
        auto const assetsAvailable = vault->at(sfAssetsAvailable);
        auto const sharesTotal = sleShareIssuance->at(sfOutstandingAmount);

        // Owner can clawback funds when the vault has shares but no assets
        if (sharesTotal == 0 || (assetsTotal != 0 || assetsAvailable != 0))
        {
            JLOG(ctx.j.debug()) << "VaultClawback: vault owner can clawback shares only"
                                   " when vault has no assets.";
            return tecNO_PERMISSION;
        }

        // If amount is non-zero, the VaultOwner must burn all shares
        if (amount != beast::kZero)
        {
            Number const& sharesHeld = accountHolds(
                ctx.view,
                holder,
                share,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                ctx.j);

            // The VaultOwner must burn all shares
            if (amount != sharesHeld)
            {
                JLOG(ctx.j.debug()) << "VaultClawback: vault owner must clawback all "
                                       "shares.";
                return tecLIMIT_EXCEEDED;
            }
        }

        return tesSUCCESS;
    }

    // The asset that is being clawed back is the vault asset
    if (amount.asset() == vaultAsset)
    {
        // XRP cannot be clawed back
        if (vaultAsset.native())
        {
            JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback XRP.";
            return tecNO_PERMISSION;
        }

        // Only the Asset Issuer may clawback the asset
        if (account != vaultAsset.getIssuer())
        {
            JLOG(ctx.j.debug()) << "VaultClawback: only asset issuer can clawback asset.";
            return tecNO_PERMISSION;
        }

        // The issuer cannot clawback from itself
        if (account == holder)
        {
            JLOG(ctx.j.debug()) << "VaultClawback: issuer cannot be the holder.";
            return tecNO_PERMISSION;
        }

        return vaultAsset.visit(
            [&](MPTIssue const& issue) -> TER {
                auto const mptIssue = ctx.view.read(keylet::mptIssuance(issue.getMptID()));
                if (mptIssue == nullptr)
                    return tecOBJECT_NOT_FOUND;

                if (!mptIssue->isFlag(lsfMPTCanClawback))
                {
                    JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback "
                                           "MPT vault asset.";
                    return tecNO_PERMISSION;
                }

                return tesSUCCESS;
            },
            [&](Issue const&) -> TER {
                auto const issuerSle = ctx.view.read(keylet::account(account));
                if (!issuerSle)
                {
                    // LCOV_EXCL_START
                    JLOG(ctx.j.error()) << "VaultClawback: missing submitter account.";
                    return tefINTERNAL;
                    // LCOV_EXCL_STOP
                }

                if (!issuerSle->isFlag(lsfAllowTrustLineClawback) || issuerSle->isFlag(lsfNoFreeze))
                {
                    JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback "
                                           "IOU vault asset.";
                    return tecNO_PERMISSION;
                }

                return tesSUCCESS;
            });
    }

    // Invalid asset
    return tecWRONG_ASSET;
}

Expected<std::pair<STAmount, STAmount>, TER>
VaultClawback::assetsToClawback(
    SLE::ref vault,
    SLE::const_ref sleShareIssuance,
    AccountID const& holder,
    STAmount const& clawbackAmount)
{
    if (clawbackAmount.asset() != vault->at(sfAsset))
    {
        // preclaim should have blocked this , now it's an internal error
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultClawback: asset mismatch in clawback.";
        return Unexpected(tecINTERNAL);
        // LCOV_EXCL_STOP
    }

    auto const assetsAvailable = vault->at(sfAssetsAvailable);
    auto const mptIssuanceID = *vault->at(sfShareMPTID);
    MPTIssue const share{mptIssuanceID};

    // Pre-fixCleanup3_1_3: zero-amount clawback returned early without
    // clamping to assetsAvailable, allowing more assets to be recovered
    // than available when there was an outstanding loan. Retained for
    // ledger replay compatibility.
    if (!ctx_.view().rules().enabled(fixCleanup3_1_3) && clawbackAmount == beast::kZero)
    {
        auto const sharesDestroyed = accountHolds(
            view(), holder, share, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, j_);
        auto const maybeAssets = sharesToAssetsWithdraw(vault, sleShareIssuance, sharesDestroyed);
        if (!maybeAssets)
            return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

        return std::make_pair(*maybeAssets, sharesDestroyed);
    }

    STAmount sharesDestroyed;
    STAmount assetsRecovered;

    try
    {
        if (clawbackAmount == beast::kZero)
        {
            sharesDestroyed = accountHolds(
                view(), holder, share, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, j_);
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleShareIssuance, sharesDestroyed);
            if (!maybeAssets)
                return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

            assetsRecovered = *maybeAssets;
        }
        else
        {
            auto const maybeShares =
                assetsToSharesWithdraw(vault, sleShareIssuance, clawbackAmount);
            if (!maybeShares)
                return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
            sharesDestroyed = *maybeShares;

            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleShareIssuance, sharesDestroyed);
            if (!maybeAssets)
                return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
            assetsRecovered = *maybeAssets;
        }
        // Clamp to maximum.
        if (assetsRecovered > *assetsAvailable)
        {
            assetsRecovered = *assetsAvailable;
            // Note, it is important to truncate the number of shares,
            // otherwise the corresponding assets might breach the
            // AssetsAvailable
            {
                auto const maybeShares = assetsToSharesWithdraw(
                    vault, sleShareIssuance, assetsRecovered, TruncateShares::Yes);
                if (!maybeShares)
                    return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
                sharesDestroyed = *maybeShares;
            }

            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleShareIssuance, sharesDestroyed);
            if (!maybeAssets)
                return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
            assetsRecovered = *maybeAssets;
            if (assetsRecovered > *assetsAvailable)
            {
                // LCOV_EXCL_START
                JLOG(j_.error()) << "VaultClawback: invalid rounding of shares.";
                return Unexpected(tecINTERNAL);
                // LCOV_EXCL_STOP
            }
        }
    }
    catch (std::overflow_error const&)
    {
        // It's easy to hit this exception from Number with large enough
        // Scale so we avoid spamming the log and only use debug here.
        JLOG(j_.debug())  //
            << "VaultClawback: overflow error with"
            << " scale=" << (int)vault->at(sfScale).value()  //
            << ", assetsTotal=" << vault->at(sfAssetsTotal).value()
            << ", sharesTotal=" << sleShareIssuance->at(sfOutstandingAmount)
            << ", amount=" << clawbackAmount.value();
        return Unexpected(tecPATH_DRY);
    }

    return std::make_pair(assetsRecovered, sharesDestroyed);
}

TER
VaultClawback::doApply()
{
    auto const& tx = ctx_.tx;
    auto const vault = view().peek(keylet::vault(tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = *vault->at(sfShareMPTID);
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultClawback: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }
    MPTIssue const share{mptIssuanceID};

    Asset const vaultAsset = vault->at(sfAsset);
    STAmount const amount = clawbackAmount(vault, tx[~sfAmount], accountID_);

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);

    [[maybe_unused]] auto const lossUnrealized = vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (assetsTotal - assetsAvailable),
        "xrpl::VaultClawback::doApply : loss and assets do balance");

    AccountID const holder = tx[sfHolder];
    STAmount sharesDestroyed = {share};
    STAmount assetsRecovered = {vault->at(sfAsset)};

    // The Owner is burning shares
    if (accountID_ == vault->at(sfOwner) && amount.asset() == share)
    {
        sharesDestroyed = accountHolds(
            view(), holder, share, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, j_);
    }
    else  // The Issuer is clawbacking vault assets
    {
        XRPL_ASSERT(amount.asset() == vaultAsset, "xrpl::VaultClawback::doApply : matching asset");

        auto const clawbackParts = assetsToClawback(vault, sleIssuance, holder, amount);
        if (!clawbackParts)
            return clawbackParts.error();

        assetsRecovered = clawbackParts->first;
        sharesDestroyed = clawbackParts->second;
    }

    if (sharesDestroyed == beast::kZero)
        return tecPRECISION_LOSS;

    assetsTotal -= assetsRecovered;
    assetsAvailable -= assetsRecovered;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from holder to vault.
    if (auto const ter =
            accountSend(view(), holder, vaultAccount, sharesDestroyed, j_, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Try to remove MPToken for shares, if the holder balance is zero. Vault
    // pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    // Keep MPToken if holder is the vault owner.
    if (holder != vault->at(sfOwner))
    {
        if (auto const ter = removeEmptyHolding(view(), holder, sharesDestroyed.asset(), j_);
            isTesSuccess(ter))
        {
            JLOG(j_.debug())  //
                << "VaultClawback: removed empty MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(holder);
        }
        else if (ter != tecHAS_OBLIGATIONS)
        {
            // LCOV_EXCL_START
            JLOG(j_.error())  //
                << "VaultClawback: failed to remove MPToken for vault shares"
                << " MPTID=" << to_string(mptIssuanceID)  //
                << " account=" << toBase58(holder)        //
                << " with result: " << transToken(ter);
            return ter;
            // LCOV_EXCL_STOP
        }
        // else quietly ignore, holder balance is not zero
    }

    if (assetsRecovered > beast::kZero)
    {
        // Transfer assets from vault to issuer.
        if (auto const ter = accountSend(
                view(), vaultAccount, accountID_, assetsRecovered, j_, WaiveTransferFee::Yes);
            !isTesSuccess(ter))
            return ter;

        // Sanity check
        if (accountHolds(
                view(),
                vaultAccount,
                assetsRecovered.asset(),
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                j_) < beast::kZero)
        {
            // LCOV_EXCL_START
            JLOG(j_.error()) << "VaultClawback: negative balance of vault assets.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    associateAsset(*vault, vaultAsset);

    return tesSUCCESS;
}

void
VaultClawback::visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    data_.visitEntry(isDelete, before, after);
}

bool
VaultClawback::finalizeInvariants(
    STTx const& tx,
    TER result,
    XRPAmount,
    ReadView const& view,
    beast::Journal const& j)
{
    static constexpr Number kZero{};
    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    if (!isTesSuccess(result))
        return true;  // Do not perform checks

    auto const& afterVaults = data_.afterVaults();
    if (afterVaults.empty())
        return true;

    auto const& afterVault = afterVaults[0];
    auto const& vaultAsset = afterVault.asset;

    auto const& beforeVaults = data_.beforeVaults();
    XRPL_ASSERT(
        !beforeVaults.empty(),
        "xrpl::VaultClawback::finalizeInvariants : clawback updated a vault");
    auto const& beforeVault = beforeVaults[0];

    // Retrieve the before-shares for this vault (from beforeMPTs_).
    auto const beforeShares = [&]() -> std::optional<VaultInvariantData::Shares> {
        for (auto const& e : data_.beforeMPTIssuances())
        {
            if (e.share.getMptID() == beforeVault.shareMPTID)
                return e;
        }
        return std::nullopt;
    }();

    auto const isVaultEmpty = [](VaultInvariantData::Vault const& v) -> bool {
        return v.assetsAvailable == Number{} && v.assetsTotal == Number{};
    };

    // Check 1: clawback is either by the asset issuer, OR by the vault owner
    // on an empty vault (force-burn of lingering shares).
    if (vaultAsset.native() || vaultAsset.getIssuer() != tx[sfAccount])
    {
        if (!(beforeShares && beforeShares->sharesTotal > 0 && isVaultEmpty(beforeVault) &&
              beforeVault.owner == tx[sfAccount]))
        {
            JLOG(j.fatal())
                << "Invariant failed: "  //
                << "clawback may only be performed by the asset issuer, or by the vault "
                << "owner of an empty vault";
            XRPL_ASSERT(
                enforce,
                "xrpl::VaultClawback::finalizeInvariants : clawback issuer or empty vault owner");
            return !enforce;  // That's all we can do
        }
    }

    bool checkResult = true;

    auto const maybeVaultDeltaAssets = data_.deltaAssets(afterVault.pseudoId);
    if (maybeVaultDeltaAssets)
    {
        auto const minScale = data_.computeVaultMinScale(*maybeVaultDeltaAssets, view.rules());
        auto const vaultDeltaAssets =
            roundToAsset(vaultAsset, maybeVaultDeltaAssets->delta, minScale);

        // Check 2: vault balance decreased.
        if (vaultDeltaAssets >= kZero)
        {
            JLOG(j.fatal()) << "Invariant failed: clawback must decrease vault balance";
            checkResult = false;
        }

        // Check 3: assetsTotal changed by vaultDelta.
        auto const assetsTotalDelta =
            roundToAsset(vaultAsset, afterVault.assetsTotal - beforeVault.assetsTotal, minScale);
        if (assetsTotalDelta != vaultDeltaAssets)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: clawback and assets outstanding must add up";
            checkResult = false;
        }

        // Check 4: assetsAvailable changed by vaultDelta.
        auto const assetAvailableDelta = roundToAsset(
            vaultAsset, afterVault.assetsAvailable - beforeVault.assetsAvailable, minScale);
        if (assetAvailableDelta != vaultDeltaAssets)
        {
            JLOG(j.fatal()) <<  //
                "Invariant failed: clawback and assets available must add up";
            checkResult = false;
        }
    }
    else if (!isVaultEmpty(beforeVault))
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: clawback must change vault balance";
        XRPL_ASSERT(
            enforce, "xrpl::VaultClawback::finalizeInvariants : clawback vault balance invariant");
        return !enforce;  // That's all we can do
    }

    // Check 5: holder shares (tx[sfHolder]) decreased.
    // We don't need to round shares, they are integral MPT.
    auto const maybeAccountDeltaShares = data_.deltaShares(tx[sfHolder]);
    if (!maybeAccountDeltaShares)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: clawback must change holder shares";
        XRPL_ASSERT(
            enforce, "xrpl::VaultClawback::finalizeInvariants : clawback holder shares invariant");
        if (!checkResult)
        {
            XRPL_ASSERT(
                enforce, "xrpl::VaultClawback::finalizeInvariants : vault clawback invariants");
        }
        return !enforce;  // That's all we can do
    }
    if (maybeAccountDeltaShares->delta >= kZero)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: clawback must decrease holder shares";
        checkResult = false;
    }

    // Check 6: vault shares (outstanding) changed by same amount as holder decrease.
    // We don't need to round shares, they are integral MPT.
    auto const vaultDeltaShares = data_.deltaShares(afterVault.pseudoId);
    if (!vaultDeltaShares || vaultDeltaShares->delta == kZero)
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: clawback must change vault shares";
        XRPL_ASSERT(
            enforce, "xrpl::VaultClawback::finalizeInvariants : clawback vault shares invariant");
        if (!checkResult)
        {
            XRPL_ASSERT(
                enforce, "xrpl::VaultClawback::finalizeInvariants : vault clawback invariants");
        }
        return !enforce;  // That's all we can do
    }

    if (vaultDeltaShares->delta * -1 != maybeAccountDeltaShares->delta)
    {
        JLOG(j.fatal()) << "Invariant failed: "  //
                        << "clawback must change holder and vault shares by equal amount";
        checkResult = false;
    }

    if (!checkResult)
    {
        XRPL_ASSERT(enforce, "xrpl::VaultClawback::finalizeInvariants : vault clawback invariants");
        return !enforce;
    }

    return true;
}

}  // namespace xrpl
