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
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/app/tx/detail/VaultDeposit.h>
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

    if (ctx.tx[sfVaultID] == beast::zero)
        return temMALFORMED;

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    return preflight2(ctx);
}

TER
VaultDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    auto const account = ctx.tx[sfAccount];
    auto const assets = ctx.tx[sfAmount];
    auto const vaultAsset = vault->at(sfAsset);
    if (assets.asset() != vaultAsset)
        return tecWRONG_ASSET;

    // Cannot deposit inside Vault an Asset frozen for the depositor
    if (isFrozen(ctx.view, account, vaultAsset))
        return tecFROZEN;

    auto const share = MPTIssue(vault->at(sfMPTokenIssuanceID));
    // Cannot deposit if the shares of the vault are frozen
    if (isFrozen(ctx.view, account, share))
        return tecFROZEN;

    // Defensive check, given above `if (asset.asset() != vaultAsset)`
    if (share == assets.asset())
        return tecWRONG_ASSET;

    if ((vault->getFlags() & tfVaultPrivate) && account != vault->at(sfOwner))
    {
        // The authorization check below is based on DomainID stored in
        // MPTokenIssuance. Had the vault shares been a regular MPToken, we
        // would allow authorization granted by the issuer explicitly, but Vault
        // does not have an MPT issuer (instead it uses pseudo-account, which is
        // blackholed and cannot create any transactions).
        auto const err = requireAuth(ctx.view, share, account);

        // As per requireAuth spec, we suppress tecEXPIRED error here, so we can
        // delete any expired credentials inside doApply.
        if (err != tecEXPIRED)
            return err;
    }

    if (assets.holds<MPTIssue>())
    {
        auto mptID = assets.get<MPTIssue>().getMptID();
        auto issuance = ctx.view.read(keylet::mptIssuance(mptID));
        if (!issuance)
            return tecOBJECT_NOT_FOUND;
        if ((issuance->getFlags() & lsfMPTCanTransfer) == 0)
            return tecNO_AUTH;
    }

    if (accountHolds(
            ctx.view,
            account,
            vaultAsset,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
            ctx.j) < assets)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
VaultDeposit::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // Enforced in preclaim

    auto const assets = ctx_.tx[sfAmount];

    // Make sure the depositor can hold shares.
    auto const mptIssuanceID = (*vault)[sfMPTokenIssuanceID];
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;

    auto const& vaultAccount = vault->at(sfAccount);
    MPTIssue const mptIssue(mptIssuanceID);
    // Note, vault owner is always authorized
    if ((vault->getFlags() & tfVaultPrivate) && account_ != vault->at(sfOwner))
    {
        if (auto const err = enforceMPTokenAuthorization(
                ctx_.view(), mptIssue, account_, mPriorBalance, j_);
            !isTesSuccess(err))
            return err;
    }
    else
    {
        // No authorization needed, but must ensure there is MPToken
        auto sleMpt = view().read(keylet::mptoken(mptIssuanceID, account_));
        if (!sleMpt)
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
    auto const shares = assetsToSharesDeposit(vault, sleIssuance, assets);
    XRPL_ASSERT(
        shares.asset() != assets.asset(),
        "ripple::VaultDeposit::doApply : assets are not shares");

    vault->at(sfAssetsTotal) += assets;
    vault->at(sfAssetsAvailable) += assets;
    view().update(vault);

    // A deposit must not push the vault over its limit.
    auto const maximum = *vault->at(sfAssetsMaximum);
    if (maximum != 0 && *vault->at(sfAssetsTotal) > maximum)
        return tecLIMIT_EXCEEDED;

    // Transfer assets from depositor to vault.
    if (auto ter = accountSend(
            view(), account_, vaultAccount, assets, j_, WaiveTransferFee::Yes))
        return ter;

    // Sanity check
    if (accountHolds(
            view(),
            account_,
            assets.asset(),
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_) < beast::zero)
        return tefINTERNAL;

    // Transfer shares from vault to depositor.
    if (auto ter = accountSend(
            view(), vaultAccount, account_, shares, j_, WaiveTransferFee::Yes))
        return ter;

    return tesSUCCESS;
}

}  // namespace ripple
