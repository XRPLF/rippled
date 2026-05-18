# `VaultInvariant.cpp` — Post-Transaction Invariant Checker for Vault Objects

## Role in the System

`VaultInvariant.cpp` implements `ValidVault`, one of the specialized invariant checkers in XRPL's post-transaction safety net. It is registered as the 24th entry in the `InvariantChecks` tuple defined in `InvariantCheck.h`, where all invariant checkers are composed and run together after every successful or failed transaction application.

The invariant exists because the Vault feature (`featureSingleAssetVault`) introduces a complex web of mutually-consistent ledger objects: a `ltVAULT` object tracking assets and shares, an `ltMPTOKEN_ISSUANCE` for the share token, individual `ltMPTOKEN` entries per depositor, and an `ltACCOUNT_ROOT` for the vault's pseudo-account. Any bug in the transaction handlers could corrupt this web in ways that would be catastrophic — e.g., crediting assets without minting shares, or allowing over-withdrawal. The invariant checker is the last line of defense.

## Two-Phase Pattern: `visitEntry` → `finalize`

Like every invariant in the framework, `ValidVault` works in two phases.

**Phase 1 — `visitEntry`**: called once per touched ledger entry. The checker does not know the transaction type yet, nor does it know which `ltMPTOKEN_ISSUANCE` objects are vault shares versus unrelated MPT issuances. So it takes a defensive approach: it snapshots every `ltVAULT` object it sees into `beforeVault_` / `afterVault_`, and snapshots all `ltMPTOKEN_ISSUANCE` entries into `beforeMPTs_` / `afterMPTs_` for later reconciliation against the vault's `sfShareMPTID`.

The key mechanism in `visitEntry` is the `deltas_` map — a `uint256 → Number` table that captures the net balance change for every interesting ledger entry. The sign convention is: for entries where higher balance means more assets flowing *into* the vault (account roots, trust lines, MPToken holdings), the sign is `-1` (balance decreased means assets went to vault). For `ltMPTOKEN_ISSUANCE`, outstanding amount increases as shares are minted, so sign is `+1`. The delta is computed as `(balanceBefore - balanceAfter) * sign`, which yields a positive number when assets/shares flow into the relevant account.

Notably, a delta entry is stored even when the balance hasn't changed (the comment at line 130 explains this): a transaction may update an account root for other reasons (e.g., sequence number) while having a zero net balance delta, and using a non-zero `sign` as the filter ensures the entry is captured for accounting completeness rather than relying on a zero-comparison that could mask subtle accounting bugs.

**Phase 2 — `finalize`**: called once after all entries have been visited. This is where the actual invariants are evaluated.

## Enforcement vs. Assertion: The `enforce` Pattern

A subtle design decision runs throughout `finalize`: every fatal invariant failure is accompanied by `XRPL_ASSERT(enforce, ...)` before returning `!enforce`. The `enforce` boolean is `true` when the `featureSingleAssetVault` amendment is active.

This two-tier system is explained in `InvariantCheckPrivilege.h`: `XRPL_ASSERT` fires (crashing the process) only in debug/test builds. In a production build with the amendment active, `enforce` is `true`, so `!enforce` is `false`, meaning the transaction is rejected hard. In a test or developer build where the amendment is *not* yet enabled, `!enforce` is `true` — the violation is logged but the transaction is allowed through. This is intentionally painful for developers, designed to surface invariant violations while building vault-adjacent features before the amendment goes live.

## `finalize` Logic Flow

The `finalize` function first dispatches on whether any vault was touched at all, using `hasPrivilege` to cross-check the transaction type's declared capabilities against what actually happened. For example, a transaction with `mustModifyVault` privilege that produced no vault change is a protocol bug; a non-vault transaction that somehow mutated a vault is equally suspicious. The privilege system is a bitmask enumerated in `InvariantCheckPrivilege.h`.

Deletion (`ttVAULT_DELETE`) is handled early as a special case, because it's the only vault-modifying transaction with no "after" vault state. The invariant verifies the deleted vault had zero shares outstanding, zero `assetsTotal`, and zero `assetsAvailable`, and that the corresponding `ltMPTOKEN_ISSUANCE` was co-deleted in the same transaction.

