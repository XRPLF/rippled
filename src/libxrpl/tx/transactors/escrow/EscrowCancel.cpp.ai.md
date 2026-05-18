# `EscrowCancel.cpp` — Escrow Cancellation Transactor

## Role in the System

`EscrowCancel.cpp` implements the `EscrowCancel` transaction type in the XRPL transaction engine. Its sole job is to unwind an escrow whose conditions were never met by the time its `CancelAfter` deadline passed, returning the locked funds to the original owner. This is the "expiry refund" path in the escrow lifecycle: if neither party triggers a successful `EscrowFinish` before the deadline, any party can submit an `EscrowCancel` to reclaim the funds once the ledger's parent close time has advanced past `sfCancelAfter`.

The file participates in the standard three-phase transactor pipeline defined by the `Transactor` base class: `preflight` (stateless format checks), `preclaim` (read-only ledger validation), and `doApply` (mutating ledger state).

## Phase 1: `preflight` — Intentional Stub

`EscrowCancel::preflight` returns `tesSUCCESS` unconditionally. This is deliberate: an `EscrowCancel` transaction carries only `sfOwner` and `sfOfferSequence` to identify the escrow object; there are no variable-length cryptographic fields or format invariants to validate at the syntax level. The original XRP-only escrow design had nothing to check here, and the token escrow extension (guarded by `featureTokenEscrow`) adds no new transaction fields that need syntactic validation either.

## Phase 2: `preclaim` — Read-Only Authorization Checks

`EscrowCancel::preclaim` is entirely gated on the `featureTokenEscrow` amendment. For classic XRP escrows the method returns `tesSUCCESS` immediately, deferring the existence check to `doApply`. This apparent redundancy (the existence check happens again in `doApply`) is a consequence of the amendment structure: the XRP escrow code predates the token escrow feature, and the additional preclaim logic was bolted on alongside it.

When `featureTokenEscrow` is active, `preclaim` reads the escrow SLE identified by `keylet::escrow(sfOwner, sfOfferSequence)` and, for non-XRP amounts, dispatches to one of two template specializations via a `std::visit` over the asset's `value()` variant:

**`escrowCancelPreclaimHelper<Issue>`** validates IOU escrows. It guards against the degenerate `issuer == account` case (which should be impossible by construction but returns `tecINTERNAL` defensively), then calls `requireAuth` to confirm the escrow owner is still authorized by the issuing account. Authorization must remain valid at cancel time, not just at creation time.

**`escrowCancelPreclaimHelper<MPTIssue>`** handles Multi-Purpose Token escrows. It adds an extra step not needed for IOUs: it explicitly looks up the `MPTIssuance` object via `keylet::mptIssuance` and returns `tecOBJECT_NOT_FOUND` if it has been deleted since the escrow was created. It then checks authorization using `AuthType::WeakAuth` rather than the strict auth used for IOUs. This distinction matters: MPT authorization can be "weak" (the holder is allowed to participate without issuer per-transaction approval), reflecting the different trust model MPTs operate under.

## Phase 3: `doApply` — State Mutation

`doApply` performs the actual ledger surgery and is where all the interesting invariant enforcement happens.

**Escrow lookup:** The escrow is located via `ctx_.view().peek()` (mutable access) using the same key as `preclaim`. If the object is missing and `featureTokenEscrow` is enabled, this returns `tecINTERNAL` — a missing object at apply time after a successful preclaim means the ledger is in an inconsistent state. For legacy XRP escrows (where preclaim performed no existence check), the missing-object case returns `tecNO_TARGET` instead.

**Time guard:** Two checks enforce the cancellation window. If the escrow has no `sfCancelAfter` field at all, cancellation is permanently forbidden — this covers escrows that use only a `FinishAfter` condition with no expiry. If `sfCancelAfter` is present but the current ledger's `parentCloseTime` has not yet passed it, the attempt is also rejected with `tecNO_PERMISSION`. The `after()` helper abstracts the comparison of ledger timestamps.

**Directory cleanup:** An escrow object can be linked into up to three owner directories. The primary entry is always in the `ownerDir` of the escrow's `sfAccount` (the original creator) under `sfOwnerNode`. An optional entry may exist in the recipient's `ownerDir` under `sfDestinationNode`, recorded at escrow creation time if a destination was specified. For non-XRP token escrows, there may be a third entry in the issuer's `ownerDir` under `sfIssuerNode`. Each removal uses `ctx_.view().dirRemove()` with a hard failure path (`tefBAD_LEDGER`) if the directory entry cannot be found — directory inconsistency is treated as fatal ledger corruption.

**Fund return:** For XRP, the funds are returned by directly incrementing the owner's `sfBalance`. This is a direct balance manipulation rather than a payment, bypassing payment routing entirely. For non-XRP assets, `escrowUnlockApplyHelper<T>` (defined in `EscrowHelpers.h`) handles the IOU trust-line or MPT balance transfer. A critical detail in the cancel path is that both `sender` and `receiver` are set to `account` — the escrow owner — because the cancel operation refunds the funds back to their source. The `createAsset` flag is `account == account_`, where `account_` is the transaction submitter; this is `true` when the escrow owner is canceling their own escrow, permitting the helper to auto-create a trust line or MPToken holding if one was somehow absent, and `false` otherwise. `parityRate` is passed as the `lockedRate`, meaning no transfer fee is applied on the refund path (unlike `EscrowFinish`, which may apply a fee if the rate was locked at creation and has since changed).

**Owner count:** `adjustOwnerCount` decrements the escrow owner's reserve count by one, releasing the reserve that was held against the escrow object since its creation.

## Design Tradeoffs

The split between `preclaim` and `doApply` for the existence check is a legacy artifact. XRP escrows perform the check only in `doApply` because the original code predated the separation of read-only and mutating phases. Token escrow added `preclaim` checks to surface authorization failures earlier (before consuming compute in `doApply`), but rather than refactoring the XRP path, the new checks are entirely behind the amendment flag.

The `createAsset = (account == account_)` heuristic for whether to auto-create a trust line or MPToken holding during a cancel is slightly subtle. Since the funds go back to the escrow creator (`account`), the question is whether to create missing ledger objects for that account. Allowing creation only when the submitter is the owner prevents a third-party canceler from unilaterally creating objects (and reserve obligations) on another account's behalf during a cancel operation.