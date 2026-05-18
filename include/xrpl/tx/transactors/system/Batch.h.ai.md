# `include/xrpl/tx/transactors/system/Batch.h`

## Role and Purpose

`Batch` is the transactor implementing the `ttBATCH` transaction type, which allows a submitter to bundle two to eight inner XRPL transactions into a single outer transaction with a configurable execution policy. It solves the composability problem on XRPL: operations that logically belong together — such as a DEX offer paired with a trust line establishment — can be submitted atomically, avoiding the race conditions and ordering problems that arise from submitting them as independent transactions.

The header defines the class interface; the implementation lives in `src/libxrpl/tx/transactors/system/Batch.cpp`.

## Inheritance and the Transactor Framework

`Batch` inherits from `Transactor`, which uses compile-time polymorphism rather than virtual dispatch for its pipeline stages. `invokePreflight<T>` in the base class calls `T::getFlagsMask`, `T::preflight`, then `preflight2`, then `T::preflightSigValidated` in sequence — relying on name hiding rather than virtual functions. `Batch` overrides all of these static hooks, plus `checkSign` (in preclaim) and `doApply` (the only true virtual method).

`ConsequencesFactory` is set to `Normal`, meaning the transaction does not unconditionally block a queue slot the way an `AccountDelete` does, and the base class consequence analysis suffices.

## Fee Calculation: `calculateBaseFee`

The fee model for Batch is deliberately additive and more complex than a standard transactor. The total base fee is:

```
batchBase (view.fees().base + own base fee)
  + sum of each inner transaction's base fee
  + (number of batch signatures × view.fees().base)
```

Charging each inner transaction's fee prevents Batch from being a cost-avoidance mechanism relative to submitting the transactions individually. Charging per batch-signer signature mirrors the multi-sign fee structure used elsewhere. The implementation performs explicit overflow checks at every accumulation step, returning `INITIAL_XRP` as a sentinel on overflow rather than throwing — a defensive pattern used throughout the XRPL fee pipeline.

## Flag Semantics: `getFlagsMask`

`getFlagsMask` returns `tfBatchMask`, which admits exactly four mutually exclusive execution policy flags and rejects `tfInnerBatchTxn` on the outer transaction (only inner transactions carry that flag). The four policies, enforced in `applyBatchTransactions()` in `apply.cpp`, are:

- **`tfAllOrNothing`** — any inner failure aborts all; the batch either fully commits or fully rolls back.
- **`tfOnlyOne`** — stops at the first success; subsequent transactions are not executed.
- **`tfUntilFailure`** — processes transactions in order, stops on the first failure, commits all prior successes.
- **`tfIndependent`** — all transactions run regardless of individual failures; successful ones commit.

`preflight` enforces that exactly one of these flags is set using `std::popcount`, rejecting any combination.

## Preflight: `preflight`

`preflight` is the structural integrity check run before signature verification. It validates:

1. Exactly one execution policy flag is present.
2. The `sfRawTransactions` array contains at least two and no more than `maxBatchTxCount` (8) entries.
3. For each inner transaction: it must not be a `ttBATCH` itself (no nesting), must not be a disabled transaction type (see `disabledTxTypes`), must carry `tfInnerBatchTxn`, must have an empty `sfSigningPubKey` and no `sfTxnSignature` or `sfSigners`, must have a zero fee in XRP, and must pass its own `preflight` call with the `tapBATCH` flag set (passing the outer batch's transaction ID as `parentBatchId`).
4. Each inner transaction must have either a non-zero `sfSequence` or an `sfTicketSequence`, but not both.
5. For `tfAllOrNothing` and `tfUntilFailure` modes, duplicate sequence or ticket values across inner transactions from the same account are rejected at this stage — since those modes commit or abort as a unit, two inner transactions consuming the same account slot would be incoherent.

The inner `preflight` calls are recursive invocations of the ledger's top-level `xrpl::preflight()`, which routes through `invokePreflight` for the appropriate inner transaction type. This ensures each inner transaction is individually well-formed.

## Post-Signature Validation: `preflightSigValidated`

This stage runs after the outer transaction's own signature has been cryptographically verified, so it has a degree of trust that the submitter is who they claim to be. It builds the set of accounts that must additionally sign the batch (`requiredSigners`): every inner transaction account that differs from the outer account, plus any `sfCounterparty` fields that differ from the outer account.

It then validates `sfBatchSigners` against this set with a double-bookkeeping pass: every batch signer must match an entry in `requiredSigners` (no extraneous signers), and every required signer must appear in the batch signers array (no missing signers). Duplicates are rejected, and the outer account may not appear in `sfBatchSigners` since its authorization is conveyed by the outer signature itself. Finally, `ctx.tx.checkBatchSign()` verifies the cryptographic signatures of all batch signers.

Separating this validation into `preflightSigValidated` rather than `preflight` is a deliberate design choice dictated by the framework: the framework only calls this hook after the outer signature has been validated, which is the right place to verify the cryptographic signatures of other parties.

## Signature Checking: `checkSign`

`Batch::checkSign` overrides `Transactor::checkSign` at the preclaim stage to run two checks: the standard outer-account signature via `Transactor::checkSign` and then `Transactor::checkBatchSign` which re-validates batch signer credentials against on-ledger account state. The separation from `preflightSigValidated` reflects the preclaim/preflight pipeline: preclaim has access to the ledger view and can check whether signing keys are authorized by on-ledger `RegularKey` or `SignerList` objects.

## Apply: `doApply`

`doApply()` returns `tesSUCCESS` immediately. The outer Batch transaction itself writes nothing directly to the ledger beyond its fee deduction and sequence increment (handled by the base class `apply()` method). The inner transaction execution occurs in `applyBatchTransactions()` inside `apply.cpp`, called by `applyTransaction()` only after the outer apply succeeds:

```cpp
if (isTesSuccess(result.ter) && txn.getTxnType() == ttBATCH)
{
    OpenView wholeBatchView(batch_view, view);
    if (applyBatchTransactions(registry, wholeBatchView, txn, j))
        wholeBatchView.apply(view);
}
```

Each inner transaction runs in its own `perTxBatchView` sandbox. On success, its changes are merged into `wholeBatchView`. Only if at least one inner transaction applied does `wholeBatchView` get merged into the authoritative ledger view. This two-level view isolation gives each inner transaction a clean snapshot to operate against while still seeing the cumulative effect of prior inner transactions in the batch.

## Disabled Transaction Types

The `disabledTxTypes` array is a `constexpr` compile-time list of `TxType` values that are forbidden as inner transactions. It currently blocks all Vault operations (`ttVAULT_*`), Loan Broker operations (`ttLOAN_BROKER_*`), and Loan operations (`ttLOAN_*`). These transaction families involve complex multi-ledger-object state transitions or are too new to have been validated in the batch execution context. The `preflight` implementation checks this array with `std::any_of` and returns `temINVALID_INNER_BATCH` on a match, giving the caller a specific error distinct from the general malformation errors.