For all other transaction types, `finalize` matches the vault's `sfShareMPTID` against the accumulated MPT issuance snapshots and falls back to reading from the current `ReadView` if the issuance was not itself modified in the transaction. This handles the common case where a deposit touches `sfOutstandingAmount` but the `ltMPTOKEN_ISSUANCE` was not otherwise part of the modified set.

Universal checks applied to every non-delete vault operation include:
- Immutable fields (`sfAsset`, `sfAccount`/pseudo-ID, `sfShareMPTID`) must not change between before and after states.
- `assetsAvailable` ≤ `assetsTotal` ≥ 0; `assetsMaximum` ≥ 0.
- `lossUnrealized` ≤ `assetsTotal - assetsAvailable`.
- `lossUnrealized` must not change except in loan transactions (`ttLOAN_MANAGE`, `ttLOAN_PAY`), since loss tracking is a loan-layer concern.

## Per-Transaction Invariants

**`ttVAULT_CREATE`**: Vault must have been newly created (no "before" state), must be empty (all fields zero), the shares issuer must be a pseudo-account, and the pseudo-account's `sfVaultID` must point back to this vault — ensuring the bidirectional link is always set up atomically.

**`ttVAULT_SET`**: Metadata-only update. The invariant requires that the vault's asset balance at the pseudo-account did not change, `assetsTotal` and `assetsAvailable` are unchanged, and shares outstanding are unchanged. The delta-map lookup for the pseudo-account's key is the mechanism used to detect any accidental asset movement.

**`ttVAULT_DEPOSIT`**: The vault's asset balance must increase, the depositor's asset balance must decrease by the same magnitude, and the depositor's share holdings must increase. The `assetsTotal` and `assetsAvailable` fields must each increase by exactly `vaultDeltaAssets`. One nuance: if the depositor is the asset's IOU issuer, their balance does not change (issuer payments create supply rather than moving existing funds), so the account-side delta check is bypassed with `issuerDeposit`.

**`ttVAULT_WITHDRAW`**: Symmetric to deposit. The vault's asset balance decreases, the destination's balance increases by the same amount. The destination may differ from `sfAccount` if `sfDestination` is set (directed withdrawal). The invariant enforces exactly one destination account changes balance — not both `sfAccount` and `sfDestination`. Issuer withdrawals are similarly exempted from the balance-equality check.

**`ttVAULT_CLAWBACK`**: The asset issuer may forcibly withdraw assets and burn shares. The invariant first validates the caller is either the asset's issuer or — as a special case — the vault *owner* performing an emergency share-burn of an empty vault (e.g., to recover from a state where shares exist but assets do not). The holder's shares must decrease, and the corresponding issuance outstanding must decrease by the same amount.

**Loan transactions** (`ttLOAN_SET`, `ttLOAN_MANAGE`, `ttLOAN_PAY`): Currently checked as TBD placeholders returning `true`, indicating the loan invariant logic is delegated to the separate `ValidLoan` and `ValidLoanBroker` checkers.

## Asset Type Polymorphism

The `deltaAssets` lambda handles all three XRPL asset types through `std::visit` on the `Asset` variant. For XRP it looks up the `AccountRoot` key; for IOU trust lines it looks up the `RippleState` key and applies a sign flip based on which side of the trust line the account is on (`id > issue.getIssuer() ? -1 : 1`, reflecting the asymmetric balance storage in `ltRIPPLE_STATE`); for MPT assets it looks up the `MPToken` key for the specific holder. This polymorphism allows the same delta-accounting logic to work correctly regardless of what asset a vault holds.

## Fee Compensation for XRP Vaults

For XRP-denominated vault deposits and withdrawals, the transaction fee must be added back to the sender's observed balance delta before comparing against the vault's delta. Without this correction, a depositor who also pays a fee would appear to have sent more XRP than the vault received. The compensation is skipped for delegated transactions (where `sfDelegate` differs from `sfAccount`), because in that case the delegate — not the sender — pays the fee and the delegate's balance is tracked separately.