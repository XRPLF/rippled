#include <xrpl/tx/transactors/vault/VaultSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

/** Gate `sfDomainID` on the `featurePermissionedDomains` amendment.
 *
 *  Allows domain support to be deployed independently of vault support: a
 *  network that has vaults but not permissioned domains will reject any
 *  `VaultSet` that tries to set `sfDomainID`, while all other `VaultSet`
 *  transactions continue to work normally.
 */
bool
VaultSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfDomainID) || ctx.rules.enabled(featurePermissionedDomains);
}

/** Validate transaction fields without ledger access.
 *
 *  Checks are ordered from cheapest to most specific:
 *  1. `sfVaultID` must be non-zero — a zero ID cannot address any real vault.
 *  2. `sfData`, if present, must be non-empty and within `kMAX_DATA_PAYLOAD_LENGTH`.
 *  3. `sfAssetsMaximum`, if present, must be non-negative (zero means no cap).
 *  4. At least one mutable field must be present — a no-op transaction is
 *     rejected as `temMALFORMED` to prevent fee-burning with no ledger effect.
 *
 *  The cap-vs-total comparison (`tecLIMIT_EXCEEDED`) cannot be done here
 *  because `sfAssetsTotal` is only readable from the ledger; that check lives
 *  in `doApply`.
 */
NotTEC
VaultSet::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfVaultID] == beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "VaultSet: zero/empty vault ID.";
        return temMALFORMED;
    }

    if (auto const data = ctx.tx[~sfData])
    {
        if (data->empty() || data->length() > kMAX_DATA_PAYLOAD_LENGTH)
        {
            JLOG(ctx.j.debug()) << "VaultSet: invalid data payload size.";
            return temMALFORMED;
        }
    }

    if (auto const assetMax = ctx.tx[~sfAssetsMaximum])
    {
        if (*assetMax < beast::kZERO)
        {
            JLOG(ctx.j.debug()) << "VaultSet: invalid max assets.";
            return temMALFORMED;
        }
    }

    if (!ctx.tx.isFieldPresent(sfDomainID) && !ctx.tx.isFieldPresent(sfAssetsMaximum) &&
        !ctx.tx.isFieldPresent(sfData))
    {
        JLOG(ctx.j.debug()) << "VaultSet: nothing is being updated.";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

/** Read-only ownership and consistency checks after signature verification.
 *
 *  Checks are ordered to fail as early as possible on the cheapest condition:
 *  1. Vault existence — `tecNO_ENTRY` if the vault SLE is absent.
 *  2. Ownership — `tecNO_PERMISSION` if submitter is not `sfOwner`.
 *  3. Issuance existence — `tefINTERNAL` if the vault's `MPTokenIssuance` is
 *     missing.  This path is covered by `LCOV_EXCL_*` because `VaultCreate`
 *     and the `ValidVault` invariant checker together ensure a vault always
 *     has a corresponding issuance; reaching this branch implies ledger
 *     corruption, not a user error.
 *  4. Domain checks (only when `sfDomainID` is present in the transaction):
 *     - `lsfVaultPrivate` must be set on the vault (`tecNO_PERMISSION`);
 *       domain restrictions can only be applied to private vaults.
 *     - A non-zero domain hash must resolve to an existing
 *       `PermissionedDomain` object (`tecOBJECT_NOT_FOUND`).
 *     - `lsfMPTRequireAuth` must be set on the issuance (`tefINTERNAL`);
 *       this flag is enforced at `VaultCreate` time for private vaults, so
 *       its absence indicates ledger corruption (also `LCOV_EXCL_*`).
 */
TER
VaultSet::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecNO_ENTRY;

    // Assert that submitter is the Owner.
    if (ctx.tx[sfAccount] != vault->at(sfOwner))
    {
        JLOG(ctx.j.debug()) << "VaultSet: account is not an owner.";
        return tecNO_PERMISSION;
    }

    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = ctx.view.read(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.error()) << "VaultSet: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (!vault->isFlag(lsfVaultPrivate))
        {
            JLOG(ctx.j.debug()) << "VaultSet: vault is not private";
            return tecNO_PERMISSION;
        }

        if (*domain != beast::kZERO)
        {
            auto const sleDomain = ctx.view.read(keylet::permissionedDomain(*domain));
            if (!sleDomain)
                return tecOBJECT_NOT_FOUND;
        }

        if ((sleIssuance->getFlags() & lsfMPTRequireAuth) == 0)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.error()) << "VaultSet: issuance of vault shares is not private.";
            return tefINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    return tesSUCCESS;
}

