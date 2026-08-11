#include <xrpl/tx/transactors/vault/VaultCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/token/MPTokenIssuanceCreate.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

bool
VaultCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return false;

    if (ctx.tx.isFieldPresent(sfDomainID) && !ctx.rules.enabled(featurePermissionedDomains))
        return false;

    return true;
}

std::uint32_t
VaultCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultCreateMask;
}

NotTEC
VaultCreate::preflight(PreflightContext const& ctx)
{
    if (!validDataLength(ctx.tx[~sfData], kMaxDataPayloadLength))
        return temMALFORMED;

    if (auto const withdrawalPolicy = ctx.tx[~sfWithdrawalPolicy])
    {
        // Enforce valid withdrawal policy
        if (*withdrawalPolicy != kVaultStrategyFirstComeFirstServe)
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::kZero)
        {
            return temMALFORMED;
        }
        if (!ctx.tx.isFlag(tfVaultPrivate))
        {
            return temMALFORMED;  // DomainID only allowed on private vaults
        }
    }

    if (auto const assetMax = ctx.tx[~sfAssetsMaximum])
    {
        if (*assetMax < beast::kZero)
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty() || metadata->length() > kMaxMpTokenMetadataLength)
            return temMALFORMED;
    }

    if (auto const scale = ctx.tx[~sfScale])
    {
        auto const vaultAsset = ctx.tx[sfAsset];
        if (vaultAsset.holds<MPTIssue>() || vaultAsset.native())
            return temMALFORMED;

        if (scale > kVaultMaximumIouScale)
            return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
VaultCreate::preclaim(PreclaimContext const& ctx)
{
    auto const vaultAsset = ctx.tx[sfAsset];
    auto const account = ctx.tx[sfAccount];

    if (auto const ter = canAddHolding(ctx.view, vaultAsset))
        return ter;

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
        auto const sleDomain = ctx.view.read(keylet::permissionedDomain(*domain));
        if (!sleDomain)
            return tecOBJECT_NOT_FOUND;
    }

    auto const sequence = ctx.tx.getSeqProxy();
    if (auto const accountId = pseudoAccountAddress(ctx.view, keylet::vault(account, sequence).key);
        accountId == beast::kZero)
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
    auto applyViewContext = ctx_.getApplyViewContext();
    auto const sequence = tx.getSeqProxy();
    auto const owner = view().peek(keylet::account(accountID_));
    if (owner == nullptr)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto vault = std::make_shared<SLE>(keylet::vault(accountID_, sequence));

    if (auto ter = dirLink(view(), accountID_, vault))
        return ter;
    // We will create Vault and PseudoAccount, hence increase OwnerCount by 2
    increaseOwnerCount(view(), owner, {}, 2, j_);
    if (preFeeBalance_ < accountReserve(view(), owner, j_))
        return tecINSUFFICIENT_RESERVE;

    auto maybePseudo = createPseudoAccount(view(), vault->key(), sfVaultID);
    if (!maybePseudo)
        return maybePseudo.error();  // LCOV_EXCL_LINE
    auto const& pseudo = *maybePseudo;
    AccountID const pseudoId = pseudo->at(sfAccount);
    auto const asset = tx[sfAsset];

    if (auto ter = addEmptyHolding(applyViewContext, pseudoId, preFeeBalance_, asset, j_);
        !isTesSuccess(ter))
        return ter;

    std::uint8_t const scale = (asset.holds<MPTIssue>() || asset.native())
        ? 0
        : ctx_.tx[~sfScale].value_or(kVaultDefaultIouScale);

    std::uint32_t mptFlags = 0;
    if (!tx.isFlag(tfVaultShareNonTransferable))
        mptFlags |= (lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer);
    if (tx.isFlag(tfVaultPrivate))
        mptFlags |= lsfMPTRequireAuth;

    // Note, here we are **not** creating an MPToken for the assets held in
    // the vault. That MPToken or TrustLine/RippleState is created above, in
    // addEmptyHolding. Here we are creating MPTokenIssuance for the shares
    // in the vault.
    //
    // Post-fixCleanup3_2_0: surface the vault pseudo's holding (MPToken
    // for MPT, RippleState for IOU) on the share via sfReferenceHolding.
    // XRP underlyings leave it unset.
    auto const referenceHolding = [&]() -> std::optional<uint256> {
        if (!view().rules().enabled(fixCleanup3_2_0) || asset.native())
            return std::nullopt;
        return asset.holds<MPTIssue>()
            ? keylet::mptoken(asset.get<MPTIssue>().getMptID(), pseudoId).key
            : keylet::trustLine(pseudoId, asset.get<Issue>()).key;
    }();
    auto const maybeShare = MPTokenIssuanceCreate::create(
        applyViewContext,
        j_,
        {
            .priorBalance = std::nullopt,
            .account = pseudoId,
            .sequence = 1,
            .flags = mptFlags,
            .assetScale = scale,
            .transferFee = std::nullopt,
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
            .referenceHolding = referenceHolding,
        });
    if (!maybeShare)
        return maybeShare.error();  // LCOV_EXCL_LINE
    auto const& mptIssuanceID = *maybeShare;

    vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    vault->at(sfFlags) = tx.getFlags() & tfVaultPrivate;
    vault->at(sfSequence) = sequence.value();
    vault->at(sfOwner) = accountID_;
    vault->at(sfAccount) = pseudoId;
    vault->at(sfAssetsTotal) = Number(0);
    vault->at(sfAssetsAvailable) = Number(0);
    vault->at(sfLossUnrealized) = Number(0);
    // Leave default values for AssetTotal and AssetAvailable, both zero.
    if (auto value = tx[~sfAssetsMaximum])
        vault->at(sfAssetsMaximum) = *value;
    vault->at(sfShareMPTID) = mptIssuanceID;
    if (auto value = tx[~sfData])
        vault->at(sfData) = *value;
    // Required field, default to vaultStrategyFirstComeFirstServe
    if (auto value = tx[~sfWithdrawalPolicy])
    {
        vault->at(sfWithdrawalPolicy) = *value;
    }
    else
    {
        vault->at(sfWithdrawalPolicy) = kVaultStrategyFirstComeFirstServe;
    }
    if (scale != 0u)
        vault->at(sfScale) = scale;
    if (view().rules().enabled(featureLendingProtocolV1_1))
        vault->at(sfLEVersion) = std::to_underlying(VaultVersion::CashBasis);
    view().insert(vault);

    // Explicitly create MPToken for the vault owner
    if (auto const err = authorizeMPToken(
            applyViewContext, preFeeBalance_, mptIssuanceID, accountID_, ctx_.journal);
        !isTesSuccess(err))
        return err;

    // If the vault is private, set the authorized flag for the vault owner
    if (tx.isFlag(tfVaultPrivate))
    {
        if (auto const err = authorizeMPToken(
                applyViewContext,
                preFeeBalance_,
                mptIssuanceID,
                pseudoId,
                ctx_.journal,
                {},
                accountID_);
            !isTesSuccess(err))
            return err;
    }

    associateAsset(*vault, asset);

    return tesSUCCESS;
}

