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

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/tx/detail/VaultWithdraw.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultWithdraw::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (ctx.tx[sfVaultID] == beast::zero)
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination];
        destination.has_value())
    {
        if (*destination == beast::zero)
        {
            JLOG(ctx.j.debug())
                << "VaultWithdraw: zero/empty destination account.";
            return temMALFORMED;
        }
    }
    else if (ctx.tx.isFieldPresent(sfDestinationTag))
    {
        JLOG(ctx.j.debug()) << "VaultWithdraw: sfDestinationTag is set but "
                               "sfDestination is not";
        return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
VaultWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    auto const assets = ctx.tx[sfAmount];
    auto const vaultAsset = vault->at(sfAsset);
    auto const vaultShare = vault->at(sfShareMPTID);
    if (assets.asset() != vaultAsset && assets.asset() != vaultShare)
        return tecWRONG_ASSET;

    if (vaultAsset.native())
        ;  // No special checks for XRP
    else if (vaultAsset.holds<MPTIssue>())
    {
        auto mptID = vaultAsset.get<MPTIssue>().getMptID();
        auto issuance = ctx.view.read(keylet::mptIssuance(mptID));
        if (!issuance)
            return tecOBJECT_NOT_FOUND;
        if (!issuance->isFlag(lsfMPTCanTransfer))
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error())
                << "VaultWithdraw: vault assets are non-transferable.";
            return tecNO_AUTH;
            // LCOV_EXCL_STOP
        }
    }
    else if (vaultAsset.holds<Issue>())
    {
        auto const issuer =
            ctx.view.read(keylet::account(vaultAsset.getIssuer()));
        if (!issuer)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error())
                << "VaultWithdraw: missing issuer of vault assets.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    // Enforce valid withdrawal policy
    if (vault->at(sfWithdrawalPolicy) != vaultStrategyFirstComeFirstServe)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultWithdraw: invalid withdrawal policy.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const account = ctx.tx[sfAccount];
    auto const dstAcct = [&]() -> AccountID {
        if (ctx.tx.isFieldPresent(sfDestination))
            return ctx.tx.getAccountID(sfDestination);
        return account;
    }();

    // Withdrawal to a 3rd party destination account is essentially a transfer,
    // via shares in the vault. Enforce all the usual asset transfer checks.
    AuthType authType = AuthType::Legacy;
    if (account != dstAcct)
    {
        auto const sleDst = ctx.view.read(keylet::account(dstAcct));
        if (sleDst == nullptr)
            return tecNO_DST;

        if (sleDst->isFlag(lsfRequireDestTag) &&
            !ctx.tx.isFieldPresent(sfDestinationTag))
            return tecDST_TAG_NEEDED;  // Cannot send without a tag

        if (sleDst->isFlag(lsfDepositAuth))
        {
            if (!ctx.view.exists(keylet::depositPreauth(dstAcct, account)))
                return tecNO_PERMISSION;
        }
        // The destination account must have consented to receive the asset by
        // creating a RippleState or MPToken
        authType = AuthType::StrongAuth;
    }

    // Destination MPToken (for an MPT) or trust line (for an IOU) must exist
    // if not sending to Account.
    if (auto const ter = requireAuth(ctx.view, vaultAsset, dstAcct, authType);
        !isTesSuccess(ter))
        return ter;

    // Cannot withdraw from a Vault an Asset frozen for the destination account
    if (auto const ret = checkFrozen(ctx.view, dstAcct, vaultAsset))
        return ret;

    if (auto const ret = checkFrozen(ctx.view, account, vaultShare))
        return ret;

    return tesSUCCESS;
}