/** Apply vault configuration changes to the mutable ledger view.
 *
 *  Re-fetches the vault and its `MPTokenIssuance` via `view().peek()` to
 *  obtain mutable SLE references, then applies updates in order:
 *
 *  **sfData**: written directly to the vault SLE when present.
 *
 *  **sfAssetsMaximum**: the ledger-state check that `preflight` cannot perform
 *  happens here — if the requested cap is non-zero but less than the current
 *  `sfAssetsTotal`, the transaction fails with `tecLIMIT_EXCEEDED`.  Zero is
 *  the sentinel for "no cap" and always succeeds.
 *
 *  **sfDomainID**: written to (or removed from) the `MPTokenIssuance` SLE, NOT
 *  the vault SLE.  The domain lives on the issuance because the MPT
 *  authorization machinery (`lsfMPTRequireAuth`, `credentials::validDomain`)
 *  operates at the issuance level; this avoids special-casing the vault layer
 *  in the depositor authorization path.  A zero hash calls `makeFieldAbsent`
 *  to clear the restriction compactly rather than storing a zero value.
 *  Clearing the domain does not remove `lsfVaultPrivate` — once private,
 *  always private (making a private vault public is not currently supported).
 *
 *  **view().update(vault)** is always called even when only the issuance
 *  changed.  The `ValidVault` invariant checker inspects modified vault SLEs
 *  to validate the operation; omitting the update would make it invisible to
 *  the checker.
 *
 *  **associateAsset** is the final step and must remain last.  It re-rounds
 *  all `STNumber` / `STTakesAsset`-derived fields in the vault SLE to the
 *  precision of the vault's underlying asset type.  Calling it earlier would
 *  produce silent precision corruption on any field written afterwards.
 */
TER
VaultSet::doApply()
{
    // All return codes in `doApply` must be `tec`, `ter`, or `tes`.
    // As we move checks into `preflight` and `preclaim`,
    // we can consider downgrading them to `tef` or `tem`.

    auto const& tx = ctx_.tx;

    auto vault = view().peek(keylet::vault(tx[sfVaultID]));
    if (!vault)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const vaultAsset = vault->at(sfAsset);

    auto const mptIssuanceID = (*vault)[sfShareMPTID];
    auto const sleIssuance = view().peek(keylet::mptIssuance(mptIssuanceID));
    if (!sleIssuance)
    {
        // LCOV_EXCL_START
        JLOG(j_.error()) << "VaultSet: missing issuance of vault shares.";
        return tefINTERNAL;
        // LCOV_EXCL_STOP
    }

    if (tx.isFieldPresent(sfData))
        vault->at(sfData) = tx[sfData];
    if (tx.isFieldPresent(sfAssetsMaximum))
    {
        if (tx[sfAssetsMaximum] != 0 && tx[sfAssetsMaximum] < *vault->at(sfAssetsTotal))
            return tecLIMIT_EXCEEDED;
        vault->at(sfAssetsMaximum) = tx[sfAssetsMaximum];
    }

    if (auto const domainId = tx[~sfDomainID]; domainId)
    {
        if (*domainId != beast::kZERO)
        {
            sleIssuance->setFieldH256(sfDomainID, *domainId);
        }
        else if (sleIssuance->isFieldPresent(sfDomainID))
        {
            sleIssuance->makeFieldAbsent(sfDomainID);
        }
        view().update(sleIssuance);
    }

    view().update(vault);

    associateAsset(*vault, vaultAsset);

    return tesSUCCESS;
}

void
VaultSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
VaultSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
