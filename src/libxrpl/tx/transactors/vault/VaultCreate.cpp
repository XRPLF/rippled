#include <xrpl/tx/transactors/vault/VaultCreate.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
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

namespace xrpl {

/** Pure amendment gate called before `preflight1`.
 *
 *  Rejects the transaction with `temDISABLED` if `featureMPTokensV1` is not
 *  active, because vaults rely entirely on MPT share issuance.  A secondary
 *  gate protects `sfDomainID`: using it requires `featurePermissionedDomains`,
 *  so the two features can activate independently without cross-coupling.
 *  Keeping these checks here (rather than in `preflight`) means a disabled
 *  transaction is rejected before any field parsing occurs.
 */
bool
VaultCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return false;

    if (ctx.tx.isFieldPresent(sfDomainID) && !ctx.rules.enabled(featurePermissionedDomains))
        return false;

    return true;
}

/** Returns `tfVaultCreateMask`, restricting valid flags to `tfVaultPrivate`
 *  and `tfVaultShareNonTransferable`.  Any bit outside this mask causes
 *  `preflight0` to reject with `temINVALID_FLAG`.
 */
std::uint32_t
VaultCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfVaultCreateMask;
}

/** Stateless, ledger-free field validation run between `preflight1` and
 *  `preflight2`.
 *
 *  Checks in order, returning `temMALFORMED` on the first violation:
 *  - `sfData` length must not exceed `kMAX_DATA_PAYLOAD_LENGTH`.
 *  - `sfWithdrawalPolicy`, if present, must equal
 *    `kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE` — the only currently supported
 *    strategy; the field exists for future extensibility.
 *  - `sfDomainID`, if present, must be non-zero and `tfVaultPrivate` must be
 *    set.  Associating a domain with a public vault would be incoherent:
 *    public vaults admit all holders, so domain-restricted access has no
 *    effect.
 *  - `sfAssetsMaximum`, if present, must not be negative.
 *  - `sfMPTokenMetadata`, if present, must be non-empty and within
 *    `kMAX_MP_TOKEN_METADATA_LENGTH`.
 *  - `sfScale`, if present, is only valid for IOU assets.  XRP and MPT assets
 *    have fixed integer representations for which a scale factor is meaningless,
 *    so they are rejected.  The upper bound `kVAULT_MAXIMUM_IOU_SCALE` (18) is
 *    chosen because 10^19 exceeds the maximum MPT amount (2^63 − 1 ≈ 10^18.9),
 *    ensuring that a single IOU unit can always be converted to at least one
 *    share.
 */
NotTEC
VaultCreate::preflight(PreflightContext const& ctx)
{
    if (!validDataLength(ctx.tx[~sfData], kMAX_DATA_PAYLOAD_LENGTH))
        return temMALFORMED;

    if (auto const withdrawalPolicy = ctx.tx[~sfWithdrawalPolicy])
    {
        if (*withdrawalPolicy != kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE)
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::kZERO)
        {
            return temMALFORMED;
        }
        if ((ctx.tx.getFlags() & tfVaultPrivate) == 0)
        {
            // DomainID only allowed on private vaults
            return temMALFORMED;
        }
    }

    if (auto const assetMax = ctx.tx[~sfAssetsMaximum])
    {
        if (*assetMax < beast::kZERO)
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty() || metadata->length() > kMAX_MP_TOKEN_METADATA_LENGTH)
            return temMALFORMED;
    }

    if (auto const scale = ctx.tx[~sfScale])
    {
        auto const vaultAsset = ctx.tx[sfAsset];
        if (vaultAsset.holds<MPTIssue>() || vaultAsset.native())
            return temMALFORMED;

        if (scale > kVAULT_MAXIMUM_IOU_SCALE)
            return temMALFORMED;
    }

    return tesSUCCESS;
}

