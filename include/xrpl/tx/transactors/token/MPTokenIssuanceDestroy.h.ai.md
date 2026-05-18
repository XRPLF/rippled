# `MPTokenIssuanceDestroy.h` — Transactor for Destroying MPToken Issuances

## Role in the System

This header declares the `MPTokenIssuanceDestroy` transactor, which handles transaction type `ttMPTOKEN_ISSUANCE_DESTROY` (opcode 55) on the XRP Ledger. Its sole responsibility is permanently deleting a Multi-Party Token (MPT) issuance object from the ledger — reclaiming the issuer's owner-count slot and removing the corresponding entry from the owner directory. The transaction is gated behind the `featureMPTokensV1` amendment and is classified as delegable with the `destroyMPTIssuance` privilege, meaning an authorized delegate account may submit it on the issuer's behalf.

## Three-Phase Transactor Pipeline

Like every `Transactor` subclass, `MPTokenIssuanceDestroy` participates in the framework's three-phase execution model. The class exposes `preflight`, `preclaim`, and `doApply` as the three checkpoints. The base `Transactor::invokePreflight<T>` template wires these together at compile time, interleaving them with the framework's own signature validation and flag-mask checks — no virtual dispatch is involved at the `preflight`/`preclaim` layer.

**`preflight`** unconditionally returns `tesSUCCESS`. This is an intentional design: every meaningful precondition for this transaction depends on reading current ledger state, which is unavailable during preflight (the stage that runs without a ledger view). There are no transaction-field validity checks beyond what the framework itself enforces (correct sequence, valid fee, well-formed signature). Keeping `preflight` trivial is correct and cheap.

**`preclaim`** is where all substantive guards live. It reads the MPToken issuance object from the ledger using `keylet::mptIssuance(sfMPTokenIssuanceID)` and enforces three invariants before allowing the transaction to proceed:

1. The issuance object must exist (`tecOBJECT_NOT_FOUND` if not).
2. The `sfIssuer` field of that object must match the transaction submitter (`tecNO_PERMISSION` if not). This prevents any account from destroying an issuance it does not own.
3. Both `sfOutstandingAmount` and `sfLockedAmount` must be zero (`tecHAS_OBLIGATIONS` if either is non-zero). An issuance can only be destroyed when no tokens remain in circulation and no tokens are locked — this protects token holders from having their holdings orphaned by a premature destruction.

The `sfLockedAmount` branch carries `// LCOV_EXCL_LINE`, signalling that test coverage does not reach it. This reflects a protocol invariant: if `sfOutstandingAmount` is already zero, `sfLockedAmount` being non-zero would represent a corrupted ledger state that the normal transaction flow cannot produce. The check is nonetheless present as a defensive backstop.

**`doApply`** executes the actual ledger mutation once both prior phases have cleared. It:
1. Peeks the MPT issuance SLE via `view().peek(keylet::mptIssuance(...))`.
2. Asserts the cached `account_` is the issuer (the `tecINTERNAL` branch is also `LCOV_EXCL_LINE` — unreachable in practice since `preclaim` already validated ownership).
3. Removes the issuance from the account's owner directory via `view().dirRemove(...)`. A failure here returns `tefBAD_LEDGER`, indicating ledger corruption rather than a user error.
4. Erases the issuance SLE with `view().erase(mpt)`.
5. Decrements the issuer's owner count by one via `adjustOwnerCount`, keeping the reserve accounting accurate.

## Design Observations

The `ConsequencesFactory` is set to `Normal`, meaning the transaction has standard fee consequences and does not act as a blocker in the transaction queue. This is appropriate: destroying an issuance is a low-priority cleanup operation that does not need to preempt other transactions.

The absence of a `preclaim` override in the base `Transactor` (which simply returns `tesSUCCESS`) is overridden here with a `static TER preclaim(PreclaimContext const&)` — the static method pattern the framework uses for compile-time polymorphism rather than virtual dispatch. Derived transactors override by name-hiding, not by virtual override, so the signature exactly matches the base class declaration with the same static qualifier.

Compared to `MPTokenIssuanceCreate`, this class is notably simpler: it carries no helper `create()`-style static utility, no custom `checkExtraFeatures`, and no `getFlagsMask` override. Destruction is a single-object, single-issuer operation with no fields beyond the issuance ID, so the minimal surface area is appropriate.

## Relationship to Sibling Files

Within `include/xrpl/tx/transactors/token/`, `MPTokenIssuanceDestroy.h` sits alongside `MPTokenIssuanceCreate.h`, `MPTokenIssuanceSet.h`, `MPTokenAuthorize.h`, `Clawback.h`, and `TrustSet.h`. Together these form the full lifecycle management surface for MPTs on the ledger — creation, configuration, holder authorization, clawback, and destruction. The auto-generated `include/xrpl/protocol_autogen/transactions/MPTokenIssuanceDestroy.h` provides a separate type-safe builder/wrapper (`MPTokenIssuanceDestroyBuilder`) for constructing the transaction's `STTx` representation, completely independent of this transactor header.