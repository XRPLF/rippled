#include <xrpld/app/tx/detail/VaultClawback.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultClawback::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::zero)
    {
        JLOG(ctx.j.debug()) << "VaultClawback: zero/empty vault ID.";
        return temMALFORMED;
    }

    AccountID const issuer = ctx.tx[sfAccount];
    AccountID const holder = ctx.tx[sfHolder];

    if (issuer == holder)
    {
        JLOG(ctx.j.debug()) << "VaultClawback: issuer cannot be holder.";
        return temMALFORMED;
    }

    auto const amount = ctx.tx[~sfAmount];
    if (amount)
    {
        // Note, zero amount is valid, it means "all". It is also the default.
        if (*amount < beast::zero)
            return temBAD_AMOUNT;
        else if (isXRP(amount->asset()))
        {
            JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback XRP.";
            return temMALFORMED;
        }
        else if (amount->asset().getIssuer() != issuer)
        {
            JLOG(ctx.j.debug())
                << "VaultClawback: only asset issuer can clawback.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
VaultClawback::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    auto account = ctx.tx[sfAccount];
    auto const issuer = ctx.view.read(keylet::account(account));
    if (!issuer)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultClawback: missing issuer account.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    Asset const vaultAsset = vault->at(sfAsset);
    if (auto const amount = ctx.tx[~sfAmount];
        amount && vaultAsset != amount->asset())
        return tecWRONG_ASSET;

    if (vaultAsset.native())
    {
        JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback XRP.";
        return tecNO_PERMISSION;  // Cannot clawback XRP.
    }
    else if (vaultAsset.getIssuer() != account)
    {
        JLOG(ctx.j.debug()) << "VaultClawback: only asset issuer can clawback.";
        return tecNO_PERMISSION;  // Only issuers can clawback.
    }

    if (vaultAsset.holds<MPTIssue>())
    {
        auto const mpt = vaultAsset.get<MPTIssue>();
        auto const mptIssue =
            ctx.view.read(keylet::mptIssuance(mpt.getMptID()));
        if (mptIssue == nullptr)
            return tecOBJECT_NOT_FOUND;

        std::uint32_t const issueFlags = mptIssue->getFieldU32(sfFlags);
        if (!(issueFlags & lsfMPTCanClawback))
        {
            JLOG(ctx.j.debug())
                << "VaultClawback: cannot clawback MPT vault asset.";
            return tecNO_PERMISSION;
        }
    }
    else if (vaultAsset.holds<Issue>())
    {
        std::uint32_t const issuerFlags = issuer->getFieldU32(sfFlags);
        if (!(issuerFlags & lsfAllowTrustLineClawback) ||
            (issuerFlags & lsfNoFreeze))
        {
            JLOG(ctx.j.debug())
                << "VaultClawback: cannot clawback IOU vault asset.";
            return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
VaultClawback::doApply()
{
    auto const& tx = ctx_.tx;
    auto const vault = view().peek(keylet::vault(tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = *((*vault)[sfShareMPTID]);
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultClawback: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    Asset const vaultAsset = vault->at(sfAsset);
    STAmount const amount = [&]() -> STAmount {
        auto const maybeAmount = tx[~sfAmount];
        if (maybeAmount)
            return *maybeAmount;
        return {sfAmount, vaultAsset, 0};
    }();
    XRPL_ASSERT(
        amount.asset() == vaultAsset,
        "ripple::VaultClawback::doApply : matching asset");

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);
    [[maybe_unused]] auto const lossUnrealized = vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (assetsTotal - assetsAvailable),
        "ripple::VaultClawback::doApply : loss and assets do balance");

    AccountID holder = tx[sfHolder];
    MPTIssue const share{mptIssuanceID};
    STAmount sharesDestroyed = {share};
    STAmount assetsRecovered;
    try
    {
        if (amount == beast::zero)
        {
            sharesDestroyed = accountHolds(
                view(),
                holder,
                share,
                FreezeHandling::fhIGNORE_FREEZE,
                AuthHandling::ahIGNORE_AUTH,
                j_);

            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesDestroyed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsRecovered = *maybeAssets;
        }
        else
        {
            assetsRecovered = amount;
            {
                auto const maybeShares =
                    assetsToSharesWithdraw(vault, sleIssuance, assetsRecovered);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesDestroyed = *maybeShares;
            }

            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesDestroyed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsRecovered = *maybeAssets;
        }

        // Clamp to maximum.
        if (assetsRecovered > *assetsAvailable)
        {
            assetsRecovered = *assetsAvailable;
            // Note, it is important to truncate the number of shares, otherwise
            // the corresponding assets might breach the AssetsAvailable
            {
                auto const maybeShares = assetsToSharesWithdraw(
                    vault, sleIssuance, assetsRecovered, TruncateShares::yes);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesDestroyed = *maybeShares;
            }

            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesDestroyed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsRecovered = *maybeAssets;
            if (assetsRecovered > *assetsAvailable)
            {
                // LCOV_EXCL_START
                JLOG(j_.error())
                    << "VaultClawback: invalid rounding of shares.";
                return tecINTERNAL;
                // LCOV_EXCL_STOP
            }
        }
    }
    catch (std::overflow_error const&)
    {
        // It's easy to hit this exception from Number with large enough Scale
        // so we avoid spamming the log and only use debug here.
        JLOG(j_.debug())  //
            << "VaultClawback: overflow error with"
            << " scale=" << (int)vault->at(sfScale).value()  //
            << ", assetsTotal=" << vault->at(sfAssetsTotal).value()
            << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount)
            << ", amount=" << amount.value();
        return tecPATH_DRY;
    }

    if (sharesDestroyed == beast::zero)
        return tecPRECISION_LOSS;

    assetsTotal -= assetsRecovered;
    assetsAvailable -= assetsRecovered;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from holder to vault.
    if (auto const ter = accountSend(
            view(),
            holder,
            vaultAccount,
            sharesDestroyed,
            j_,
            WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Try to remove MPToken for shares, if the holder balance is zero. Vault
    // pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    // Keep MPToken if holder is the vault owner.
    if (holder != vault->at(sfOwner))
    {
        if (auto const ter =
                removeEmptyHolding(view(), holder, sharesDestroyed.asset(), j_);
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

    // Transfer assets from vault to issuer.
    if (auto const ter = accountSend(
            view(),
            vaultAccount,
            account_,
            assetsRecovered,
            j_,
            WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Sanity check
    if (accountHolds(
            view(),
            vaultAccount,
            assetsRecovered.asset(),
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_) < beast::zero)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultClawback: negative balance of vault assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    return tesSUCCESS;
}

}  // namespace ripple
