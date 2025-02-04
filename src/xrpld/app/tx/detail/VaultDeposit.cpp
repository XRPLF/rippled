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
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
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

    auto account = ctx.tx[sfAccount];
    auto const assets = ctx.tx[sfAmount];
    auto asset = vault->at(sfAsset);
    if (assets.asset() != asset)
        return tecWRONG_ASSET;

    // Cannot deposit inside Vault an Asset frozen for the depositor
    if (isFrozen(ctx.view, account, asset))
        return tecFROZEN;

    if (vault->getFlags() == tfVaultPrivate && account != vault->at(sfOwner))
    {
        auto const err = requireAuth(
            ctx.view, MPTIssue(vault->at(sfMPTokenIssuanceID)), account);
        return err;

        // The above will perform authorization check based on DomainID stored
        // in MPTokenIssuance. Had this been a regular MPToken, it would also
        // allow use of authorization granted by the issuer explicitly, but
        // Vault does not have an MPT issuer (instead it uses pseudo-account).
        //
        // If we passed the above check then we also need to do similar check
        // inside doApply(), in order to check for expired credentials.
    }

    return tesSUCCESS;
}

TER
VaultDeposit::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    auto const assets = ctx_.tx[sfAmount];
    Asset const& asset = vault->at(sfAsset);

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
    auto const mptIssuanceID = (*vault)[sfMPTokenIssuanceID];
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;

    auto const& vaultAccount = vault->at(sfAccount);

    MPTIssue const mptIssue(mptIssuanceID);
    if (vault->getFlags() == tfVaultPrivate)
    {
        if (auto const err =
                verifyAuth(ctx_.view(), mptIssue, account_, mPriorBalance, j_);
            !isTesSuccess(err))
            return err;
    }
    else
    {
        // No authorization needed, but must ensure there is MPToken
        auto sleMpt = view().read(keylet::mptoken(mptIssuanceID, account_));
        if (!sleMpt && account_ != vaultAccount)
        {
            if (auto const err = MPTokenAuthorize::authorize(
                    view(),
                    ctx_.journal,
                    {.priorBalance = mPriorBalance,
                     // The operator-> gives the underlying STUInt192
                     // whose value function returns a const&.
                     .mptIssuanceID = mptIssuanceID->value(),
                     .accountID = account_});
                !isTesSuccess(err))
                return err;
        }
    }

    // Compute exchange before transferring any amounts.
    auto const shares = assetsToSharesDeposit(view(), vault, assets);
    XRPL_ASSERT(
        shares.asset() != assets.asset(),
        "ripple::VaultDeposit::doApply : assets are not shares");

    vault->at(sfAssetTotal) += assets;
    vault->at(sfAssetAvailable) += assets;
    view().update(vault);

    // A deposit must not push the vault over its limit.
    auto const maximum = *vault->at(sfAssetMaximum);
    if (maximum != 0 && *vault->at(sfAssetTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    // Transfer assets from depositor to vault.
    if (auto ter = accountSend(view(), account_, vaultAccount, assets, j_))
        return ter;

    // Transfer shares from vault to depositor.
    if (auto ter = accountSend(view(), vaultAccount, account_, shares, j_))
        return ter;

    return tesSUCCESS;
}

}  // namespace ripple
