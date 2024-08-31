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

#define ASSERT(condition) condition
#undef ASSERT
#define ASSERT(condition)
#define MATCH(...)
#define OVERWRITE(lhs, rhs) \
    if (rhs)                \
    {                       \
        lhs = rhs;          \
    }
#undef OVERWRITE
#define OVERWRITE(lhs, rhs)

TER
VaultSet::doApply()
{
    auto const& tx = ctx_.tx;

    auto const& vaultId = tx[~sfVaultID];
    auto klVault = vaultId ? Keylet{ltVAULT, *vaultId}
                           : keylet::vault(tx[sfAccount], tx[sfSequence]);

    auto vault = view().peek(klVault);

    if (vault)
    {
        ASSERT(vault->getType() == ltVAULT);
        ASSERT(vault->key() == klVault.key);
        ASSERT((*vault)[sfOwner] == tx[sfAccount]);
        MATCH((*vault)[sfAsset], tx[~sfAsset]);
        view().update(vault);
    }
    else if (tx.isFieldPresent(sfVaultID))
    {
        return tecOBJECT_NOT_FOUND;
    }
    else
    {
        // create new vault
    }

    OVERWRITE((*vault)[sfAssetMaximum], tx[~sfAssetMaximum]);

    return tesSUCCESS;
}

}  // namespace ripple
