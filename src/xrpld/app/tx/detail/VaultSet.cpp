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
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultSet::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

TER
VaultSet::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultSet::doApply()
{
    // All return codes in `doApply` must be `tec`, `ter`, or `tes`.
    // As we move checks into `preflight` and `preclaim`,
    // we can consider downgrading them to `tef` or `tem`.

    auto const& tx = ctx_.tx;
    auto const& owner = account_;

    // Update existing object.
    auto vault = view().peek({ltVAULT, tx[sfVaultID]});
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    // Assert that submitter is the Owner.
    if (owner != vault->at(sfOwner))
        return tecNO_PERMISSION;

    // Assert immutable flags not given.
    auto txFlags = tx.getFlags();
    if ((txFlags & tfVaultPrivate) || (txFlags & tfVaultShareNonTransferable))
        return tecIMMUTABLE;

    // Update mutable flags and fields if given.
    if (tx.isFieldPresent(sfData))
        vault->at(sfData) = tx[sfData];
    if (tx.isFieldPresent(sfAssetMaximum))
        vault->at(~sfAssetMaximum) = tx[sfAssetMaximum];

    view().update(vault);

    return tesSUCCESS;
}

}  // namespace ripple
