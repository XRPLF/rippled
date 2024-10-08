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
#include "MPTokenIssuanceCreate.h"

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

XRPAmount
AMMCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // TODO: What should the base fee be? One increment does not seem enough.
    return view.fees().increment * 4;
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
    auto sequence = tx.getSequence();

    if (auto const& vaultId = tx[~sfVaultID]; vaultId)
    {
        // Update existing object.
        auto keylet = Keylet{ltVAULT, *vaultId};
        auto vault = view().peek(keylet);
        if (!vault)
            return tecOBJECT_NOT_FOUND;

        // Assert that submitter is the Owner.
        if (owner != vault->at(sfOwner))
            return tecNO_PERMISSION;

        // Assert immutable flags not given.
        auto txFlags = tx[sfFlags];
        if ((txFlags & tfVaultPrivate) ||
            (txFlags & tfVaultShareNonTransferable))
            return tecIMMUTABLE;
        // Assert identical immutable fields if given.
        if (tx.isFieldPresent(sfAsset) && tx[sfAsset] != vault->at(sfAsset))
            return tecIMMUTABLE;

        // TODO: sfMPTokenMetadata?

        // Update mutable flags and fields if given.
        if (tx.isFieldPresent(sfData))
            vault->at(~sfData) = tx[~sfData];
        if (tx.isFieldPresent(sfAssetMaximum))
            vault->at(~sfAssetMaximum) = tx[sfAssetMaximum];

        view().update(vault);
    }
    else
    {
        // Create new object.
        if (!tx.isFieldPresent(sfAsset))
            return tecINCOMPLETE;

        auto keylet = keylet::vault(owner, sequence);
        auto vault = std::make_shared<SLE>(keylet);
        if (auto ter = dirLink(view(), owner, vault); !isTesSuccess(ter))
            return ter;
        auto maybe = createPseudoAccount(view(), vault->key());
        if (!maybe)
            return maybe.error();
        auto& pseudo = *maybe;
        auto pseudoId = pseudo->at(sfAccount);
        auto txFlags = tx[sfFlags];
        std::uint32_t mptFlags = 0;
        if (!(txFlags & tfVaultShareNonTransferable))
            mptFlags |= (lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer);
        if (txFlags & tfVaultPrivate)
            mptFlags |= lsfMPTRequireAuth;
        auto maybe2 = MPTokenIssuanceCreate::create(
            view(),
            j_,
            {
                .account = pseudoId,
                .sequence = 1,
                .flags = mptFlags,
                .metadata = tx[~sfMPTokenMetadata],
            });
        if (!maybe2)
            return maybe2.error();
        auto& mptId = *maybe2;
        vault->at(sfFlags) = txFlags & tfVaultPrivate;
        vault->at(sfSequence) = sequence;
        vault->at(sfOwner) = owner;
        vault->at(sfAccount) = pseudoId;
        vault->at(~sfData) = tx[~sfData];
        vault->at(sfAsset) = tx[sfAsset];
        // vault->at(sfAssetTotal) = 0;
        // vault->at(sfAssetAvailable) = 0;
        vault->at(~sfAssetMaximum) = tx[~sfAssetMaximum];
        vault->at(sfMPTokenIssuanceID) = mptId;
        // No `LossUnrealized`.
        view().insert(vault);
    }

    return tesSUCCESS;
}

}  // namespace ripple
