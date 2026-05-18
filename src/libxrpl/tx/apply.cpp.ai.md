# `src/libxrpl/tx/apply.cpp`

## Role in the System

This file is the top-level transaction application coordinator for the XRPL. It bridges two orthogonal concerns: the network-level validity cache (tracked by `HashRouter`) and the stateless-then-stateful application pipeline (`preflight → preclaim → doApply`) defined in `applySteps.cpp`. Where `applySteps.cpp` contains the mechanics of each pipeline stage, `apply.cpp` decides *when* to run them, *how* to short-circuit redundant work, and *how* to handle the multi-transaction semantics of the Batch feature.

## Validity Caching via `HashRouter`

`checkValidity()` is the gateway that decides whether a transaction is safe to propagate over the P2P network and ultimately apply to a ledger. Cryptographic signature verification and local-field checks are intentionally expensive, so their results are memoized in the node's `HashRouter` — a time-bounded hash table that tracks metadata about every recently seen object by its 256-bit hash.

The file claims four of the router's six private flag bits (`PRIVATE1`–`PRIVATE4`), aliased as `SF_SIGBAD`, `SF_SIGGOOD`, `SF_LOCALBAD`, and `SF_LOCALGOOD`. The naming makes the semantics self-documenting: a transaction moves through a linear validity state machine from unknown → `SigBad`/`SigGoodOnly`/`Valid`. The check logic is carefully ordered — bad signature short-circuits immediately without running local checks, because there is no point inspecting well-formedness of a transaction whose author cannot be authenticated. This avoids a class of CPU-exhaustion attacks where an attacker sends malformed-but-locally-valid transactions with invalid signatures.

The returned `Validity` enum (`SigBad`, `SigGoodOnly`, `Valid`) maps directly to what the P2P layer can do with the transaction: a bad signature means the transaction should not be forwarded; a good signature with failed local checks means it may be relayed (the signature vouches for authenticity) but will never be applied; `Valid` means it can be both relayed and applied.

## `forceValidity()` — Elevating Cached State

`forceValidity()` lets callers forcibly promote a transaction's cached validity, bypassing the actual checks. It uses a `[[fallthrough]]` switch to compose flags: `Valid` sets both `SF_LOCALGOOD` and `SF_SIGGOOD`; `SigGoodOnly` sets only `SF_SIGGOOD`. Crucially, it can never set `SF_SIGBAD` — calling `forceValidity(router, txid, Validity::SigBad)` is a no-op. The design intentionally allows raising validity (e.g., for locally-submitted transactions where the node is authoritative) but never lowering it through this path. The header's comment warns "Use with extreme care," since bypassing signature verification in the cache means every subsequent `checkValidity` call on the same hash will skip the cryptographic check entirely until the cache entry expires.

## The `apply()` Pipeline

The private template `apply(ServiceRegistry&, OpenView&, PreflightChecks&&)` is the canonical implementation. It accepts any callable that produces a `PreflightResult`, runs `preclaim` on that result against the open view, then calls `doApply`. Wrapping the preflight step in a callable — rather than accepting a `PreflightResult` directly — is a key design choice: `preflight` results can be safely computed in advance and on a different thread (they contain no ledger references), but `preclaim` and `doApply` must run together against the same view. The template keeps this sequencing enforced while allowing both the standard case (plain `preflight`) and the batch-inner case (batch `preflight` with a `parentBatchId`) to share identical post-preflight logic.

The `NumberSO` RAII guard is installed here, before `preclaim` runs. It configures the thread-local fixed-point math precision mode based on whether the `fixUniversalNumber` amendment is active in the current view's rules. This mirrors similar setup in `applySteps.cpp`'s `with_txn_type()`, which adds `fixUniversalNumber` to `Transactor::operator()` for the actual execution phase.

## Batch Transaction Execution

`applyBatchTransactions()` implements the Batch feature's inner-transaction execution loop. The outer `ttBATCH` transaction has already been applied by the time this function is called (via `applyTransaction()`), meaning the outer account's sequence number and fee have already been committed. The inner transactions are then executed against a layered view stack:

1. **`wholeBatchView`** — an `OpenView` wrapping the outer ledger view, created with the `batch_view` tag. Changes here are all-or-nothing relative to the outer view.
2. **`perTxBatchView`** — an `OpenView` wrapping `wholeBatchView`, created fresh per inner transaction. If the inner transaction succeeds, its changes are promoted to `wholeBatchView` via `perTxBatchView.apply(batchView)`. If it fails, the sub-view is simply abandoned.

This two-level nesting isolates each inner transaction's tentative state changes. Only after the entire batch loop completes successfully does `wholeBatchView.apply(view)` promote the aggregate changes to the actual ledger.

The three Batch execution modes are enforced by examining the outer transaction's flags:
- **`tfAllOrNothing`** — any inner failure causes `applyBatchTransactions()` to return `false`, and none of the changes reach the outer view.
- **`tfUntilFailure`** — iteration stops at the first failure; all previously applied inner transactions are kept.
- **`tfOnlyOne`** — stops after the first *success*; subsequent transactions are not executed.

The `applied` count guards the return value: even in `tfUntilFailure` mode, the function returns `false` if no inner transaction was ever applied. This ensures `wholeBatchView.apply(view)` is only called when there is something worth committing.

## Batch Inner Signature Handling and `fixBatchInnerSigs`

Inner batch transactions are signed as unsigned objects — they carry no `sfTxnSignature`, no `sfSigners`, and an empty `sfSigningPubKey`. Their authorization comes entirely from the outer transaction's signature. `checkValidity()` detects this via `tfInnerBatchTxn` and `featureBatch`, then applies a defensive check: if any of those signature fields are *present*, it returns `SigBad` with a malformation error.

The `fixBatchInnerSigs` amendment addresses a subtle correctness bug in the original Batch implementation. Before the fix, when an inner transaction reached `checkValidity()`, the code would still run `passesLocalChecks()` and then record `SF_SIGGOOD` — implying a valid signature on a transaction that has none. The `fixBatchInnerSigs` block (`neverValid` path) corrects this: once the amendment is enabled, any inner-batch transaction is never assigned a good-signature cache entry; the code returns immediately after the defensive field check. The comment in the source explicitly notes this block "should probably have never been included in the original `Batch` implementation," making the amendment a targeted retroactive fix rather than a feature expansion.

## `applyTransaction()` — The Ledger-Layer Interface

`applyTransaction()` is the highest-level entry point, used by the ledger consensus and open-ledger building machinery. It adds `tapRETRY` to the flags when `retryAssured` is true (signaling to the `Transactor` that a `tec` result can be soft-failed and re-tried in the same ledger cycle). After a successful `apply()` call, it checks whether the applied transaction was a `ttBATCH` and, if so, runs `applyBatchTransactions()` in the same call frame. This placement is intentional: the batch's inner transactions must run immediately after the outer transaction is committed, within the same ledger view and under the same error-handling umbrella.

The function's return type, `ApplyTransactionResult` (`Success`, `Fail`, `Retry`), collapses the full `TER` space into a three-way decision for callers that only care about scheduling:
- `tef`/`tem`/`tel` codes — hard failures, no retry.
- Applied transactions — always `Success`.
- Anything else — `Retry`, meaning the transaction remains a candidate for the open ledger.

All execution is wrapped in a `try`/`catch(std::exception const&)` that converts exceptions into `Fail`, matching the guarantee documented in `apply.h` that `applyTransaction()` does not throw.