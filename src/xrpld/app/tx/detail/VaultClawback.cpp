//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/tx/detail/VaultClawback.h>
//
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/TER.h>

#include <optional>

namespace ripple {

NotTEC
VaultClawback::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::zero)
    {
        JLOG(ctx.j.debug()) << "VaultClawback: zero/empty vault ID.";
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
    }

    return tesSUCCESS;
}

[[nodiscard]] STAmount
clawbackAmount(
    std::shared_ptr<SLE const> const& vault,
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
    auto const sleShareIssuance =
        ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleShareIssuance)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error())
            << "VaultClawback: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    Asset const share = MPTIssue{mptIssuanceID};

    // Ambiguous case: If Issuer is Owner they must specify the asset
    if (!maybeAmount && !vaultAsset.native() &&
        vaultAsset.getIssuer() == vault->at(sfOwner))
    {
        JLOG(ctx.j.debug())
            << "VaultClawback: must specify amount when issuer is owner.";
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
            JLOG(ctx.j.debug())
                << "VaultClawback: only vault owner can clawback shares.";
            return tecNO_PERMISSION;
        }

        auto const assetsTotal = vault->at(sfAssetsTotal);
        auto const assetsAvailable = vault->at(sfAssetsAvailable);
        auto const sharesTotal = sleShareIssuance->at(sfOutstandingAmount);

        // Owner can clawback funds when the vault has shares but no assets
        if (sharesTotal == 0 || (assetsTotal != 0 || assetsAvailable != 0))
        {
            JLOG(ctx.j.debug())
                << "VaultClawback: vault owner can clawback shares only"
                   " when vault has no assets.";
            return tecNO_PERMISSION;
        }

        // If amount is non-zero, the VaultOwner must burn all shares
        if (amount != beast::zero)
        {
            Number const& sharesHeld = accountHolds(
                ctx.view,
                holder,
                share,
                FreezeHandling::fhIGNORE_FREEZE,
                AuthHandling::ahIGNORE_AUTH,
                ctx.j);

            // The VaultOwner must burn all shares
            if (amount != sharesHeld)
            {
                JLOG(ctx.j.debug())
                    << "VaultClawback: vault owner must clawback all "
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
            JLOG(ctx.j.debug())
                << "VaultClawback: only asset issuer can clawback asset.";
            return tecNO_PERMISSION;
        }

        // The issuer cannot clawback from itself
        if (account == holder)
        {
            JLOG(ctx.j.debug())
                << "VaultClawback: issuer cannot be the holder.";
            return tecNO_PERMISSION;
        }

        return std::visit(
            [&]<ValidIssueType TIss>(TIss const& issue) -> TER {
                if constexpr (std::is_same_v<TIss, MPTIssue>)
                {
                    auto const mptIssue =
                        ctx.view.read(keylet::mptIssuance(issue.getMptID()));
                    if (mptIssue == nullptr)
                        return tecOBJECT_NOT_FOUND;

                    std::uint32_t const issueFlags =
                        mptIssue->getFieldU32(sfFlags);
                    if (!(issueFlags & lsfMPTCanClawback))
                    {
                        JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback "
                                               "MPT vault asset.";
                        return tecNO_PERMISSION;
                    }
                }
                else if constexpr (std::is_same_v<TIss, Issue>)
                {
                    auto const issuerSle =
                        ctx.view.read(keylet::account(account));
                    if (!issuerSle)
                    {
                        // LCOV_EXCL_START
                        JLOG(ctx.j.error())
                            << "VaultClawback: missing submitter account.";
                        return tefINTERNAL;
                        // LCOV_EXCL_STOP
                    }

                    std::uint32_t const issuerFlags =
                        issuerSle->getFieldU32(sfFlags);
                    if (!(issuerFlags & lsfAllowTrustLineClawback) ||
                        (issuerFlags & lsfNoFreeze))
                    {
                        JLOG(ctx.j.debug()) << "VaultClawback: cannot clawback "
                                               "IOU vault asset.";
                        return tecNO_PERMISSION;
                    }
                }
                return tesSUCCESS;
            },
            vaultAsset.value());
    }

    // Invalid asset
    return tecWRONG_ASSET;
}

Expected<std::pair<STAmount, STAmount>, TER>
VaultClawback::assetsToClawback(
    std::shared_ptr<SLE> const& vault,
    std::shared_ptr<SLE const> const& sleShareIssuance,
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

    if (clawbackAmount == beast::zero)
    {
        auto const sharesDestroyed = accountHolds(
            view(),
            holder,
            share,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_);
        auto const maybeAssets =
            sharesToAssetsWithdraw(vault, sleShareIssuance, sharesDestroyed);
        if (!maybeAssets)
            return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

        return std::make_pair(*maybeAssets, sharesDestroyed);
    }

    STAmount sharesDestroyed;
    STAmount assetsRecovered = clawbackAmount;
    try
    {
        {
            auto const maybeShares = assetsToSharesWithdraw(
                vault, sleShareIssuance, assetsRecovered);
            if (!maybeShares)
                return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
            sharesDestroyed = *maybeShares;
        }

        auto const maybeAssets =
            sharesToAssetsWithdraw(vault, sleShareIssuance, sharesDestroyed);
        if (!maybeAssets)
            return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
        assetsRecovered = *maybeAssets;

        // Clamp to maximum.
        if (assetsRecovered > *assetsAvailable)
        {
            assetsRecovered = *assetsAvailable;
            // Note, it is important to truncate the number of shares,
            // otherwise the corresponding assets might breach the
            // AssetsAvailable
            {
                auto const maybeShares = assetsToSharesWithdraw(
                    vault,
                    sleShareIssuance,
                    assetsRecovered,
                    TruncateShares::yes);
                if (!maybeShares)
                    return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
                sharesDestroyed = *maybeShares;
            }

            auto const maybeAssets = sharesToAssetsWithdraw(
                vault, sleShareIssuance, sharesDestroyed);
            if (!maybeAssets)
                return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
            assetsRecovered = *maybeAssets;
            if (assetsRecovered > *assetsAvailable)
            {
                // LCOV_EXCL_START
                JLOG(j_.error())
                    << "VaultClawback: invalid rounding of shares.";
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
    STAmount const amount = clawbackAmount(vault, tx[~sfAmount], account_);

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);

    [[maybe_unused]] auto const lossUnrealized = vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (assetsTotal - assetsAvailable),
        "ripple::VaultClawback::doApply : loss and assets do balance");

    AccountID holder = tx[sfHolder];
    STAmount sharesDestroyed = {share};
    STAmount assetsRecovered = {vault->at(sfAsset)};

    // The Owner is burning shares
    if (account_ == vault->at(sfOwner) && amount.asset() == share)
    {
        sharesDestroyed = accountHolds(
            view(),
            holder,
            share,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_);
    }
    else  // The Issuer is clawbacking vault assets
    {
        XRPL_ASSERT(
            amount.asset() == vaultAsset,
            "xrpl::VaultClawback::doApply : matching asset");

        auto const clawbackParts =
            assetsToClawback(vault, sleIssuance, holder, amount);
        if (!clawbackParts)
            return clawbackParts.error();

        assetsRecovered = clawbackParts->first;
        sharesDestroyed = clawbackParts->second;
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

    if (assetsRecovered > beast::zero)
    {
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
            JLOG(j_.error())
                << "VaultClawback: negative balance of vault assets.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    associateAsset(*vault, vaultAsset);

    return tesSUCCESS;
}

}  // namespace ripple
