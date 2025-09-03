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
    {
        JLOG(ctx.j.debug()) << "VaultDeposit: zero/empty vault ID.";
        return temMALFORMED;
    }

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
                << "VaultDeposit: vault assets are non-transferable.";
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
                << "VaultDeposit: missing issuer of vault assets.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    auto const mptIssuanceID = vault->at(sfShareMPTID);
    auto const vaultShare = MPTIssue(mptIssuanceID);
    if (vaultShare == assets.asset())
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error())
            << "VaultDeposit: vault shares and assets cannot be same.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error())
            << "VaultDeposit: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (sleIssuance->isFlag(lsfMPTLocked))
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error())
            << "VaultDeposit: issuance of vault shares is locked.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Cannot deposit inside Vault an Asset frozen for the depositor
    if (isFrozen(ctx.view, account, vaultAsset))
        return vaultAsset.holds<Issue>() ? tecFROZEN : tecLOCKED;

    // Cannot deposit if the shares of the vault are frozen
    if (isFrozen(ctx.view, account, vaultShare))
        return tecLOCKED;

    if (vault->isFlag(tfVaultPrivate) && account != vault->at(sfOwner))
    {
        auto const maybeDomainID = sleIssuance->at(~sfDomainID);
        // Since this is a private vault and the account is not its owner, we
        // perform authorization check based on DomainID read from sleIssuance.
        // Had the vault shares been a regular MPToken, we would allow
        // authorization granted by the Issuer explicitly, but Vault uses Issuer
        // pseudo-account, which cannot grant an authorization.
        if (maybeDomainID)
        {
            // As per validDomain documentation, we suppress tecEXPIRED error
            // here, so we can delete any expired credentials inside doApply.
            if (auto const err =
                    credentials::validDomain(ctx.view, *maybeDomainID, account);
                !isTesSuccess(err) && err != tecEXPIRED)
                return err;
        }
        else
            return tecNO_AUTH;
    }

    // Source MPToken must exist (if asset is an MPT)
    if (auto const ter = requireAuth(ctx.view, vaultAsset, account);
        !isTesSuccess(ter))
        return ter;

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
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const assets = ctx_.tx[sfAmount];
    // Make sure the depositor can hold shares.
    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = view().read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDeposit: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    auto const& vaultAccount = vault->at(sfAccount);
    // Note, vault owner is always authorized
    if ((vault->getFlags() & tfVaultPrivate) && account_ != vault->at(sfOwner))
    {
        if (auto const err = enforceMPTokenAuthorization(
                ctx_.view(), mptIssuanceID, account_, mPriorBalance, j_);
            !isTesSuccess(err))
            return err;
    }
    else
    {
        // No authorization needed, but must ensure there is MPToken
        auto sleMpt = view().read(keylet::mptoken(mptIssuanceID, account_));
        if (!sleMpt)
        {
            if (auto const err = authorizeMPToken(
                    view(),
                    mPriorBalance,
                    mptIssuanceID->value(),
                    account_,
                    ctx_.journal);
                !isTesSuccess(err))
                return err;
        }

        // If the vault is private, set the authorized flag for the vault owner
        if (vault->isFlag(tfVaultPrivate))
        {
            if (auto const err = authorizeMPToken(
                    view(),
                    mPriorBalance,              // priorBalance
                    mptIssuanceID->value(),     // mptIssuanceID
                    sleIssuance->at(sfIssuer),  // account
                    ctx_.journal,
                    {},       // flags
                    account_  // holderID
                );
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
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultDeposit: negative balance of account assets.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Transfer shares from vault to depositor.
    if (auto ter = accountSend(
            view(), vaultAccount, account_, shares, j_, WaiveTransferFee::Yes))
        return ter;

    return tesSUCCESS;
}

}  // namespace ripple
