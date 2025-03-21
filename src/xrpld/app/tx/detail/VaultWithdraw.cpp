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

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    if (ctx.tx.isFieldPresent(sfDestination))
    {
        if (auto const dstAccountID = ctx.tx.getAccountID(sfDestination);
            dstAccountID == beast::zero)
            return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
VaultWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    if (ctx.tx.isFieldPresent(sfDestination))
    {
        auto const dstAccountID = ctx.tx.getAccountID(sfDestination);
        if (auto const sleDst = ctx.view.read(keylet::account(dstAccountID));
            sleDst == nullptr)
            return tecNO_DST;
    }

    // Enforce valid withdrawal policy
    if (vault->at(sfWithdrawalPolicy) != vaultStrategyFirstComeFirstServe)
        return tefINTERNAL;

    auto const assets = ctx.tx[sfAmount];
    auto const asset = vault->at(sfAsset);
    auto const share = vault->at(sfMPTokenIssuanceID);
    if (assets.asset() != asset && assets.asset() != share)
        return tecWRONG_ASSET;

    auto const account = ctx.tx[sfAccount];
    auto const dstAcct = [&]() -> AccountID {
        if (ctx.tx.isFieldPresent(sfDestination))
            return ctx.tx.getAccountID(sfDestination);
        return account;
    }();

    if (account != dstAcct && assets.holds<MPTIssue>())
    {
        auto mptID = assets.get<MPTIssue>().getMptID();
        auto issuance = ctx.view.read(keylet::mptIssuance(mptID));
        if (!issuance)
            return tecNO_ENTRY;
        if ((issuance->getFlags() & lsfMPTCanTransfer) == 0)
            return tecNO_AUTH;
    }

    // Cannot withdraw from a Vault an Asset frozen for the destination account
    if (isFrozen(ctx.view, dstAcct, asset))
        return tecFROZEN;

    if (isFrozen(ctx.view, account, share))
        return tecFROZEN;

    return tesSUCCESS;
}

TER
VaultWithdraw::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // Enforced in preclaim

    auto const mptIssuanceID = (*vault)[sfMPTokenIssuanceID];
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;

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
        return tefINTERNAL;

    if (accountHolds(
            view(),
            account_,
            share,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahIGNORE_AUTH,
            j_) < shares)
    {
        return tecINSUFFICIENT_FUNDS;
    }

    // The vault must have enough assets on hand. The vault may hold assets that
    // it has already pledged. That is why we look at AssetAvailable instead of
    // the pseudo-account balance.
    if (*vault->at(sfAssetAvailable) < assets)
        return tecINSUFFICIENT_FUNDS;

    vault->at(sfAssetTotal) -= assets;
    vault->at(sfAssetAvailable) -= assets;
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
        return tefINTERNAL;

    return tesSUCCESS;
}

}  // namespace ripple
