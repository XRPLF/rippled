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
#include <xrpld/ledger/View.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultClawback::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ter = preflight1(ctx, tfUniversalMask))
        return ter;

    if (ctx.tx[sfVaultID] == beast::zero)
        return temMALFORMED;

    AccountID const issuer = ctx.tx[sfAccount];
    AccountID const holder = ctx.tx[sfHolder];

    if (issuer == holder)
        return temMALFORMED;

    auto const amount = ctx.tx[~sfAmount];
    if (amount)
    {
        // Note, zero amount is valid, it means "all". It is also the default.
        if (*amount < beast::zero)
            return temBAD_AMOUNT;
        else if (isXRP(amount->asset()))
            return temMALFORMED;
        else if (amount->asset().getIssuer() != issuer)
            return temMALFORMED;
    }

    return preflight2(ctx);
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
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Asset const vaultAsset = vault->at(sfAsset);
    if (auto const amount = ctx.tx[~sfAmount];
        amount && vaultAsset != amount->asset())
        return tecWRONG_ASSET;

    if (vaultAsset.native())
        return tecNO_PERMISSION;  // Cannot clawback XRP.
    else if (vaultAsset.getIssuer() != account)
        return tecNO_PERMISSION;  // Only issuers can clawback.

    if (vaultAsset.holds<MPTIssue>())
    {
        auto const mpt = vaultAsset.get<MPTIssue>();
        auto const mptIssue =
            ctx.view.read(keylet::mptIssuance(mpt.getMptID()));
        if (mptIssue == nullptr)
            return tecOBJECT_NOT_FOUND;

        std::uint32_t const issueFlags = mptIssue->getFieldU32(sfFlags);
        if (!(issueFlags & lsfMPTCanClawback))
            return tecNO_PERMISSION;
    }
    else if (vaultAsset.holds<Issue>())
    {
        std::uint32_t const issuerFlags = issuer->getFieldU32(sfFlags);
        if (!(issuerFlags & lsfAllowTrustLineClawback) ||
            (issuerFlags & lsfNoFreeze))
            return tecNO_PERMISSION;
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

    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Asset const asset = vault->at(sfAsset);
    STAmount const amount = [&]() -> STAmount {
        auto const maybeAmount = tx[~sfAmount];
        if (maybeAmount)
            return *maybeAmount;
        return {sfAmount, asset, 0};
    }();
    XRPL_ASSERT(
        amount.asset() == asset,
        "ripple::VaultClawback::doApply : matching asset");

    AccountID holder = tx[sfHolder];
    STAmount assets, shares;
    if (amount == beast::zero)
    {
        Asset share = *(*vault)[sfShareMPTID];
        shares = accountHolds(
            view(),
            holder,
            share,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_);
        assets = sharesToAssetsWithdraw(vault, sleIssuance, shares);
    }
    else
    {
        assets = amount;
        shares = assetsToSharesWithdraw(vault, sleIssuance, assets);
    }

    // Clamp to maximum.
    Number maxAssets = *vault->at(sfAssetsAvailable);
    if (assets > maxAssets)
    {
        assets = maxAssets;
        shares = assetsToSharesWithdraw(vault, sleIssuance, assets);
    }

    if (shares == beast::zero)
        return tecINSUFFICIENT_FUNDS;

    vault->at(sfAssetsTotal) -= assets;
    vault->at(sfAssetsAvailable) -= assets;
    view().update(vault);

    auto const& vaultAccount = vault->at(sfAccount);
    // Transfer shares from holder to vault.
    if (auto ter = accountSend(
            view(), holder, vaultAccount, shares, j_, WaiveTransferFee::Yes))
        return ter;

    // Transfer assets from vault to issuer.
    if (auto ter = accountSend(
            view(), vaultAccount, account_, assets, j_, WaiveTransferFee::Yes))
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
