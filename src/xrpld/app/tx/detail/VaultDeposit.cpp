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

#include <xrpld/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STNumber.h>
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
        return tecNO_AUTH;

    auto assets = ctx_.tx[sfAmount];
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

    vault->at(sfAssetTotal) += assets;
    vault->at(sfAssetAvailable) += assets;

    // A deposit must not push the vault over its limit.
    auto maximum = *vault->at(sfAssetMaximum);
    if (maximum != 0 && *vault->at(sfAssetTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer assets from depositor to vault.
    if (auto ter = accountSend(view(), account_, vaultAccount, assets, j_))
        return ter;

    // Transfer shares from vault to depositor.
    auto shares = assetsToSharesDeposit(view(), vault, assets);
    if (auto ter = accountSend(view(), vaultAccount, account_, shares, j_))
        return ter;

    view().update(vault);

    return tesSUCCESS;
}

}  // namespace ripple
