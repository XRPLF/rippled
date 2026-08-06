#include <xrpl/tx/transactors/vault/VaultCreate.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
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

    auto const sequence = ctx.tx.getSeqValue();
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
    auto const sequence = tx.getSeqValue();
    auto const owner = view().peek(keylet::account(accountID_));
    if (owner == nullptr)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto vault = std::make_shared<SLE>(keylet::vault(accountID_, sequence));
    auto const asset = tx[sfAsset];

    // Solution A (docs/plan-vault-dust-a-second-account.md §4): give the
    // Vault a second pseudo-account to hold un-representable repayment
    // remainder ("dust"), but only for IOU Vaults created after the
    // amendment activates. sfLEVersion is set for every asset type, but
    // XRP/MPT amounts are integral so those Vaults can never produce dust
    // (common §2.4) and must not pay for an account they cannot use. A
    // Vault that already exists at activation gets nothing: there is no
    // migration, and the new-Vaults-only scope decision (common §2.5) is
    // implemented by tying dust-account creation to the very same
    // condition that sets sfLEVersion below, so the two can never diverge.
    bool const createsDustAccount = view().rules().enabled(featureLendingProtocolV1_1) &&
        !asset.holds<MPTIssue>() && !asset.native();

    if (auto ter = dirLink(view(), accountID_, vault))
        return ter;
    // We will create Vault and PseudoAccount, hence increase OwnerCount by
    // 2, or by 3 if this Vault also gets a dust pseudo-account.
    increaseOwnerCount(view(), owner, {}, createsDustAccount ? 3 : 2, j_);
    if (preFeeBalance_ < accountReserve(view(), owner, j_))
        return tecINSUFFICIENT_RESERVE;

    auto maybePseudo = createPseudoAccount(view(), vault->key(), sfVaultID);
    if (!maybePseudo)
        return maybePseudo.error();  // LCOV_EXCL_LINE
    auto const& pseudo = *maybePseudo;
    AccountID const pseudoId = pseudo->at(sfAccount);

    if (auto ter = addEmptyHolding(applyViewContext, pseudoId, preFeeBalance_, asset, j_);
        !isTesSuccess(ter))
        return ter;

    std::optional<AccountID> dustId;
    if (createsDustAccount)
    {
        // Same pseudoOwnerKey as the main account, so the address-retry
        // loop lands on the next free address; both accounts are created
        // inside this one atomic transaction, so there is no window
        // between the seed becoming known and either address being
        // claimed.
        auto maybeDust = createPseudoAccount(view(), vault->key(), sfVaultDustID);
        if (!maybeDust)
            return maybeDust.error();
        dustId = (*maybeDust)->at(sfAccount);

        if (auto ter = addEmptyHolding(applyViewContext, *dustId, preFeeBalance_, asset, j_);
            !isTesSuccess(ter))
            return ter;
    }

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
            .mutableFlags = std::nullopt,
            .referenceHolding = referenceHolding,
        });
    if (!maybeShare)
        return maybeShare.error();  // LCOV_EXCL_LINE
    auto const& mptIssuanceID = *maybeShare;

    vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    vault->at(sfFlags) = tx.getFlags() & tfVaultPrivate;
    vault->at(sfSequence) = sequence;
    vault->at(sfOwner) = accountID_;
    vault->at(sfAccount) = pseudoId;
    // Deliberate exception to "every write to sfAssetsTotal /
    // sfAssetsAvailable goes through VaultHelpers.h's addAssetsToVault /
    // removeAssetsFromVault / closeVaultAssets" (base-branch plan §5.5):
    // these are literal zero initializations on a brand-new object, with no
    // rounding discipline to get wrong.
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
    if (dustId)
        vault->at(sfDustAccount) = *dustId;
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
VaultCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
VaultCreate::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
