# `MPTokenIssuanceDestroy.cpp` — Transactor for Destroying an MPT Issuance

## Role and Purpose

This file implements the `MPTokenIssuanceDestroy` transactor, which handles the lifecycle event of permanently removing a Multi-Party Token (MPT) issuance object from the XRPL ledger. It is one of a family of MPT-related transactors — alongside `MPTokenIssuanceCreate`, `MPTokenIssuanceSet`, and `MPTokenAuthorize` — that together manage the full lifecycle of fungible token types on the ledger.

The transactor follows the standard XRPL three-phase execution model defined in the `Transactor` base class: `preflight` (stateless syntax/rule checks), `preclaim` (read-only ledger validation), and `doApply` (state mutation). This separation is architecturally important: `preflight` and `preclaim` are static methods, allowing the engine to cheaply screen transactions before committing ledger access, while `doApply` is an instance method that mutates the ledger view.

## Phase Analysis

### `preflight`

The `preflight` implementation is intentionally trivial — it simply returns `tesSUCCESS`. This is by design: a destroy transaction carries only an `sfMPTokenIssuanceID` field and no flags or complex parameters that require offline validation. All meaningful constraints are ledger-state-dependent and therefore deferred to `preclaim`.

### `preclaim`

This phase performs all substantive validation against a read-only snapshot of the ledger:

1. **Existence check** — The issuance is looked up via `keylet::mptIssuance(ctx.tx[sfMPTokenIssuanceID])`. If the ledger entry is absent, `tecOBJECT_NOT_FOUND` is returned. This subsumes any concern about whether the ID itself is well-formed, since the keylet lookup implicitly validates its format.

2. **Ownership check** — The `sfIssuer` field on the ledger object must match `sfAccount` from the transaction. Any other account — even one with administrative authority — cannot destroy someone else's issuance. The error is `tecNO_PERMISSION`.

3. **Outstanding balance check** — `sfOutstandingAmount` must be exactly zero. This enforces a critical economic invariant: you cannot destroy a token type while holders still have positive balances. The destroy operation would otherwise eliminate the accounting record for real token holdings, corrupting the ledger state.

4. **Locked amount check** — `sfLockedAmount` is an optional field (accessed via the `~` optional-field operator and defaulted with `value_or(0)`); if present and non-zero, the operation is also blocked with `tecHAS_OBLIGATIONS`. Locked amounts represent tokens held in escrow or protocol-controlled positions that cannot be unilaterally freed by the issuer. The `LCOV_EXCL_LINE` annotation on this branch indicates it is unreachable under current test coverage — a known gap in coverage for this defensive check.

The separation of outstanding vs. locked amount checks is deliberate: they represent distinct accounting concepts (circulating supply vs. protocol-held collateral), and future protocol changes may handle them differently.

### `doApply`

The application phase performs three ledger mutations, all of which must succeed atomically as part of the transaction:

1. **Issuer re-verification** — The first statement re-checks that `account_` (the transactor's cached account ID) matches the issuance's `sfIssuer`. This appears redundant with `preclaim` but is a defensive invariant: `doApply` runs against a mutable view that could, in theory, diverge from the read-only view used during `preclaim`. The `tecINTERNAL` error here (also `LCOV_EXCL_LINE`) would indicate a framework-level bug rather than user error.

2. **Owner directory removal** — `view().dirRemove(keylet::ownerDir(account_), (*mpt)[sfOwnerNode], mpt->key(), false)` removes the issuance's entry from the issuer's owner directory. The `sfOwnerNode` field stored on the ledger object is a back-pointer into the directory, making this O(1) removal rather than a linear search. If the directory entry is not found, `tefBAD_LEDGER` is returned, signalling ledger structural corruption. The final `false` argument indicates the directory itself should not be deleted even if it becomes empty.

3. **Object erasure and owner count adjustment** — `view().erase(mpt)` removes the `MPTokenIssuance` SLE from the ledger, and `adjustOwnerCount(..., -1, j_)` decrements the issuer's reserve-tracked owner count. These two operations are the symmetric inverse of what `MPTokenIssuanceCreate::create()` does: that function inserts the SLE, inserts the directory entry, and increments the owner count by 1. The destroy operation cleanly unwinds this state.

## Design Tradeoffs and Invariants

The strict "zero outstanding balance" requirement before destruction is the central economic safety guarantee. An alternative design might allow destruction with non-zero supply (implicitly burning remaining tokens), but XRPL's ledger model tracks holder `MPToken` objects separately — simply deleting the issuance record while those objects exist would leave orphaned entries. The `tecHAS_OBLIGATIONS` error forces the issuer to coordinate with all holders to redeem/burn their tokens before the issuance type itself can be removed.

The `sfOwnerNode` back-pointer pattern is pervasive across XRPL object types: by storing the directory page index at object-creation time, removal never needs to scan the owner directory. This file's `doApply` relies on that invariant having been correctly established by `MPTokenIssuanceCreate`.

There are no concurrency concerns specific to this file — XRPL ledger application is single-threaded per ledger close. The `view()` mutable accessor and `ctx_.tx` access patterns follow the standard transactor contract throughout.