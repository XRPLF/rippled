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

    if (ctx.tx[sfVaultID] == beast::zero)
    {
        JLOG(ctx.j.debug()) << "VaultSet: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const data = ctx.tx[~sfData])
    {
        if (data->empty() || data->length() > maxDataPayloadLength)
        {
            JLOG(ctx.j.debug()) << "VaultSet: invalid data payload size.";
            return temMALFORMED;
        }
    }

    if (auto const assetMax = ctx.tx[~sfAssetsMaximum])
    {
        if (*assetMax < beast::zero)
        {
            JLOG(ctx.j.debug()) << "VaultSet: invalid max assets.";
            return temMALFORMED;
        }
    }

    if (!ctx.tx.isFieldPresent(sfDomainID) &&
        !ctx.tx.isFieldPresent(sfAssetsMaximum) &&
        !ctx.tx.isFieldPresent(sfData))
    {
        JLOG(ctx.j.debug()) << "VaultSet: nothing is being updated.";
        return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
VaultSet::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    // Assert that submitter is the Owner.
    if (ctx.tx[sfAccount] != vault->at(sfOwner))
    {
        JLOG(ctx.j.debug()) << "VaultSet: account is not an owner.";
        return tecNO_PERMISSION;
    }

    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultSet: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        // We can only set domain if private flag was originally set
        if ((vault->getFlags() & tfVaultPrivate) == 0)
        {
            JLOG(ctx.j.debug()) << "VaultSet: vault is not private";
            return tecNO_PERMISSION;
        }

        if (*domain != beast::zero)
        {
            auto const sleDomain =
                ctx.view.read(keylet::permissionedDomain(*domain));
            if (!sleDomain)
                return tecOBJECT_NOT_FOUND;
        }

        // Sanity check only, this should be enforced by VaultCreate
        if ((sleIssuance->getFlags() & lsfMPTRequireAuth) == 0)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error())
                << "VaultSet: issuance of vault shares is not private.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }
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
    auto vault = view().peek(keylet::vault(tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultSet: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Update mutable flags and fields if given.
    if (tx.isFieldPresent(sfData))
        vault->at(sfData) = tx[sfData];
    if (tx.isFieldPresent(sfAssetsMaximum))
    {
        if (tx[sfAssetsMaximum] != 0 &&
            tx[sfAssetsMaximum] < *vault->at(sfAssetsTotal))
            return tecLIMIT_EXCEEDED;
        vault->at(sfAssetsMaximum) = tx[sfAssetsMaximum];
    }

    if (auto const domainId = tx[~sfDomainID]; domainId)
    {
        if (*domainId != beast::zero)
        {
            // In VaultSet::preclaim we enforce that tfVaultPrivate must have
            // been set in the vault. We currently do not support making such a
            // vault public (i.e. removal of tfVaultPrivate flag). The
            // sfDomainID flag must be set in the MPTokenIssuance object and can
            // be freely updated.
            sleIssuance->setFieldH256(sfDomainID, *domainId);
        }
        else if (sleIssuance->isFieldPresent(sfDomainID))
        {
            sleIssuance->makeFieldAbsent(sfDomainID);
        }
        view().update(sleIssuance);
    }

    view().update(vault);

    return tesSUCCESS;
}

}  // namespace ripple