TER
VaultWithdraw::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = view().read(keylet::mptIssuance(*mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultWithdraw: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Note, we intentionally do not check lsfVaultPrivate flag on the Vault. If
    // you have a share in the vault, it means you were at some point authorized
    // to deposit into it, and this means you are also indefinitely authorized
    // to withdraw from it.

    auto const amount = ctx_.tx[sfAmount];
    Asset const vaultAsset = vault->at(sfAsset);
    auto const share = MPTIssue(*mptIssuanceID);
    STAmount sharesRedeemed = {share};
    STAmount assetsWithdrawn;
    try
    {
        if (amount.asset() == vaultAsset)
        {
            // Fixed assets, variable shares.
            {
                auto const maybeShares =
                    assetsToSharesWithdraw(vault, sleIssuance, amount);
                if (!maybeShares)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                sharesRedeemed = *maybeShares;
            }

            if (sharesRedeemed == beast::zero)
                return tecPRECISION_LOSS;
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else if (amount.asset() == share)
        {
            // Fixed shares, variable assets.
            sharesRedeemed = amount;
            auto const maybeAssets =
                sharesToAssetsWithdraw(vault, sleIssuance, sharesRedeemed);
            if (!maybeAssets)
                return tecINTERNAL;  // LCOV_EXCL_LINE
            assetsWithdrawn = *maybeAssets;
        }
        else
            return tefINTERNAL;  // LCOV_EXCL_LINE
    }
    catch (std::overflow_error const&)
    {
        // It's easy to hit this exception from Number with large enough Scale
        // so we avoid spamming the log and only use debug here.
        JLOG(j_.debug())  //
            << "VaultWithdraw: overflow error with"
            << " scale=" << (int)vault->at(sfScale).value()  //
            << ", assetsTotal=" << vault->at(sfAssetsTotal).value()
            << ", sharesTotal=" << sleIssuance->at(sfOutstandingAmount)
            << ", amount=" << amount.value();
        return tecPATH_DRY;
    }

    if (accountHolds(
            view(),
            account_,
            share,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahIGNORE_AUTH,
            j_) < sharesRedeemed)
    {
        JLOG(j_.debug()) << "VaultWithdraw: account doesn't hold enough shares";
        return tecINSUFFICIENT_FUNDS;
    }

    auto assetsAvailable = vault->at(sfAssetsAvailable);
    auto assetsTotal = vault->at(sfAssetsTotal);
    [[maybe_unused]] auto const lossUnrealized = vault->at(sfLossUnrealized);
    XRPL_ASSERT(
        lossUnrealized <= (assetsTotal - assetsAvailable),
        "ripple::VaultWithdraw::doApply : loss and assets do balance");

    // The vault must have enough assets on hand. The vault may hold assets
    // that it has already pledged. That is why we look at AssetAvailable
    // instead of the pseudo-account balance.
    if (*assetsAvailable < assetsWithdrawn)
    {
        JLOG(j_.debug()) << "VaultWithdraw: vault doesn't hold enough assets";
        return tecINSUFFICIENT_FUNDS;
    }

    assetsTotal -= assetsWithdrawn;
    assetsAvailable -= assetsWithdrawn;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from depositor to vault.
    if (auto const ter = accountSend(
            view(),
            account_,
            vaultAccount,
            sharesRedeemed,
            j_,
            WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Try to remove MPToken for shares, if the account balance is zero. Vault
    // pseudo-account will never set lsfMPTAuthorized, so we ignore flags.
    // Keep MPToken if holder is the vault owner.
    if (account_ != vault->at(sfOwner))
    {
        auto const ter =
            removeEmptyHolding(view(), account_, sharesRedeemed.asset(), j_);
        if (isTesSuccess(ter))
        {
            JLOG(j_.debug())  //
                << "VaultWithdraw: removed empty MPToken for vault shares"
                << " MPTID=" << to_string(*mptIssuanceID)  //
                << " account=" << toBase58(account_);
        }
        else if (ter != tecHAS_OBLIGATIONS)
        {
            // LCOV_EXCL_START
            JLOG(j_.error())  //
                << "VaultWithdraw: failed to remove MPToken for vault shares"
                << " MPTID=" << to_string(*mptIssuanceID)  //
                << " account=" << toBase58(account_)       //
                << " with result: " << transToken(ter);
            return ter;
            // LCOV_EXCL_STOP
        }
        // else quietly ignore, account balance is not zero
    }

    auto const dstAcct = [&]() -> AccountID {
        if (ctx_.tx.isFieldPresent(sfDestination))
            return ctx_.tx.getAccountID(sfDestination);
        return account_;
    }();

    // Transfer assets from vault to depositor or destination account.
    if (auto const ter = accountSend(
            view(),
            vaultAccount,
            dstAcct,
            assetsWithdrawn,
            j_,
            WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;

    // Sanity check
    if (accountHolds(
            view(),
            vaultAccount,
            assetsWithdrawn.asset(),
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_) < beast::zero)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultWithdraw: negative balance of vault assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    return tesSUCCESS;
}

}  // namespace ripple
