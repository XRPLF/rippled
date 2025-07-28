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
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
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

    auto amount = ctx_.tx[sfAmount];
    auto const asset = vault->at(sfAsset);
    auto const share = MPTIssue(mptIssuanceID);
    STAmount shares, assets;
    if (amount.asset() == asset)
    {
        // Fixed assets, variable shares.
        assets = amount;
        shares = assetsToSharesWithdraw(vault, sleIssuance, assets);
    }
    else if (amount.asset() == share)
    {
        // Fixed shares, variable assets.
        shares = amount;
        assets = sharesToAssetsWithdraw(vault, sleIssuance, shares);
    }
    else
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (accountHolds(
            view(),
            account_,
            share,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahIGNORE_AUTH,
            j_) < shares)
    {
        JLOG(j_.debug()) << "VaultWithdraw: account doesn't hold enough shares";
        return tecINSUFFICIENT_FUNDS;
    }

    // The vault must have enough assets on hand. The vault may hold assets that
    // it has already pledged. That is why we look at AssetAvailable instead of
    // the pseudo-account balance.
    if (*vault->at(sfAssetsAvailable) < assets)
    {
        JLOG(j_.debug()) << "VaultWithdraw: vault doesn't hold enough assets";
        return tecINSUFFICIENT_FUNDS;
    }

    vault->at(sfAssetsTotal) -= assets;
    vault->at(sfAssetsAvailable) -= assets;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from depositor to vault.
    if (auto ter = accountSend(
            view(), account_, vaultAccount, shares, j_, WaiveTransferFee::Yes))
        return ter;

    auto const dstAcct = [&]() -> AccountID {
        if (ctx_.tx.isFieldPresent(sfDestination))
            return ctx_.tx.getAccountID(sfDestination);
        return account_;
    }();

    // Transfer assets from vault to depositor or destination account.
    if (auto ter = accountSend(
            view(), vaultAccount, dstAcct, assets, j_, WaiveTransferFee::Yes))
        return ter;

    // Sanity check
    if (accountHolds(
            view(),
            vaultAccount,
            assets.asset(),
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
