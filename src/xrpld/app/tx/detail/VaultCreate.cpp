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

#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/app/tx/detail/MPTokenIssuanceCreate.h>
#include <xrpld/app/tx/detail/VaultCreate.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/Asset.h>
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
    if (!ctx.rules.enabled(featureSingleAssetVault) ||
        !ctx.rules.enabled(featureMPTokensV1))
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

    if (auto const withdrawalPolicy = ctx.tx[~sfWithdrawalPolicy])
    {
        // Enforce valid withdrawal policy
        if (*withdrawalPolicy != vaultStrategyFirstComeFirstServe)
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::zero)
            return temMALFORMED;
        else if ((ctx.tx.getFlags() & tfVaultPrivate) == 0)
            return temMALFORMED;  // DomainID only allowed on private vaults
    }

    if (auto const assetMax = ctx.tx[~sfAssetsMaximum])
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
    auto vaultAsset = ctx.tx[sfAsset];
    auto account = ctx.tx[sfAccount];

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
            // NOTE: flag lsfMPTCanTransfer is immutable, so this is debug in
            // VaultCreate only; in other vault function it's an error.
            JLOG(ctx.j.debug())
                << "VaultCreate: vault assets are non-transferable.";
            return tecNO_AUTH;
        }
    }
    else if (vaultAsset.holds<Issue>())
    {
        auto const issuer =
            ctx.view.read(keylet::account(vaultAsset.getIssuer()));
        if (!issuer)
            return terNO_ACCOUNT;
        else if (!issuer->isFlag(lsfDefaultRipple))
            return terNO_RIPPLE;
    }

    // Check for pseudo-account issuers - we do not want a vault to hold such
    // assets (e.g. MPT shares to other vaults or AMM LPTokens) as they would be
    // impossible to clawback (should the need arise)
    if (!vaultAsset.native())
    {
        if (isPseudoAccount(ctx.view, vaultAsset.getIssuer()))
            return tecWRONG_ASSET;
    }

    // Cannot create Vault for an Asset frozen for the vault owner
    if (isFrozen(ctx.view, account, vaultAsset))
        return vaultAsset.holds<Issue>() ? tecFROZEN : tecLOCKED;

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        auto const sleDomain =
            ctx.view.read(keylet::permissionedDomain(*domain));
        if (!sleDomain)
            return tecOBJECT_NOT_FOUND;
    }

    auto sequence = ctx.tx.getSeqValue();
    if (auto const accountId = pseudoAccountAddress(
            ctx.view, keylet::vault(account, sequence).key);
        accountId == beast::zero)
        return terADDRESS_COLLISION;

    return tesSUCCESS;
}

TER
VaultCreate::doApply()
{
    // All return codes in `doApply` must be `tec`, `ter`, or `tes`.
    // As we move checks into `preflight` and `preclaim`,
    // we can consider downgrading them to `tef` or `tem`.

    auto const& tx = ctx_.tx;
    auto sequence = tx.getSeqValue();
    auto owner = view().peek(keylet::account(account_));
    if (owner == nullptr)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto vault = std::make_shared<SLE>(keylet::vault(account_, sequence));

    if (auto ter = dirLink(view(), account_, vault))
        return ter;
    adjustOwnerCount(view(), owner, 1, j_);
    auto ownerCount = owner->at(sfOwnerCount);
    if (mPriorBalance < view().fees().accountReserve(ownerCount))
        return tecINSUFFICIENT_RESERVE;

    auto maybePseudo = createPseudoAccount(view(), vault->key(), sfVaultID);
    if (!maybePseudo)
        return maybePseudo.error();  // LCOV_EXCL_LINE
    auto& pseudo = *maybePseudo;
    auto pseudoId = pseudo->at(sfAccount);
    auto asset = tx[sfAsset];

    if (auto ter = addEmptyHolding(view(), pseudoId, mPriorBalance, asset, j_);
        !isTesSuccess(ter))
        return ter;

    auto txFlags = tx.getFlags();
    std::uint32_t mptFlags = 0;
    if ((txFlags & tfVaultShareNonTransferable) == 0)
        mptFlags |= (lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer);
    if (txFlags & tfVaultPrivate)
        mptFlags |= lsfMPTRequireAuth;

    // Note, here we are **not** creating an MPToken for the assets held in
    // the vault. That MPToken or TrustLine/RippleState is created above, in
    // addEmptyHolding. Here we are creating MPTokenIssuance for the shares
    // in the vault
    auto maybeShare = MPTokenIssuanceCreate::create(
        view(),
        j_,
        {
            .priorBalance = std::nullopt,
            .account = pseudoId->value(),
            .sequence = 1,
            .flags = mptFlags,
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
        });
    if (!maybeShare)
        return maybeShare.error();  // LCOV_EXCL_LINE
    auto& share = *maybeShare;

    vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    vault->at(sfFlags) = txFlags & tfVaultPrivate;
    vault->at(sfSequence) = sequence;
    vault->at(sfOwner) = account_;
    vault->at(sfAccount) = pseudoId;
    vault->at(sfAssetsTotal) = Number(0);
    vault->at(sfAssetsAvailable) = Number(0);
    vault->at(sfLossUnrealized) = Number(0);
    // Leave default values for AssetTotal and AssetAvailable, both zero.
    if (auto value = tx[~sfAssetsMaximum])
        vault->at(sfAssetsMaximum) = *value;
    vault->at(sfShareMPTID) = share;
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
