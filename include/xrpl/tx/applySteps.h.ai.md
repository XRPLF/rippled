# `include/xrpl/tx/applySteps.h`

This header defines the public interface for the XRPL transaction application pipeline — the structured sequence `preflight → preclaim → doApply` that every transaction must traverse before being committed to an open ledger. It is the architectural seam between the protocol's validation logic and the ledger's mutation layer.

## The Three-Stage Pipeline

The core design pattern in `applySteps.h` is the explicit decomposition of transaction processing into three sequential, independently cacheable stages. Each stage produces a typed result struct that must be passed to the next, making the pipeline both type-safe and auditable.

**`preflight`** performs ledger-agnostic validation — format checks, signature structure, fee field sanity, and any static constraints that require only the transaction and current protocol rules. Critically, `preflight` produces a `TxConsequences` object alongside its `TER` result, which the Transaction Queue (TxQ) uses for worst-case XRP accounting before the transaction ever touches a ledger. The `preflight` result can be safely cached and reused across multiple ledger versions. There are two overloads: one for standard transactions and one that accepts a `parentBatchId`, used when a transaction belongs to a `Batch` transaction group.

**`preclaim`** performs ledger-dependent validation — sequence/ticket checks, signature verification, fee sufficiency, and transaction-type-specific pre-conditions. Because ledger state can change between when `preflight` was cached and when `preclaim` runs, `preclaim` detects rules mismatches: if the rules embedded in `preflightResult` differ from the rules in the supplied `OpenView` (because the ledger advanced), `preclaim` automatically re-runs `preflight` with the updated rules before proceeding. This makes it safe for TxQ to hold stale `PreflightResult` values across ledger boundaries.

**`doApply`** performs the actual ledger mutation. It only executes if `preclaimResult.likelyToClaimFee` is true — meaning the transaction either succeeded pre-checks (`tes`) or is a hard-fail `tec` that will charge a fee regardless. As a defensive measure, `doApply` checks that the view's sequence number matches what `preclaim` saw; if they differ, it returns `tefEXCEPTION` rather than applying to an inconsistent ledger snapshot.

## `TxConsequences` — Pre-Application Cost Analysis

`TxConsequences` answers a specific question the TxQ needs resolved before a transaction runs: *how much XRP can this transaction consume in the worst case?* This is not about the transaction's actual effects but its worst-case claim on the submitting account's balance, used to determine whether queued follow-on transactions remain viable.

The class tracks five properties: whether the transaction is a `blocker`, the `fee_`, the `potentialSpend_` (XRP moved beyond the fee, e.g., in a Payment), a `SeqProxy` capturing the transaction's sequence or ticket, and `sequencesConsumed_` for transactions that burn multiple sequences (such as `TicketCreate`). The `followingSeq()` helper computes what sequence number should follow this transaction — essential for validating the queue ordering of subsequent transactions from the same account.

The `blocker` category is a subtle but important flag. When `SetRegularKey` removes a regular key (or similar key-management operations run), subsequent queued transactions from that account may no longer be able to claim fees because their signature might become invalid. Marking a transaction as a blocker signals TxQ to stop processing further queued transactions from that account until the blocker is finalized.

The constructors are deliberately split into five variants rather than using a single struct with defaulted fields. Each variant captures a specific invariant: the `NotTEC` constructor (for failed preflight) zeroes everything to ensure no cost estimate leaks from a rejected transaction; the category constructor delegates to the base and then flips `isBlocker_`; and the `potentialSpend` and `sequencesConsumed` constructors similarly extend the normal case. This prevents callers from accidentally constructing an inconsistent consequences object.

## `PreflightResult` and `PreclaimResult` — Immutable Pipeline Tokens

Both result structs make copy-assignment `= delete` while allowing copy-construction. This is a deliberate anti-tampering design noted in the comments: all fields are `const`, and the only way to obtain a valid result is to call the corresponding function. There is no way to construct a `PreflightResult` or `PreclaimResult` with arbitrary field values and pass it into the next stage — the template constructor pulls fields directly from the context object.

`PreclaimResult` computes `likelyToClaimFee` in its constructor initializer list as `isTesSuccess(ter) || isTecClaimHardFail(ter, flags)`. This means the downstream `doApply` doesn't need to re-evaluate fee behavior — it simply reads this cached boolean.

## `isTecClaimHardFail` — Soft vs. Hard Fee Claims

A `tec` result is a protocol-level "claim a fee and fail" outcome — the transaction fails to execute but the network still charges the submitter. However, when `tapRETRY` is set in `ApplyFlags`, the TxQ is treating the transaction as a soft failure that might succeed later (e.g., after another transaction in the queue runs first). In that mode, a `tec` should not be treated as a definitive fee claim, because the transaction is not actually being applied yet.

`isTecClaimHardFail` returns true when a `tec` will definitely result in a fee charge — i.e., the retry flag is absent. This inline predicate appears in `PreclaimResult`'s initializer and also in the TxQ's own fee reasoning, making it the authoritative definition of "this transaction will cost the submitter money."

## Fee Calculation Utilities

`calculateBaseFee` dispatches through the `with_txn_type` macro mechanism to the transaction-type-specific static method, returning the actual fee floor for that specific transaction. `calculateDefaultBaseFee` bypasses the type dispatch and calls `Transactor::calculateBaseFee` directly, returning what a plain "reference" transaction would cost. The TxQ uses this distinction when computing a transaction's fee *level* — the ratio of its actual fee to the reference fee — for prioritization and admission decisions.

## Relationship to `apply.h`

`apply.h` wraps the three-step pipeline into a single `apply()` call for callers that don't need intermediate results. `applySteps.h` exists because the TxQ is the principal reason to split the stages: it runs `preflight` once when a transaction arrives, caches the result, and may run `preclaim` and `doApply` much later (possibly after re-queuing across ledger closes). Without this split, the TxQ would need to re-validate transactions from scratch on every application attempt.