void
VaultCreate::visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    invariantData_.visitEntry(isDelete, before, after);
}

bool
VaultCreate::finalizeInvariants(
    STTx const& tx,
    TER result,
    XRPAmount,
    ReadView const& view,
    beast::Journal const& j)
{
    if (!isTesSuccess(result))
        return true;

    bool const enforce = view.rules().enabled(featureSingleAssetVault);

    auto const& afterVaults = invariantData_.afterVault();
    auto const& beforeVaults = invariantData_.beforeVault();

    if (afterVaults.empty())
    {
        JLOG(j.fatal()) << "Invariant failed: VaultCreate must create a vault";
        XRPL_ASSERT(enforce, "xrpl::VaultCreate::finalizeInvariants : vault created");
        return !enforce;
    }

    auto const& afterVault = afterVaults[0];

    bool ok = true;

    if (!beforeVaults.empty())
    {
        JLOG(j.fatal()) <<  //
            "Invariant failed: create operation must not have updated a vault";
        ok = false;
    }

    auto const updatedShares = [&]() -> std::optional<VaultInvariantData::Shares> {
        if (auto found = invariantData_.resolveUpdatedShares(afterVault))
            return found;
        auto const sleShares = view.read(keylet::mptokenIssuance(afterVault.shareMPTID));
        if (!sleShares)
            return std::nullopt;
        return VaultInvariantData::Shares::make(*sleShares);
    }();

    if (!updatedShares)
    {
        JLOG(j.fatal()) << "Invariant failed: VaultCreate must create share issuance";
        XRPL_ASSERT(enforce, "xrpl::VaultCreate::finalizeInvariants : shares created");
        return !enforce;
    }

    static constexpr Number kZero{};
    if (afterVault.assetsAvailable != kZero || afterVault.assetsTotal != kZero ||
        afterVault.lossUnrealized != kZero || updatedShares->sharesTotal != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: created vault must be empty";
        ok = false;
    }

    if (afterVault.pseudoId != updatedShares->share.getIssuer())
    {
        JLOG(j.fatal())  //
            << "Invariant failed: shares issuer and vault "
               "pseudo-account must be the same";
        ok = false;
    }

    auto const sleSharesIssuer = view.read(keylet::account(updatedShares->share.getIssuer()));
    if (!sleSharesIssuer)
    {
        JLOG(j.fatal()) << "Invariant failed: shares issuer must exist";
        XRPL_ASSERT(enforce, "xrpl::VaultCreate::finalizeInvariants : shares issuer exists");
        return !enforce;
    }

    if (!isPseudoAccount(sleSharesIssuer))
    {
        JLOG(j.fatal())  //
            << "Invariant failed: shares issuer must be a "
               "pseudo-account";
        ok = false;
    }

    if (auto const vaultId = (*sleSharesIssuer)[~sfVaultID]; !vaultId || *vaultId != afterVault.key)
    {
        JLOG(j.fatal())  //
            << "Invariant failed: shares issuer pseudo-account "
               "must point back to the vault";
        ok = false;
    }

    if (!ok)
    {
        XRPL_ASSERT(enforce, "xrpl::VaultCreate::finalizeInvariants : vault invariants");
        return !enforce;
    }
    return true;
}

}  // namespace xrpl
