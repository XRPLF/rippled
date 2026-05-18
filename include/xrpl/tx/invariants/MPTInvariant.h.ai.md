# `MPTInvariant.h` — Invariant Checkers for Multi-Purpose Token Ledger Consistency

This header declares two invariant checker classes, `ValidMPTIssuance` and `ValidMPTPayment`, that guard the XRPL ledger's MPT (Multi-Purpose Token) subsystem from state corruption. Both classes plug into the general-purpose invariant framework defined in `InvariantCheck.h` and are included in the `InvariantChecks` tuple that the engine iterates after every transaction application.

## The Invariant Framework Pattern

Every invariant checker in the XRPL engine exposes the same two-phase contract: `visitEntry()` is called once per modified `SLE` (Signed Ledger Entry) during transaction application, and `finalize()` is called once after all entries have been visited to render a pass/fail verdict. The split exists because invariants must reason about the *aggregate* effect of a transaction — you cannot tell if MPT issuance counts are correct by looking at one ledger entry in isolation.

Critically, `finalize()` must perform meaningful checks even when the transaction failed. A bug or exploit could cause a failed transaction to mutate ledger state in unexpected ways, so invariants are the last line of defence. Both classes in this file respect that contract.

## `ValidMPTIssuance` — Structural Integrity of MPT Lifecycle Objects

`ValidMPTIssuance` accumulates four counters during `visitEntry()`:

- `mptIssuancesCreated_` / `mptIssuancesDeleted_` count `ltMPTOKEN_ISSUANCE` entries.
- `mptokensCreated_` / `mptokensDeleted_` count `ltMPTOKEN` holder entries.
- `mptCreatedByIssuer_` flags the edge case where an `MPToken` was auto-created for the issuance's own issuer account, which is always an error.

In `finalize()`, the class consults the transaction's *privilege* flags (from `InvariantCheckPrivilege.h`) to determine exactly what structural changes were permitted. The privilege system is a bitmask enum: `createMPTIssuance`, `destroyMPTIssuance`, `mustAuthorizeMPT`, `mayAuthorizeMPT`, `mayCreateMPT`, and `mayDeleteMPT` encode what each transaction type is *allowed* to do to MPT ledger objects.

The invariant then enforces tight accounting rules:

- A transaction with `createMPTIssuance` privilege must have created exactly one `ltMPTOKEN_ISSUANCE` and deleted zero.
- A transaction with `destroyMPTIssuance` privilege must have deleted exactly one and created zero.
- A transaction with `mustAuthorizeMPT` (submitted by a holder) must have created or deleted exactly one `ltMPTOKEN`.
- Transactions with `mayAuthorizeMPT` (e.g., `ttAMM_WITHDRAW`, `ttAMM_CLAWBACK`) are subject to more permissive but still bounded limits: at most one `ltMPTOKEN` created, at most two deleted (because an empty two-asset AMM pool can shed both holder objects on withdrawal).
- Transactions with `mayCreateMPT` — including `ttAMM_CREATE` (up to two, for an MPT/MPT pool) and `ttCHECK_CASH` (at most one) — may auto-create `ltMPTOKEN` entries for receiving accounts that didn't already hold the token.
- Any transaction that has none of these privileges must have left all MPT object counts at zero.

An important nuance is amendment gating. Several checks only *enforce* (i.e., return `false`) when specific feature flags such as `featureMPTokensV2`, `featureSingleAssetVault`, or `featureLendingProtocol` are enabled. Before these amendments activate, a failing assertion is still logged at fatal severity, but the transaction is not rejected. The `assert(enforce)` pattern documented in `InvariantCheckPrivilege.h` exploits the fact that asserts fire in debug/test builds but not production, providing an early-warning system for developers working on pre-amendment code paths.

## `ValidMPTPayment` — Conservation of Outstanding Amounts

`ValidMPTPayment` enforces the fundamental accounting invariant for MPT value flows: after any successful transaction, the `OutstandingAmount` field on each `ltMPTOKEN_ISSUANCE` must equal the sum of all individual `ltMPTOKEN` balances (the `MPTAmount` field plus the `LockedAmount` field for each holder entry).

The class stores this as a `hash_map<uint192, MPTData>` keyed by MPT ID (`uint192` being the 192-bit issuance identifier). Each `MPTData` holds a two-element array for the before/after `OutstandingAmount` snapshot and a signed `mptAmount` accumulator representing the net delta across all holder `ltMPTOKEN` entries (`after - before`, summed).

The conservation equation verified in `finalize()` is:

```
OutstandingAmount[After] == OutstandingAmount[Before] + sum(MPTAmount[After] - MPTAmount[Before])
```

Overflow is treated as a first-class concern. Because MPT amounts can be up to `maxMPTokenAmount` (a large 64-bit quantity), the code checks for overflow at every arithmetic step during `visitEntry()` and sets the `overflow_` flag rather than risking undefined behaviour. In `finalize()`, an overflow immediately fails the invariant with a fatal log message. The enforcement is again amendment-gated: the check only hard-fails (returns `false`) when `featureMPTokensV2` is enabled.

Unlike `ValidMPTIssuance`, the `finalize()` method on `ValidMPTPayment` is non-`const` — a consequence of the hash-map accumulator being mutated lazily during visit rather than being fully pre-built.

## Relationship to the Broader Invariant System

`ValidMPTIssuance` appears near the middle of the `InvariantChecks` tuple in `InvariantCheck.h`, while `ValidMPTPayment` is the last entry. This ordering matters because the engine runs all checks unconditionally and any failure causes the transaction to be reverted. The MPT issuance structural check and the MPT payment conservation check are intentionally separate classes rather than one combined checker because they address orthogonal concerns: one guards *object lifecycle* (did the right ledger entries appear or disappear?), while the other guards *numeric conservation* (are token balances consistent with the issuance's outstanding total?). Splitting them also keeps each class small and its logic straightforward to audit.