/** Read-only ledger checks executed after signature verification.
 *
 *  Checks in order, returning the first failing code:
 *  1. `canAddHolding` — delegates to the asset-type-appropriate helper to
 *     verify the pseudo-account will be able to hold the given asset (e.g.,
 *     MPT issuance limits and similar constraints).
 *  2. Pseudo-account issuer rejection — assets issued by a pseudo-account
 *     (e.g., shares of another vault or AMM LP tokens) are rejected with
 *     `tecWRONG_ASSET`.  Such assets cannot be clawed back through the normal
 *     mechanism because the issuer has no private key and no direct authority
 *     path; allowing the vault to hold them would create an irrecoverable
 *     liability if emergency intervention were ever needed.
 *  3. Freeze check — returns `tecFROZEN` (IOU) or `tecLOCKED` (MPT) if the
 *     asset is frozen for the vault owner.
 *  4. Domain existence — if `sfDomainID` is present, the referenced
 *     `PermissionedDomain` object must exist; returns `tecOBJECT_NOT_FOUND`
 *     otherwise.
 *  5. Address collision — pre-computes the deterministic pseudo-account address
 *     from the vault keylet; if the result is zero (collision with an existing
 *     account), returns `terADDRESS_COLLISION` before any state change.
 */
TER
VaultCreate::preclaim(PreclaimContext const& ctx)
{
    auto const vaultAsset = ctx.tx[sfAsset];
    auto const account = ctx.tx[sfAccount];

    if (auto const ter = canAddHolding(ctx.view, vaultAsset))
        return ter;

    if (!vaultAsset.native())
    {
        if (isPseudoAccount(ctx.view, vaultAsset.getIssuer()))
            return tecWRONG_ASSET;
    }

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
        accountId == beast::kZERO)
        return terADDRESS_COLLISION;

    return tesSUCCESS;
}

/** Mutates the ledger to create the vault and all supporting infrastructure.
 *
 *  All return codes here are `tec`, `ter`, or `tes`; a `tec` return signals
 *  the framework to discard all mutations via `ApplyContext::discard()`, then
 *  re-apply only the fee and sequence.  Steps are ordered so that the reserve
 *  check occurs before any irreversible state is written.
 *
 *  Execution order:
 *  1. **Directory link + owner count**: `dirLink` inserts the new vault SLE
 *     into the owner's directory.  `adjustOwnerCount` then increments by 2 —
 *     one for the vault SLE and one for the pseudo-account — *before* the
 *     reserve check.  This order is intentional: the reserve must reflect the
 *     true post-creation object count to prevent the account from being left
 *     underfunded.  If `preFeeBalance_` is insufficient, returns
 *     `tecINSUFFICIENT_RESERVE` and the framework rolls back all mutations.
 *  2. **Pseudo-account**: `createPseudoAccount` creates a new `AccountRoot`
 *     SLE at the deterministically-derived address, storing the vault's key in
 *     `sfVaultID`.  This synthetic account holds the pooled asset, issues
 *     shares, and has no private key — it is controlled solely by the
 *     transactor logic.  Failure here is an invariant violation that `preclaim`
 *     should have prevented; the branch is marked `LCOV_EXCL_LINE`.
 *  3. **Empty asset holding**: `addEmptyHolding` establishes a zero-balance
 *     `MPToken` (MPT assets) or `TrustLine`/`RippleState` (IOU assets) on the
 *     pseudo-account, ready to receive deposits.  XRP vaults require no holding
 *     object, but the dispatch is handled uniformly.
 *  4. **Share MPT issuance**: `MPTokenIssuanceCreate::create` is called from
 *     the pseudo-account's perspective at sequence 1 (its first issuance).
 *     This creates the `MPTokenIssuance` SLE that represents vault shares —
 *     distinct from the asset holding created in step 3.  Flag mapping:
 *     `tfVaultShareNonTransferable` absent → `lsfMPTCanEscrow | lsfMPTCanTrade
 *     | lsfMPTCanTransfer`; `tfVaultPrivate` set → `lsfMPTRequireAuth`.
 *  5. **Vault SLE population**: All fields are written to the in-memory SLE
 *     before `view().insert` commits it: `sfAsset`, `sfFlags` (private bit
 *     only), `sfOwner`, `sfAccount` (pseudo-account ID), `sfAssetsTotal`,
 *     `sfAssetsAvailable`, `sfLossUnrealized` (all zero), optional
 *     `sfAssetsMaximum`, `sfShareMPTID`, `sfData`, `sfWithdrawalPolicy`
 *     (defaulting to `kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE`), and `sfScale`
 *     (omitted for XRP/MPT assets where scale is always 0).
 *  6. **Owner MPToken authorization**: `authorizeMPToken` creates an `MPToken`
 *     SLE for the vault creator so they can hold shares immediately.  For
 *     private vaults, a second call authorizes the pseudo-account itself (with
 *     the owner as the authorizing party), which is required for the
 *     pseudo-account to participate in share issuance mechanics.
 *  7. **`associateAsset`**: Must be the final write.  Propagates the vault's
 *     asset type through all `sMD_NeedsAsset` fields (primarily `STNumber`
 *     fields), tying the asset's decimal scale to the number representation
 *     so that serialization rounds correctly.
 */
