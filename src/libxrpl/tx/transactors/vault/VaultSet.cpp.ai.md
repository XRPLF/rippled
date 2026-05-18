# VaultSet.cpp — Vault Metadata and Access Policy Update Transactor

`VaultSet.cpp` implements the `VaultSet` transactor, which allows the owner of an on-ledger vault to update a limited set of mutable vault properties without affecting the vault's core structure or asset balances. It lives in the `vault` transactor group alongside `VaultCreate`, `VaultDeposit`, `VaultWithdraw`, `VaultDelete`, and `VaultClawback`.

## Role in the Vault Subsystem

A vault in XRPL is a ledger object (`SLE` keyed by `keylet::vault`) that acts as a pooled asset container. Vaults are backed by a pseudo-account that holds the actual assets, and share ownership is represented via an `MPTokenIssuance` object whose ID is stored as `sfShareMPTID` on the vault. `VaultCreate` establishes all the immutable structure of a vault; `VaultSet` then handles post-creation configuration. Only three fields are intentionally mutable after creation: the data payload (`sfData`), the asset cap (`sfAssetsMaximum`), and the permissioned domain (`sfDomainID`).

## Validation Pipeline

The class follows the standard three-phase XRPL transactor model.

`checkExtraFeatures()` is the earliest gate: if the transaction carries an `sfDomainID`, it verifies that the `featurePermissionedDomains` amendment is enabled on the network, returning `false` (reject) otherwise. This allows the feature to be deployed independently of vault support.

`preflight()` performs stateless field-level validation before any ledger access. It enforces that `sfVaultID` is not the zero hash, that `sfData` (if present) is neither empty nor exceeds `maxDataPayloadLength`, and that `sfAssetsMaximum` (if present) is non-negative. Crucially, it also requires that at least one of the three mutable fields is present — a no-op transaction is rejected as `temMALFORMED` rather than silently accepted.

`preclaim()` reads the ledger to enforce ownership and consistency. It verifies the vault exists (`tecNO_ENTRY`), confirms the submitting account matches `sfOwner` stored on the vault (`tecNO_PERMISSION`), and checks that the `MPTokenIssuance` for vault shares is present. The issuance check uses `tefINTERNAL` (marked `LCOV_EXCL_START`) because the missing-issuance path represents an invariant violation — a vault should never exist without its issuance. Domain-related checks only trigger when `sfDomainID` is present in the transaction: the vault must have been created with `lsfVaultPrivate`, the referenced domain must exist in the ledger (`tecOBJECT_NOT_FOUND` if not, unless zero), and the issuance must carry `lsfMPTRequireAuth` (another `tefINTERNAL` sanity guard, enforced at `VaultCreate` time).

## Application Logic in doApply()

`doApply()` performs the actual mutations. It re-fetches the vault and issuance with `view().peek()` (mutable references) and applies each field if present in the transaction.

**AssetsMaximum update** has a runtime semantic check that `preflight` cannot perform: if the new maximum is non-zero and is less than the current `sfAssetsTotal`, the transaction fails with `tecLIMIT_EXCEEDED`. Zero is the sentinel meaning "no limit," so setting it to zero always succeeds and removes any existing cap.

**DomainID update** is architecturally interesting: the domain is not stored on the vault SLE itself — it lives on the underlying `MPTokenIssuance` object. `VaultSet` writes the domain directly to `sleIssuance` via `setFieldH256`, then calls `view().update(sleIssuance)`. Setting `sfDomainID` to the zero hash clears the field entirely (`makeFieldAbsent`) rather than storing zero, preserving compactness on the ledger. Clearing the domain does not remove `lsfVaultPrivate` from the vault — once a vault is private, it stays private. A private vault with no domain falls back to rejecting all non-owner depositors with `tecNO_AUTH`, as checked by `VaultDeposit::preclaim`.

A subtle comment in the code explains why `view().update(vault)` is always called, even when only the issuance changed: the vault invariant checker needs to see the vault as modified so it can validate the operation. Skipping the vault update would make invariant verification unreliable.

**`associateAsset()` call** at the end is a precision-binding step. It iterates all fields in the vault SLE that carry the `sMD_NeedsAsset` metadata flag (fields derived from `STTakesAsset`, such as `STNumber`) and calls their virtual `associateAsset()` method with the vault's underlying asset. This ties the `STNumber` precision to the asset type — for example, so asset totals stored as numbers are rounded to the correct decimal scale for the vault's currency. The same call appears in every vault-modifying transactor.

## Design Observations

The decision to store `sfDomainID` on the `MPTokenIssuance` rather than on the vault SLE itself is architecturally driven: the MPT authorization machinery (`lsfMPTRequireAuth`, domain-based credential validation) operates on issuances, not vaults. By keeping the domain on the issuance, the depositor authorization path in `VaultDeposit` can use the standard `credentials::validDomain` check without special-casing the vault layer.

The `lsfVaultPrivate` flag is intentionally made immutable by design. The comment in `doApply()` explicitly notes that making a private vault public is not currently supported. This one-way privacy boundary simplifies the trust model: depositors in a private vault can rely on its access control never being retroactively removed.