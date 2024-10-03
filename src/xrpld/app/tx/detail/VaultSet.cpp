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
#include <xrpl/protocol/st.h>

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

    auto const& vaultId = tx[~sfVaultID];
    auto keylet = vaultId ? Keylet{ltVAULT, *vaultId}
                          : keylet::vault(tx[sfAccount], tx[sfSequence]);

    auto vault = view().peek(keylet);

    if (vault)
    {
        // Update existing object.
        // Assert that submitter is the Owner.
        if (tx[sfAccount] != (*vault)[sfOwner])
            return tecNO_PERMISSION;
        // Assert that Asset is the same if given.
        if (tx.isFieldPresent(sfAsset) && tx[sfAsset] != (*vault)[sfAsset])
            return tecNO_PERMISSION;
        view().update(vault);
    }
    else if (tx.isFieldPresent(sfVaultID))
    {
        // Update missing object.
        return tecOBJECT_NOT_FOUND;
    }
    else
    {
        // Create new object.
        if (!tx.isFieldPresent(sfAsset))
            return tecINCOMPLETE;

        vault = std::make_shared<SLE>(keylet);
        if (auto ter = dirLink(view(), tx[sfAccount], vault); ter)
            return ter;
        auto pseudo = createPseudoAccount(view(), vault->key());
        if (!pseudo)
            return pseudo.error();
        (*vault)[sfSequence] = tx[sfSequence];
        (*vault)[sfOwner] = tx[sfAccount];
        (*vault)[sfAccount] = pseudo.value();
        (*vault)[~sfData] = tx[~sfData];
        (*vault)[sfAsset] = tx[sfAsset];
        (*vault)[sfAssetTotal] = 0;
        (*vault)[sfAssetAvailable] = 0;
        (*vault)[~sfAssetMaximum] = tx[~sfAssetMaximum];
        // TODO: create MPT for share
        // No `LossUnrealized`.
        view().insert(vault);
    }

    // Set or clear field AssetMaximum.
    if (tx.isFieldPresent(sfAssetMaximum))
    {
        if (tx[sfAssetMaximum] == 0)
        {
            vault->delField(sfAssetMaximum);
        }
        else
        {
            (*vault)[sfAssetMaximum] = tx[sfAssetMaximum];
        }
    }

    return tesSUCCESS;
}

}  // namespace ripple