TER
VaultCreate::doApply()
{
    auto const& tx = ctx_.tx;
    auto const sequence = tx.getSeqValue();
    auto const owner = view().peek(keylet::account(account_));
    if (owner == nullptr)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto vault = std::make_shared<SLE>(keylet::vault(account_, sequence));

    if (auto ter = dirLink(view(), account_, vault))
        return ter;
    adjustOwnerCount(view(), owner, 2, j_);
    auto const ownerCount = owner->at(sfOwnerCount);
    if (preFeeBalance_ < view().fees().accountReserve(ownerCount))
        return tecINSUFFICIENT_RESERVE;

    auto maybePseudo = createPseudoAccount(view(), vault->key(), sfVaultID);
    if (!maybePseudo)
        return maybePseudo.error();  // LCOV_EXCL_LINE
    auto& pseudo = *maybePseudo;
    auto pseudoId = pseudo->at(sfAccount);
    auto asset = tx[sfAsset];

    if (auto ter = addEmptyHolding(view(), pseudoId, preFeeBalance_, asset, j_); !isTesSuccess(ter))
        return ter;

    std::uint8_t const scale = (asset.holds<MPTIssue>() || asset.native())
        ? 0
        : ctx_.tx[~sfScale].value_or(kVAULT_DEFAULT_IOU_SCALE);

    auto txFlags = tx.getFlags();
    std::uint32_t mptFlags = 0;
    if ((txFlags & tfVaultShareNonTransferable) == 0)
        mptFlags |= (lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer);
    if ((txFlags & tfVaultPrivate) != 0u)
        mptFlags |= lsfMPTRequireAuth;

    // This creates the MPTokenIssuance for vault *shares*, not for the
    // underlying asset.  The asset holding (MPToken or TrustLine/RippleState)
    // was already created above via addEmptyHolding — the two calls are
    // structurally similar, and the distinction is easy to miss.
    auto maybeShare = MPTokenIssuanceCreate::create(
        view(),
        j_,
        {
            .priorBalance = std::nullopt,
            .account = pseudoId->value(),
            .sequence = 1,
            .flags = mptFlags,
            .assetScale = scale,
            .transferFee = std::nullopt,
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
            .mutableFlags = std::nullopt,
        });
    if (!maybeShare)
        return maybeShare.error();  // LCOV_EXCL_LINE
    auto const& mptIssuanceID = *maybeShare;

    vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
    vault->at(sfFlags) = txFlags & tfVaultPrivate;
    vault->at(sfSequence) = sequence;
    vault->at(sfOwner) = account_;
    vault->at(sfAccount) = pseudoId;
    vault->at(sfAssetsTotal) = Number(0);
    vault->at(sfAssetsAvailable) = Number(0);
    vault->at(sfLossUnrealized) = Number(0);
    if (auto value = tx[~sfAssetsMaximum])
        vault->at(sfAssetsMaximum) = *value;
    vault->at(sfShareMPTID) = mptIssuanceID;
    if (auto value = tx[~sfData])
        vault->at(sfData) = *value;
    if (auto value = tx[~sfWithdrawalPolicy])
    {
        vault->at(sfWithdrawalPolicy) = *value;
    }
    else
    {
        vault->at(sfWithdrawalPolicy) = kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE;
    }
    if (scale != 0u)
        vault->at(sfScale) = scale;
    view().insert(vault);

    if (auto const err =
            authorizeMPToken(view(), preFeeBalance_, mptIssuanceID, account_, ctx_.journal);
        !isTesSuccess(err))
        return err;

    if ((txFlags & tfVaultPrivate) != 0u)
    {
        if (auto const err = authorizeMPToken(
                view(), preFeeBalance_, mptIssuanceID, pseudoId, ctx_.journal, {}, account_);
            !isTesSuccess(err))
            return err;
    }

    associateAsset(*vault, asset);

    return tesSUCCESS;
}

void
VaultCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
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
