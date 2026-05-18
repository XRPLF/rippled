# `Batch.cpp` — Batch Transaction Transactor

`Batch.cpp` implements the transactor for `ttBATCH`, XRPL's mechanism for bundling multiple inner transactions into a single outer transaction with a defined execution policy. The file lives in the "system" transactor subdirectory alongside `Change`, `LedgerStateFix`, and `TicketCreate` — transactors that operate at a protocol-infrastructure level rather than against ordinary user accounts.

## Why Batches Exist

Without batches, submitting several related transactions requires coordination across ledger closes, each carrying independent fees and each risking partial execution. A batch lets a group of accounts (or one account with many transactions) express "apply all of these, or none" (or other policies) as a single atomic unit with a single combined fee.

## Execution Policies and the `doApply` Split

`Batch::doApply()` returns `tesSUCCESS` unconditionally and does nothing else. This is intentional: the outer batch transaction only needs to land in the ledger and consume its fee. The actual execution of inner transactions is deferred to `applyBatchTransactions()` in `apply.cpp`, which is called by `applyTransaction()` after `doApply` completes successfully.

`applyBatchTransactions` creates a nested `OpenView` called `wholeBatchView`. Each inner transaction runs inside its own further-nested `perTxBatchView`, and successful changes are promoted to `wholeBatchView` one at a time. Only if at least one inner transaction succeeds is `wholeBatchView` merged back into the primary ledger view. The four execution policies — whose flag bits are checked with `std::popcount` in `preflight` to guarantee exactly one is active — map directly to control flow in that loop:

- **`tfAllOrNothing`**: returns `false` (discards `wholeBatchView`) on the first inner failure.
- **`tfUntilFailure`**: breaks on the first failure but keeps all prior successes.
- **`tfOnlyOne`**: breaks immediately after the first success, applying only that transaction.
- **`tfIndependent`**: runs every inner transaction regardless of individual outcomes; all successes commit.

## Fee Architecture in `calculateBaseFee`

The fee formula is: `batchBase + Σ(inner tx fees) + signerCount × view.fees().base`, where `batchBase` itself equals `view.fees().base + Transactor::calculateBaseFee(view, tx)`. The extra `view.fees().base` per batch signer reflects the cost of verifying each multi-party batch signature — one base unit per key (whether the signer used a single signature or multi-sign, the count is expanded accordingly by examining `sfSigners` sub-arrays).

Every intermediate addition in `calculateBaseFee` is preceded by an explicit overflow check against `std::numeric_limits<XRPAmount::value_type>::max()`. These guards are wrapped in `LCOV_EXCL_START/STOP` because they are unreachable under any valid transaction structure — `preflight` already enforces that array counts and structures are within limits before `calculateBaseFee` would ever be called with malformed data. They exist purely as defense in depth.

## `preflight`: Structural Validation

`preflight` is a dense structural validator that runs before any state-aware checks. Its key decisions:

**Inner transaction authentication model**: Inner transactions must have an empty `sfSigningPubKey`, no `sfTxnSignature`, and no `sfSigners`. They do not self-authenticate; the outer account signs the outer transaction, and batch signers (other accounts whose inner transactions are included) sign a separate batch payload. `preflight` enforces this by rejecting any inner transaction that carries conventional signature fields. The optional `sfCounterpartySignature` field is treated the same way — if present, it must not contain any signature material.

**Zero-fee requirement**: Every inner transaction must carry a fee of exactly zero XRP. The batch's combined fee (computed by `calculateBaseFee`) covers the cost of all inner transactions.

**Duplicate and nesting prevention**: A `std::unordered_set<uint256>` tracks inner transaction hashes; duplicates return `temREDUNDANT`. Nested `ttBATCH` inner transactions are explicitly rejected with `temINVALID`. A compile-time `disabledTxTypes` array in the header blocks vault (`ttVAULT_*`) and loan (`ttLOAN_*`) transaction types entirely — these have multi-step state machines (deposit, withdraw, clawback) whose invariants would be difficult to reason about under batch atomicity.

**Sequence integrity**: Every inner transaction must carry exactly one of `sfSequence` (nonzero) or `sfTicketSequence` — both present or both absent is invalid. For `tfAllOrNothing` and `tfUntilFailure` modes, duplicate sequence or ticket values across inner transactions from the same account are detected and rejected at this phase. The `tfIndependent` and `tfOnlyOne` modes relax this constraint because partial success is acceptable — two inner transactions from the same account consuming the same sequence slot can coexist when only one might actually execute.

**Inner preflight recursion**: Each inner transaction has `xrpl::preflight` called on it with `tapBATCH` and the outer batch's transaction ID as `parentBatchId`. This propagates the full normal preflight pipeline (feature checks, flag validation, field sanity) to every inner transaction before the outer batch is accepted.

## `preflightSigValidated`: Signer Authorization

This runs after the outer transaction's own signature is verified by the framework (the `invokePreflight` template in `Transactor.h` calls `preflight2` for outer signature verification before calling `preflightSigValidated`). The method builds `requiredSigners` — the set of all inner transaction account IDs that differ from the outer account, plus counterparty accounts — and then reconciles that set against the `sfBatchSigners` array.

The reconciliation is bidirectional: each batch signer is removed from `requiredSigners` as it appears; any signer not in `requiredSigners` is an extra unknown signer (`temBAD_SIGNER`). After the loop, `requiredSigners` must be empty — all inner accounts must be accounted for. The outer account is explicitly excluded from `sfBatchSigners` (returning `temBAD_SIGNER` if found there) because its authorization is already captured by the outer transaction signature. Finally, `ctx.tx.checkBatchSign(ctx.rules)` cryptographically verifies the batch signature payload produced by `serializeBatch()` in `protocol/Batch.h`, which serializes the batch flags and the ordered list of inner transaction IDs.

## `checkSign`

Chains `Transactor::checkSign` (validates the outer transaction's own signature or multi-sign set) followed by `Transactor::checkBatchSign` (validates signatures from the `sfBatchSigners` array against the ledger-stored account public keys). Both must pass.

## Logging Convention

All log messages use the `BatchTrace[<parentBatchId>]` prefix, making it straightforward to correlate outer batch events with inner transaction preflight failures in production logs by filtering on a single transaction ID.