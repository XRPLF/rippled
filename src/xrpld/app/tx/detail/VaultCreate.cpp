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

#include <xrpld/app/tx/detail/MPTokenIssuanceCreate.h>
#include <xrpld/app/tx/detail/VaultCreate.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !ctx.rules.enabled(featurePermissionedDomains))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;

    if (ctx.tx.getFlags() & tfVaultCreateMask)
        return temINVALID_FLAG;

    if (auto const data = ctx.tx[~sfData])
    {
        if (data->empty() || data->length() > maxDataPayloadLength)
            return temMALFORMED;
    }

    if (auto const data = ctx.tx[~sfWithdrawalPolicy])
    {
        // Enforce valid withdrawal policy
        if (*data != vaultStrategyFirstComeFirstServe)
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::zero)
            return temMALFORMED;
        else if ((ctx.tx.getFlags() & tfVaultPrivate) == 0)
            return temMALFORMED;  // DomainID only allowed on private vaults
    }

    if (auto const assetMax = ctx.tx[~sfAssetMaximum])
    {
        if (*assetMax < beast::zero)
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->length() == 0 ||
            metadata->length() > maxMPTokenMetadataLength)
            return temMALFORMED;
    }

    return preflight2(ctx);
}

XRPAmount
VaultCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // One reserve increment is typically much greater than one base fee.
    return view.fees().increment;
}

TER
VaultCreate::preclaim(PreclaimContext const& ctx)
{
    auto asset = ctx.tx[sfAsset];
    auto account = ctx.tx[sfAccount];

    if (asset.holds<MPTIssue>())
    {
        auto mptID = asset.get<MPTIssue>().getMptID();
        auto issuance = ctx.view.read(keylet::mptIssuance(mptID));
        if (!issuance)
            return tecNO_ENTRY;
        if ((issuance->getFlags() & lsfMPTCanTransfer) == 0)
            return tecNO_AUTH;
    }

    // Cannot create Vault for an Asset frozen for the vault owner
    if (isFrozen(ctx.view, account, asset))
        return tecFROZEN;

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        auto const sleDomain =
            ctx.view.read(keylet::permissionedDomain(*domain));
        if (!sleDomain)
            return tecNO_ENTRY;
    }

    return tesSUCCESS;
}

TER
VaultCreate::doApply()
{
    // All return codes in `doApply` must be `tec`, `ter`, or `tes`.
    // As we move checks into `preflight` and `preclaim`,
    // we can consider downgrading them to `tef` or `tem`.

    auto const& tx = ctx_.tx;
    auto const& ownerId = account_;
    auto sequence = tx.getSeqValue();

    auto owner = view().peek(keylet::account(ownerId));
    auto vault = std::make_shared<SLE>(keylet::vault(ownerId, sequence));

    if (auto ter = dirLink(view(), ownerId, vault))
        return ter;
    // Should the next 3 lines be folded into `dirLink`?
    adjustOwnerCount(view(), owner, 1, j_);
    auto ownerCount = owner->at(sfOwnerCount);
    if (mPriorBalance < view().fees().accountReserve(ownerCount))
        return tecINSUFFICIENT_RESERVE;

    auto maybePseudo = createPseudoAccount(
        view(), vault->key(), PseudoAccountOwnerType::Vault);
    if (!maybePseudo)
        return maybePseudo.error();
    auto& pseudo = *maybePseudo;
    auto pseudoId = pseudo->at(sfAccount);

    if (auto ter =
            addEmptyHolding(view(), pseudoId, mPriorBalance, tx[sfAsset], j_))
        return ter;

    auto txFlags = tx.getFlags();
    std::uint32_t mptFlags = 0;
    if ((txFlags & tfVaultShareNonTransferable) == 0)
        mptFlags |= (lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer);
    if (txFlags & tfVaultPrivate)
        mptFlags |= lsfMPTRequireAuth;

    // Note, here we are **not** creating an MPToken for the assets held in the
    // vault. That MPToken or TrustLine/RippleState is created above, in
    // addEmptyHolding. Here we are creating MPTokenIssuance for the shares in
    // the vault
    auto maybeShare = MPTokenIssuanceCreate::create(
        view(),
        j_,
        {
            // The operator-> gives the underlying STAccount,
            // whose value function returns a const&.
            .account = pseudoId->value(),
            .sequence = 1,
            .flags = mptFlags,
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
        });
    if (!maybeShare)
        return maybeShare.error();
    auto& share = *maybeShare;

    vault->at(sfFlags) = txFlags & tfVaultPrivate;
    vault->at(sfSequence) = sequence;
    vault->at(sfOwner) = ownerId;
    vault->at(sfAccount) = pseudoId;
    vault->at(sfAsset) = tx[sfAsset];
    // Leave default values for AssetTotal and AssetAvailable, both zero.
    if (auto value = tx[~sfAssetMaximum])
        vault->at(sfAssetMaximum) = *value;
    vault->at(sfMPTokenIssuanceID) = share;
    if (auto value = tx[~sfData])
        vault->at(sfData) = *value;
    // Required field, default to vaultStrategyFirstComeFirstServe
    if (auto value = tx[~sfWithdrawalPolicy])
        vault->at(sfWithdrawalPolicy) = *value;
    else
        vault->at(sfWithdrawalPolicy) = vaultStrategyFirstComeFirstServe;
    // No `LossUnrealized`.
    view().insert(vault);

    return tesSUCCESS;
}

}  // namespace ripple
