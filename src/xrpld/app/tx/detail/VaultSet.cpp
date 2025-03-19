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

#include <xrpld/app/tx/detail/VaultSet.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !ctx.rules.enabled(featurePermissionedDomains))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const data = ctx.tx[~sfData])
    {
        if (data->empty() || data->length() > maxDataPayloadLength)
            return temMALFORMED;
    }

    auto const domain = ctx.tx[~sfDomainID];
    if (domain && *domain == beast::zero)
        return temMALFORMED;

    if (!ctx.tx.isFieldPresent(sfDomainID) &&
        !ctx.tx.isFieldPresent(sfAssetMaximum) &&
        !ctx.tx.isFieldPresent(sfData))
        return temMALFORMED;

    return preflight2(ctx);
}

TER
VaultSet::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    // Assert that submitter is the Owner.
    if (ctx.tx[sfAccount] != vault->at(sfOwner))
        return tecNO_PERMISSION;

    auto const mptIssuanceID = (*vault)[sfMPTokenIssuanceID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;

    auto const domain = ctx.tx[~sfDomainID];
    auto const oldDomain = sleIssuance->at(~sfDomainID);
    auto const data = ctx.tx[~sfData];
    auto const oldData = vault->at(~sfData);
    auto const assetMax = ctx.tx[~sfAssetMaximum];
    auto const oldAssetMax = vault->at(sfAssetMaximum);
    int const changes =                      //
        (domain && (domain != oldDomain)) +  //
        (data && (data != oldData)) +        //
        (assetMax && (*assetMax != oldAssetMax));

    // This transaction would change nothing
    if (!changes)
        return tecNO_PERMISSION;

    if (domain)
    {
        // We can only set domain if private flag was originally set
        if ((vault->getFlags() & tfVaultPrivate) == 0)
            return tecINVALID_DOMAIN;

        auto const sleDomain =
            ctx.view.read(keylet::permissionedDomain(*domain));
        if (!sleDomain)
            return tecNO_ENTRY;

        // Sanity check only, this should be enforced by VaultCreate
        if ((sleIssuance->getFlags() & lsfMPTRequireAuth) == 0)
            return tefINTERNAL;
    }

    return tesSUCCESS;
}

TER
VaultSet::doApply()
{
    // All return codes in `doApply` must be `tec`, `ter`, or `tes`.
    // As we move checks into `preflight` and `preclaim`,
    // we can consider downgrading them to `tef` or `tem`.

    auto const& tx = ctx_.tx;

    // Update existing object.
    auto vault = view().peek({ltVAULT, tx[sfVaultID]});
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    auto const mptIssuanceID = (*vault)[sfMPTokenIssuanceID];
    auto const sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
        return tefINTERNAL;

    // Update mutable flags and fields if given.
    if (tx.isFieldPresent(sfData))
        vault->at(sfData) = tx[sfData];
    if (tx.isFieldPresent(sfAssetMaximum))
    {
        if (tx[sfAssetMaximum] < *vault->at(sfAssetTotal))
            return tecLIMIT_EXCEEDED;
        vault->at(sfAssetMaximum) = tx[sfAssetMaximum];
    }
    if (tx.isFieldPresent(sfDomainID))
    {
        // In VaultSet::preclaim we enforce that tfVaultPrivate must have been
        // set in the vault. We currently do not support making such a vault
        // public (i.e. removal of tfVaultPrivate flag). The sfDomainID flag
        // must be set in the MPTokenIssuance object and can be freely updated.
        sleIssuance->setFieldH256(sfDomainID, tx.getFieldH256(sfDomainID));
        view().update(sleIssuance);
    }

    view().update(vault);

    return tesSUCCESS;
}

}  // namespace ripple
