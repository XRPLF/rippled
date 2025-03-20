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

#include <xrpld/app/tx/detail/VaultDelete.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultDelete::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ter = preflight1(ctx, tfUniversalMask))
        return ter;

    if (ctx.tx[sfVaultID] == beast::zero)
        return temMALFORMED;

    return preflight2(ctx);
}

TER
VaultDelete::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    if (vault->at(sfOwner) != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    if (vault->at(sfAssetsAvailable) != 0)
        return tecHAS_OBLIGATIONS;

    if (vault->at(sfAssetsTotal) != 0)
        return tecHAS_OBLIGATIONS;

    // Verify we can destroy MPTokenIssuance
    auto const sleMPT =
        ctx.view.read(keylet::mptIssuance(vault->at(sfShareMPTID)));

    if (!sleMPT)
        return tecOBJECT_NOT_FOUND;

    if (sleMPT->at(sfIssuer) != vault->getAccountID(sfAccount))
        return tecNO_PERMISSION;

    if (sleMPT->at(sfOutstandingAmount) != 0)
        return tecHAS_OBLIGATIONS;

    return tesSUCCESS;
}

TER
VaultDelete::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Destroy the asset holding.
    auto asset = vault->at(sfAsset);
    if (auto ter = removeEmptyHolding(view(), vault->at(sfAccount), asset, j_);
        !isTesSuccess(ter))
        return ter;

    auto const& pseudoID = vault->at(sfAccount);
    auto const pseudoAcct = view().peek(keylet::account(pseudoID));
    if (!pseudoAcct)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Destroy the share issuance. Do not use MPTokenIssuanceDestroy for this,
    // no special logic needed. First run few checks, duplicated from preclaim.
    auto const mpt = view().peek(keylet::mptIssuance(vault->at(sfShareMPTID)));
    if (!mpt)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(
            keylet::ownerDir(pseudoID), (*mpt)[sfOwnerNode], mpt->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    adjustOwnerCount(view(), pseudoAcct, -1, j_);

    view().erase(mpt);

    // The pseudo-account's directory should have been deleted already.
    if (view().peek(keylet::ownerDir(pseudoID)))
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE

    // Destroy the pseudo-account.
    view().erase(view().peek(keylet::account(pseudoID)));

    // Remove the vault from its owner's directory.
    auto const ownerID = vault->at(sfOwner);
    if (!view().dirRemove(
            keylet::ownerDir(ownerID),
            vault->at(sfOwnerNode),
            vault->key(),
            false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const owner = view().peek(keylet::account(ownerID));
    if (!owner)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    adjustOwnerCount(view(), owner, -1, j_);

    // Destroy the vault.
    view().erase(vault);

    return tesSUCCESS;
}

}  // namespace ripple
