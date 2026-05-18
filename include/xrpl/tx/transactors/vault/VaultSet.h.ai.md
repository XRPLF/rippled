# `VaultSet.h` — Vault Configuration Transactor

`VaultSet` is the transactor that allows a vault owner to modify the mutable properties of an existing XRPL Single-Sided AMM vault after it has been created. It lives alongside `VaultCreate`, `VaultDelete`, `VaultDeposit`, `VaultWithdraw`, and `VaultClawback` in the vault transactor family, all of which share the same `Transactor` base pipeline.

## Role in the Vault Lifecycle

While `VaultCreate` establishes a vault and fixes its immutable identity (the underlying asset, the share MPT issuance, private/public status), `VaultSet` governs the three fields that remain writable after creation: `sfData` (an arbitrary byte payload for off-chain metadata), `sfAssetsMaximum` (a deposit cap), and `sfDomainID` (a permissioned-domain gate for private vaults). Any other mutation requires destroying and recreating the vault.

## Pipeline Phases

`VaultSet` follows the standard XRPL three-phase validation pipeline inherited from `Transactor`. The phases use compile-time name hiding rather than virtual dispatch — `invokePreflight<VaultSet>` calls `VaultSet::checkExtraFeatures`, `VaultSet::preflight`, and the base `preflight1`/`preflight2` in a fixed sequence.

**`checkExtraFeatures`** provides a lightweight amendment gate. It returns `false` (causing `temDISABLED`) if the transaction carries an `sfDomainID` field but the `featurePermissionedDomains` amendment is not yet active on the network. This cleanly separates the domain feature's availability from the broader vault feature flag, without any per-field branching in `preflight`.

**`preflight`** runs entirely against the transaction object with no ledger access. Three structural invariants are enforced here:

1. `sfVaultID` must be non-zero — a zero vault ID cannot identify any real ledger object.
2. `sfData`, if present, must be non-empty and within `maxDataPayloadLength` bytes. Both bounds matter: an empty blob is rejected to prevent storing useless entries, and the upper bound caps chain bloat.
3. `sfAssetsMaximum`, if present, must be non-negative.

A critical no-op guard rejects the transaction as `temMALFORMED` when none of the three mutable fields are present. This prevents fee-burning transactions that would accomplish nothing — a pattern that appears across several XRPL transactors.

Unlike `VaultCreate`, `VaultSet` does not override `getFlagsMask`, so it inherits the base implementation returning `tfUniversalMask`. This signals that the transaction carries no transaction-type-specific flags.

**`preclaim`** performs ledger-state checks with read-only access to the current view:

- The vault ledger object must exist for the given `sfVaultID`; absent vaults return `tecNO_ENTRY`.
- The submitting account must match the vault's `sfOwner`; unauthorized updates return `tecNO_PERMISSION`. Only the original creator can alter the vault's configuration.
- If `sfDomainID` is being set, the vault must have been created with `lsfVaultPrivate`. This enforces a one-way door: a vault created as public can never be retroactively restricted to a permissioned domain. The reverse (lifting domain restriction from a private vault) is permitted — by sending a zero `sfDomainID`.
- A non-zero `sfDomainID` must resolve to an existing permissioned domain object (`tecOBJECT_NOT_FOUND` if not).
- The vault's associated `MPTokenIssuance` object must still exist. This path is guarded with `LCOV_EXCL_*` markers because it represents a defensive check against a state that `VaultCreate` and the invariant checker should prevent from ever occurring.

**`doApply`** performs the actual ledger mutations. `sfData` and `sfAssetsMaximum` are updated directly on the vault SLE. A subtle constraint on `sfAssetsMaximum` prevents the cap from being lowered below the vault's current `sfAssetsTotal` — you cannot cap a vault below what it already holds (`tecLIMIT_EXCEEDED`). For `sfDomainID`, the field is written to or removed from the `MPTokenIssuance` SLE (not the vault SLE itself), because domain enforcement for MPT-based vaults lives at the issuance level. Sending zero clears the domain restriction by calling `makeFieldAbsent`.

A deliberate design note in the implementation: the vault SLE is always marked dirty via `view().update(vault)` even when the only change was to the issuance SLE. The comment explains this is required so the vault invariant checker can observe the operation — without an update to the vault object, the invariant verifier has no signal that a `VaultSet` occurred in this ledger.

## Relationship to `VaultCreate`

The `ConsequencesFactory{Normal}` constant is identical to `VaultCreate`'s — both transactions produce normal consequences, meaning they do not block other transactions in a batch from the same account. This differs from transactors like account-level configuration changes that might use `Blocker`.

The structure of `VaultSet.h` is intentionally minimal: it declares only the four pipeline hooks that differ from the base class defaults. The absence of `getFlagsMask` is meaningful — `VaultCreate` overrides it to expose vault-private creation flags, while `VaultSet` has no creation-time flags to expose.