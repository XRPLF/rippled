# `VaultDelete.h` — Vault Deletion Transactor

## Role in the System

`VaultDelete` is the transactor responsible for removing an XRPL Single-Sided AMM vault and all of its associated ledger objects. It sits alongside `VaultCreate`, `VaultSet`, `VaultDeposit`, `VaultWithdraw`, and `VaultClawback` in the vault sub-module of XRPL transaction processing, and it is the only one that performs a full multi-object teardown of the vault lifecycle.

The file itself is a minimal header: it declares the class, wires up the constructor, and exposes the three static/virtual hooks that the `Transactor` framework calls during transaction processing. The substantive logic lives in `VaultDelete.cpp`.

## Class Design

`VaultDelete` inherits from `Transactor` and follows the standard three-phase dispatch pattern enforced by the base class template `invokePreflight<T>`:

1. **`preflight(PreflightContext const&)`** — a stateless, read-only sanity check that runs before ledger state is consulted. It confirms that `sfVaultID` is non-zero; a zero vault ID is an immediate `temMALFORMED` rejection.

2. **`preclaim(PreclaimContext const&)`** — a read-only check against the current ledger view. This phase enforces the three-invariant guard for safe deletion: the submitting account must be the vault `sfOwner`; `sfAssetsAvailable` must be zero; `sfAssetsTotal` must be zero; and the MPTokenIssuance backing the vault shares must have `sfOutstandingAmount` equal to zero. Any nonzero asset or share count returns `tecHAS_OBLIGATIONS`, preventing destruction of a vault that still holds depositor funds.

3. **`doApply()`** — the state-mutating phase. This is where the real complexity lives.

`ConsequencesFactory` is set to `Normal`, matching `VaultCreate` and the other vault transactors. Unlike `VaultCreate`, `VaultDelete` does not override `checkExtraFeatures` or `getFlagsMask`, relying on the base-class defaults — meaning no additional amendment check is needed beyond what `invokePreflight` performs via `Permission::getInstance().getTxFeature`.

## The `doApply()` Teardown Sequence

Destroying a vault requires cleaning up five distinct ledger objects in a specific order that maintains invariants at each step:

1. **Asset holding** — the vault's pseudo-account holds a zero-balance token or XRP holding representing the underlying asset. `removeEmptyHolding()` is called to remove it from the pseudo-account's directory.

2. **Share MPTokenIssuance** — the vault's vault shares are tracked as an MPT issuance. The code explicitly avoids the `MPTokenIssuanceDestroy` transactor path ("no special logic needed") and instead directly removes the issuance's directory entry, adjusts the pseudo-account's owner count, and erases the SLE.

3. **Vault owner's MPToken for shares** — if the vault owner holds an `MPToken` for the vault's share issuance (e.g., received shares they didn't redeem), `removeEmptyHolding()` clears that token too before the issuance is erased.

4. **Pseudo-account** — each vault has an associated pseudo-account (an `AccountRoot` SLE bearing an `sfVaultID` back-reference). After the above steps, this account must have zero balance, zero owner count, and no remaining directory. Three defensive `tecHAS_OBLIGATIONS` guards confirm this before the SLE is erased. These guards are annotated `// LCOV_EXCL_START`, marking them as theoretically unreachable — they protect against bugs in the cleanup steps rather than legitimate user-triggered states.

5. **Vault SLE + owner directory** — the vault object is removed from the owner's `ownerDir`, owner count is decremented by **2** (one for the vault object, one for the pseudo-account that was also tracked against the owner's reserve), and the vault SLE itself is erased.

## Error Handling Philosophy

The distinction between `tec` and `tef` errors in `doApply()` is deliberate. Conditions that a caller might legitimately trigger — vault not found, non-owner submitter, outstanding assets — return `tec` codes (claimable fee errors). Conditions that should be impossible given correct ledger state — missing pseudo-account, mismatched issuance owner, non-zero pseudo-account balance after cleanup — return `tefBAD_LEDGER` or `tefINTERNAL`, which signal ledger corruption and prevent the fee from being claimed. The `LCOV_EXCL_START` markers on these paths make explicit the authors' expectation that test coverage cannot reach them through valid transaction sequences.

## Relationship to Other Vault Transactors

`VaultDelete` is the inverse of `VaultCreate`. Where `VaultCreate` allocates the vault SLE, pseudo-account, and share MPTokenIssuance and charges owner reserves, `VaultDelete` reclaims all of those objects and releases two owner-count units. The symmetry is enforced by `preclaim`'s `tecHAS_OBLIGATIONS` guards, which ensure deletion is only possible once `VaultWithdraw` has reduced all balances to zero — making `VaultDelete` the terminal state of the vault lifecycle.