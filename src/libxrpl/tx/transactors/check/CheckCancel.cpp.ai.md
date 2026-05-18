# `CheckCancel.cpp` — Canceling Checks on the XRPL

## Role in the System

This file implements the `CheckCancel` transactor, one of three components (alongside `CheckCreate` and `CheckCash`) that together manage the XRPL's deferred payment mechanism. A Check ledger object represents a standing authorization to transfer value — the sender has not yet moved funds, but has declared intent and locked a reserve. `CheckCancel` is the teardown path: it removes the check from the ledger, releases the owner reserve, and cleans up both directory entries that reference the object. It serves two distinct use cases — voluntary abortion by either party before expiration, and post-expiration sweeping by any account.

## Class Structure

`CheckCancel` inherits from `Transactor` and declares `ConsequencesFactory` as `Normal`, meaning the consensus engine treats it as a standard fee-paying transaction with no special fee escalation behavior. The `preflight` and `preclaim` methods are `static`, operating only on their context parameters without any instance state, while `doApply` is a virtual override that accesses the mutable ledger view through the base class.

## The Three-Phase Execution Model

**`preflight`** is intentionally empty — it returns `tesSUCCESS` unconditionally. There is nothing about the cancel transaction that can be validated without ledger state: whether the check exists, whether it's expired, and whether the submitter is authorized all depend on current ledger contents. Contrast this with `CheckCreate::preflight`, which validates self-sends, amount sanity, and expiration format — purely structural properties of the transaction itself. The blank `CheckCancel::preflight` is architecturally correct, not a gap.

**`preclaim`** handles all semantic validation against a read-only view. It encodes two distinct authorization regimes:

1. If the check has **expired** (its `sfExpiration` has passed the parent ledger's close time), any account may cancel it. Expired checks are stale obligations that anyone can sweep from the ledger.

2. If the check has **not yet expired**, only the original creator (`sfAccount` on the check SLE) or the designated destination (`sfDestination`) may cancel. A third party cannot unilaterally void a valid, outstanding check.

The expiration comparison deliberately uses the **parent ledger's close time**, not the ledger currently being constructed. This is a determinism constraint: the closing time of the in-progress ledger is not finalized at transaction application time, so using it would produce different results across validators. The parent ledger's close time is consensus-agreed and immutable, making the expiration check fully deterministic.

Unauthorized cancellation returns `tecNO_PERMISSION` — a `tec`-class error meaning the transaction still claims the transaction fee even though it fails, consistent with how the XRPL handles fee-charging for ledger-state-level rejections.

## Ledger Mutation in `doApply`

The apply phase executes three sequential operations that mirror in reverse what `CheckCreate::doApply` set up.

**Directory cleanup** is the most structurally significant operation. When a check is created, it is inserted into the owner directories of both the source account and the destination account. The check SLE caches the directory page numbers for both insertions as `sfOwnerNode` and `sfDestinationNode`. `CheckCancel` reads these cached page numbers and calls `view().dirRemove()` with them directly, enabling O(1) removal without scanning directory pages. The guard `if (srcId != dstId)` skips the destination directory removal for self-checks — a case that `CheckCreate` explicitly rejects with `temREDUNDANT`, so this guard is a defensive measure for ledger consistency rather than a live code path.

**Owner reserve release** via `adjustOwnerCount(view(), sleSrc, -1, viewJ)` decrements the source account's `sfOwnerCount` by one. This releases the XRP reserve increment that was allocated when the check was created. Only the source account's count is adjusted because only the source "owns" the check object for reserve purposes — the destination has a directory reference but bears no reserve obligation.

**SLE deletion** with `view().erase(sleCheck)` removes the check object itself from the ledger state.

## Defensive Coding Patterns

The `view().dirRemove()` failure branches are wrapped in `// LCOV_EXCL_START` / `// LCOV_EXCL_STOP` markers, indicating they are excluded from coverage requirements and considered unreachable in a well-formed ledger. If directory entries are inconsistent with the SLE, it signals ledger corruption rather than a user error, hence the `tefBAD_LEDGER` return code and `fatal`-level log. The `tef` prefix means the transaction is not applied and the fee is not charged.

The existence check on `sleCheck` inside `doApply` using `view().peek()` is redundant given that `preclaim` already verified the check exists. This is a deliberate defensive guard against any theoretical divergence between the read-only `preclaim` view and the mutable `doApply` view — if somehow the check disappeared between phases (which the framework prevents in practice), the fallback returns `tecNO_ENTRY` rather than crashing.

## Relationship to Sibling Files

`CheckCreate.cpp` is the direct inverse of `CheckCancel`: where `CheckCreate::doApply` calls `dirInsert` twice and `adjustOwnerCount(..., +1, ...)`, `CheckCancel::doApply` calls `dirRemove` twice and `adjustOwnerCount(..., -1, ...)`. The `sfOwnerNode` and `sfDestinationNode` fields written by `CheckCreate` are the exact page indices consumed by `CheckCancel`.

`CheckCash.cpp` shares the cleanup responsibility — it must also remove the check SLE, its directory entries, and adjust the owner count, but it additionally handles value delivery (XRP or IOU/MPT), trust line interactions, and partial payment semantics. `CheckCancel` is structurally simpler because it destroys the promise without ever honoring it.