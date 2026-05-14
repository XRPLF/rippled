#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `ttVAULT_SET` transaction type.
 *
 *  Allows the vault owner to modify the mutable properties of an existing
 *  vault after creation.  The three writable fields are: `sfData` (arbitrary
 *  off-chain metadata), `sfAssetsMaximum` (deposit cap), and `sfDomainID`
 *  (permissioned-domain gate for private vaults).  All other vault properties
 *  are fixed at `VaultCreate` time and cannot be changed.
 *
 *  The three-phase pipeline is:
 *  - `checkExtraFeatures` / `preflight` — amendment gating and field
 *    validation (no ledger access)
 *  - `preclaim` — read-only ownership and domain checks
 *  - `doApply` — updates the Vault SLE and, for domain changes, the
 *    associated `MPTokenIssuance` SLE
 *
 *  @note `sfDomainID` can only be set on vaults that were created with
 *      `lsfVaultPrivate`.  A zero value clears an existing domain restriction;
 *      it is not possible to retroactively restrict a public vault.
 *  @note The vault SLE is always marked dirty via `view().update(vault)` even
 *      when only the issuance SLE changes — this gives the `ValidVault`
 *      invariant checker a signal that a `VaultSet` occurred.
 *  @see VaultCreate, VaultDeposit, VaultWithdraw, VaultClawback, VaultDelete
 */
class VaultSet : public Transactor
{
public:
    /** Standard fee and sequence consequences; does not block queued transactions. */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit VaultSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transaction on required amendments.
     *
     *  Returns `false` (producing `temDISABLED`) if `sfDomainID` is present
     *  in the transaction but `featurePermissionedDomains` is not yet active.
     *  Called by `invokePreflight<VaultSet>` before `preflight1`, keeping the
     *  domain-feature availability check separate from per-field branching in
     *  `preflight`.
     *
     *  @param ctx  Preflight context.
     *  @return `true` if all required amendments are active; `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Perform stateless, ledger-free field validation.
     *
     *  Validates the following constraints (returning `temMALFORMED` on any
     *  violation):
     *  - `sfVaultID` must be non-zero.
     *  - `sfData`, if present, must be non-empty and at most
     *    `kMAX_DATA_PAYLOAD_LENGTH` bytes.
     *  - `sfAssetsMaximum`, if present, must be non-negative.
     *  - At least one of `sfData`, `sfAssetsMaximum`, or `sfDomainID` must be
     *    present; a transaction that sets none of these fields is rejected as a
     *    no-op to prevent fee-burning with no effect.
     *
     *  @param ctx  Preflight context carrying the transaction and active rules.
     *  @return `tesSUCCESS` or `temMALFORMED`.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Perform read-only ledger checks after signature verification.
     *
     *  Checks, in order (returning the first failing code):
     *  - The vault ledger object for `sfVaultID` must exist; returns
     *    `tecNO_ENTRY` if absent.
     *  - The submitting account must be the vault's `sfOwner`; returns
     *    `tecNO_PERMISSION` otherwise.
     *  - The vault's `MPTokenIssuance` SLE must exist; returns `tefINTERNAL`
     *    if missing (a defensive guard marked `LCOV_EXCL_*` because
     *    `VaultCreate` and the invariant checker prevent this state).
     *  - If `sfDomainID` is being set, `lsfVaultPrivate` must be active on the
     *    vault; returns `tecNO_PERMISSION` for public vaults.
     *  - A non-zero `sfDomainID` must resolve to an existing
     *    `PermissionedDomain` object; returns `tecOBJECT_NOT_FOUND` if not.
     *
     *  @param ctx  Preclaim context carrying a read-only ledger view.
     *  @return `tesSUCCESS`, `tecNO_ENTRY`, `tecNO_PERMISSION`,
     *      `tecOBJECT_NOT_FOUND`, or `tefINTERNAL`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply vault configuration changes to the mutable ledger view.
     *
     *  Applies updates in order:
     *  - `sfData`: written directly to the Vault SLE if present.
     *  - `sfAssetsMaximum`: written to the Vault SLE if present; returns
     *    `tecLIMIT_EXCEEDED` if a non-zero cap would be lower than the
     *    vault's current `sfAssetsTotal`.
     *  - `sfDomainID`: written to or removed from the `MPTokenIssuance` SLE
     *    (not the Vault SLE) because domain enforcement for MPT-based vaults
     *    lives at the issuance level.  A zero value calls `makeFieldAbsent` to
     *    clear the restriction.
     *
     *  The Vault SLE is always marked dirty via `view().update(vault)` even
     *  when only the issuance changed, so the `ValidVault` invariant checker
     *  can observe the operation.  `associateAsset` is called last to re-round
     *  stored numeric values to the asset's precision.
     *
     *  @return `tesSUCCESS`, `tecLIMIT_EXCEEDED`, or `tefINTERNAL`.
     */
    TER
    doApply() override;

    /** Accumulate per-entry data for transaction-specific invariant checks.
     *
     *  Currently a no-op placeholder; no transaction-specific invariants are
     *  defined for `VaultSet` yet.
     *
     *  @param isDelete  `true` if the entry was erased.
     *  @param before    Entry state before the transaction (nullptr if new).
     *  @param after     Entry state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Evaluate transaction-specific post-conditions after all entries are visited.
     *
     *  Currently a no-op placeholder that always returns `true`; no
     *  transaction-specific invariants are defined for `VaultSet` yet.
     *
     *  @param tx     The transaction being applied.
     *  @param result Tentative TER result.
     *  @param fee    Fee consumed by the transaction.
     *  @param view   Read-only ledger view after the transaction.
     *  @param j      Journal for logging invariant failures.
     *  @return `true` (all invariants pass).
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
