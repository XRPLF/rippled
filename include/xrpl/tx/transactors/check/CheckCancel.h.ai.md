# `CheckCancel.h` — CheckCancel Transaction Transactor

`CheckCancel` is the transactor responsible for removing a Check ledger object from the XRP Ledger. It participates in the standard three-phase transaction pipeline inherited from `Transactor`: `preflight` for stateless validation, `preclaim` for ledger-state validation, and `doApply` for committed state mutation.

## Role in the Check Subsystem

The `check/` directory contains exactly three transactors — `CheckCreate`, `CheckCash`, and `CheckCancel` — mirroring the full lifecycle of a Check object. `CheckCreate` writes the on-ledger object and reserves owner funds; `CheckCash` redeems it; `CheckCancel` tears it down without transferring value. `CheckCancel` is the only path for reclaiming the owner reserve when a check goes unused, whether because it expired or the parties agreed not to proceed.

## Class Design

`CheckCancel` inherits `Transactor` and adds no new data members; its constructor simply forwards `ApplyContext&` to the base. The `ConsequencesFactory` tag is set to `Normal`, meaning the transaction system treats a `CheckCancel` as a routine fee-paying transaction with no special blocking semantics.

Unlike `CheckCreate` and `CheckCash`, `CheckCancel` does **not** override `checkExtraFeatures`. This is intentional: cancellation is always permitted regardless of which amendments are active — it is a cleanup operation, not a capability gate.

## Validation Phases

`preflight` is a stub that returns `tesSUCCESS` immediately. All meaningful validation is deferred to `preclaim`, which has access to the read-only ledger state. `preclaim` resolves the Check by `sfCheckID`, fails with `tecNO_ENTRY` if it does not exist, then enforces the permission model: if the check has **not** yet expired (tested against the parent ledger's close time, the only definitively known timestamp), only the **source account** or the **destination account** may cancel it. An expired check may be removed by anyone. This asymmetry keeps expired objects purgeable without requiring the original parties.

## State Mutation in `doApply`

`doApply` performs three coordinated writes against the mutable `ApplyView`:

1. **Destination owner-directory removal** — the Check's `sfDestinationNode` field stores the page index in the destination's owner directory; `dirRemove` uses this for O(1) removal without a linear scan. This step is skipped if source and destination are the same account (a degenerate but theoretically valid case).
2. **Source owner-directory removal** — symmetrically uses `sfOwnerNode` to remove the entry from the creator's owner directory.
3. **Owner reserve adjustment** — calls `adjustOwnerCount` with `-1` to release the reserve increment that `CheckCreate` claimed.
4. **SLE erasure** — the Check ledger object itself is erased.

The two `dirRemove` calls are each guarded by a `LCOV_EXCL` block that returns `tefBAD_LEDGER` on failure. These branches are unreachable in a correctly functioning ledger — the stored page indices are immutable after creation and `preclaim` already confirmed the Check exists — so they serve as defensive invariant checks rather than expected error paths.