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

#include <xrpld/app/tx/detail/VaultClawback.h>

#include <xrpld/ledger/View.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultClawback::preflight(PreflightContext const& ctx)
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
VaultClawback::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    auto account = ctx.tx[sfAccount];
    Asset const asset = vault->at(sfAsset);
    if (asset.native())
        return tecNO_PERMISSION;  // Cannot clawback XRP.
    else if (asset.getIssuer() != account)
        return tecNO_PERMISSION;  // Only issuers can clawback.

    STAmount const amount = ctx.tx[sfAmount];
    if (asset != amount.asset())
        return tecWRONG_ASSET;

    return tesSUCCESS;
}

TER
VaultClawback::doApply()
{
    auto const& tx = ctx_.tx;
    auto const vault = view().peek(keylet::vault(tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    STAmount const amount = tx[sfAmount];
    AccountID holder = tx[sfHolder];
    STAmount assets, shares;
    if (amount == beast::zero)
    {
        Asset share = *(*vault)[sfMPTokenIssuanceID];
        shares = accountHolds(
            view(),
            holder,
            share,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_);
        assets = sharesToAssetsWithdraw(view(), vault, shares);
    }
    else
    {
        assets = amount;
        shares = assetsToSharesWithdraw(view(), vault, assets);
    }

    // Clamp to maximum.
    Number maxAssets = *vault->at(sfAssetAvailable);
    if (assets > maxAssets)
    {
        assets = maxAssets;
        shares = assetsToSharesWithdraw(view(), vault, assets);
    }

    if (shares == beast::zero)
        return tecINSUFFICIENT_FUNDS;

    vault->at(sfAssetTotal) -= assets;
    vault->at(sfAssetAvailable) -= assets;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from holder to vault.
    if (auto ter = accountSend(view(), holder, vaultAccount, shares, j_))
        return ter;

    // Transfer assets from vault to issuer.
    if (auto ter = accountSend(view(), vaultAccount, account_, assets, j_))
        return ter;

    return tesSUCCESS;
}

}  // namespace ripple
