//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/VaultDeposit.h>

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultDeposit::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

TER
VaultDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    // Only the VaultDeposit transaction is subject to this permission check.
    if (vault->getFlags() == tfVaultPrivate &&
        ctx.tx[sfAccount] != vault->at(sfOwner))
    {
        if (auto const domain = vault->at(~sfVaultID))
        {
            if (credentials::authorizedDomain(
                    ctx.view, *domain, ctx.tx[sfAccount]) != tesSUCCESS)
                return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
VaultDeposit::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    // TODO: Check credentials.
    if (vault->getFlags() & lsfVaultPrivate)
        return tecNO_PERMISSION;

    auto const assets = ctx_.tx[sfAmount];
    Asset const& asset = vault->at(sfAsset);
    if (assets.asset() != asset)
        return tecWRONG_ASSET;

    if (accountHolds(
            view(),
            account_,
            asset,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
            j_) < assets)
    {
        return tecINSUFFICIENT_FUNDS;
    }

    // Make sure the depositor can hold shares.
    auto share = (*vault)[sfMPTokenIssuanceID];
    auto maybeToken = findToken(view(), MPTIssue(share), account_);
    if (!maybeToken)
    {
        if (maybeToken.error() == tecNO_LINE)
        {
            if (auto ter = MPTokenAuthorize::authorize(
                    view(),
                    j_,
                    {.priorBalance = mPriorBalance,
                     .mptIssuanceID = share,
                     .accountID = account_}))
                return ter;
        }
        else if (maybeToken.error() != tesSUCCESS)
        {
            return maybeToken.error();
        }
    }

    // Compute exchange before transferring any amounts.
    auto const shares = assetsToSharesDeposit(view(), vault, assets);
    XRPL_ASSERT(
        shares.asset() != assets.asset(), "do not mix up assets and shares");

    vault->at(sfAssetTotal) += assets;
    vault->at(sfAssetAvailable) += assets;
    view().update(vault);

    // A deposit must not push the vault over its limit.
    auto const maximum = *vault->at(sfAssetMaximum);
    if (maximum != 0 && *vault->at(sfAssetTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer assets from depositor to vault.
    if (auto ter = accountSend(view(), account_, vaultAccount, assets, j_))
        return ter;

    // Transfer shares from vault to depositor.
    if (auto ter = accountSend(view(), vaultAccount, account_, shares, j_))
        return ter;

    return tesSUCCESS;
}

}  // namespace ripple
