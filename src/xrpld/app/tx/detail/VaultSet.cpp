//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;
    if (auto const data = ctx.tx[~sfData])
    {
        if (data->length() > maxVaultDataLength)
            return temSTRING_TOO_LARGE;
    }

    auto const domain = ctx.tx[~sfDomainID];
    if (domain && *domain == beast::zero)
        return temMALFORMED;

    return preflight2(ctx);
}

TER
VaultSet::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfVaultID];
    auto const sle = ctx.view.read(keylet::vault(id));
    if (!sle)
        return tecOBJECT_NOT_FOUND;

    // Assert that submitter is the Owner.
    if (ctx.tx[sfAccount] != sle->at(sfOwner))
        return tecNO_PERMISSION;

    // We can only set domain if private flag was originally set and
    // domain was not set
    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if ((sle->getFlags() & tfVaultPrivate) == 0)
            return tecREMOVING_PERMISSIONS;
        if (auto const oldDomain = sle->at(~sfDomainID))
        {
            if (*oldDomain != *domain)
                return tecREMOVING_PERMISSIONS;
            // else no change
        }
        // else domain wasn't set previously, we allow setting it now
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
        // In VaultSet::preclaim we enforce that either DomainID wasn't present
        // in the vault, or was the same value as the one supplied. We also
        // enforce that tfVaultPrivate must have been set in the vault. By
        // adding DomainID to an existing private vault, we are allowing
        // permissioned users to interract with a vault which was previously
        // accessible to its owner only. We currently do not support making
        // such a vault public (i.e. removal of tfVaultPrivate flag)
        vault->setFieldH256(sfDomainID, tx.getFieldH256(sfDomainID));
    }

    view().update(vault);

    return tesSUCCESS;
}

}  // namespace ripple
