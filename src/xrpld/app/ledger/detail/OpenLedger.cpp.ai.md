# `OpenLedger.cpp` — Managing the In-Progress Ledger

## Role in the System

Every XRPL validator node maintains two views of ledger state: the closed, validated history, and the *open ledger* — the mutable accumulator where incoming transactions land before the next consensus round closes them into a new validated ledger. `OpenLedger` is the class that owns that accumulator. It sits between the consensus engine (which calls `accept()` when a round completes) and the transaction-processing layer (which calls `modify()` to inject individual transactions as they arrive from the network or the local queue).

The class wraps `OpenView`, an append-only ledger view that mirrors SLE state from a parent closed ledger, and makes it safely accessible to concurrent readers and writers.

## Two-Mutex Concurrency Design

The single most important architectural decision in this file is the *two-mutex* layout declared in the header:

- `modify_mutex_` — a coarse, write-time lock that serialises all calls to `modify()` and, critically, is also held for the entire transaction-application phase inside `accept()`.
- `current_mutex_` — a fine, brief lock that protects only the `current_` shared pointer itself during swap.

The asymmetry is intentional. Reading `current()` is a single lock-acquire and pointer copy, so it is cheap and can happen at any time from any thread without blocking writers. The `modify_mutex_` does the heavy work: by holding it through the full body of `accept()`, the code ensures that any `modify()` call racing from a network I/O thread will either complete *before* `accept()` begins (and its transaction will be picked up from `current_->txs`) or wait until the new open view is published. This prevents a narrow window where a freshly submitted transaction could be accepted into neither the closing ledger nor the new open ledger.

The short comment in the source — "Block calls to modify, otherwise new tx going into the open ledger would get lost" — is the key invariant.

## The `accept()` Lifecycle

`accept()` is called by the consensus layer (`RCLConsensus`) and by `NetworkOPs` (for out-of-band ledger advances) after a ledger has been built and closed. Its job is to reconstitute the open ledger on top of the new closed state. The sequence is deliberate:

1. **Create a fresh `OpenView`** rooted on the newly closed ledger via `create()`. This wraps the ledger in a `CachedLedger`, which decorates it with an `CachedSLEs` overlay so repeated SLE lookups during transaction application are served from memory rather than the backing store.

2. **Apply retry transactions first, outside the lock.** If `retriesFirst` is true (set when there were disputed transactions in the consensus round), the existing `retries` set is applied against the new view *before* the `modify_mutex_` is acquired. This is safe because `retries` is caller-owned; it runs outside the lock specifically to minimise the critical section.

3. **Acquire `modify_mutex_`, then apply the previous open ledger's transactions.** All transactions from `current_->txs` are forwarded to the new view. Using `boost::adaptors::transform` extracts only the `STTx` from each pair in `current_->txs`, keeping the `apply()` template generic over any forward range of transactions.

4. **Call the optional modifier `f`.** The caller (typically `RCLConsensus`) may supply a `modify_type` lambda to inject additional changes atomically with the view transition, e.g. to insert a fee escalation marker.

5. **Apply local transactions via `TxQ`.** `locals` (an `OrderedTxs` alias for `CanonicalTXSet`) contains transactions originating on this node that haven't been included in a validated ledger yet. They are applied through `TxQ::apply`, which enforces queue ordering and fee logic.

6. **Relay recovered transactions.** After all application is done, `accept()` walks `next->txs` and calls `app.getHashRouter().shouldRelay()` for each. This prevents flooding peers with transactions they already know about. Inner batch transactions (flagged with `tfInnerBatchTxn`) are explicitly skipped — they are sub-transactions of a parent batch and must not be relayed independently. The relay path is marked `LCOV_EXCL_START` because batch support is gated on a feature flag that is not active in test environments, making those branches unreachable in coverage runs.

7. **Atomically publish the new view.** `current_mutex_` is acquired and `current_` is move-assigned from `next`, releasing the old snapshot.

## The `modify()` Copy-on-Write Pattern

`modify()` implements a copy-on-write approach: it copies `current_` into a fresh `OpenView`, invokes the caller's function, and only acquires `current_mutex_` to publish if the function reported changes. This means readers always see a consistent, complete snapshot via `current()` — they never observe a partially-mutated view because any mutation builds on a full copy. The pattern also makes the modification function's semantics straightforward: it receives a mutable `OpenView&`, makes changes, and returns `true` if anything changed.

## `apply_one()` and the Retry Loop

`apply_one()` (defined in `OpenLedger.cpp` though declared in the header alongside the template `apply()`) translates the raw `TER` result from `xrpl::apply()` into the three-way `Result` enum: `success`, `failure`, or `retry`. The mapping is:

- **success**: transaction was applied, or the TxQ queued it (`terQUEUED`).
- **failure**: `tef`/`tem`/`tel` errors — permanently invalid; drop it.
- **retry**: everything else — might succeed later, keep it in the retry set.

The template `apply()` in the header drives the outer retry loop. It makes up to `LEDGER_TOTAL_PASSES` (3) passes over the `retries` set, reducing to non-retry mode after `LEDGER_RETRY_PASSES` (1) pass without progress. The assertion at the end catches the invariant that after the loop ends, either retries is empty or the last pass was not a retry pass — ensuring every transaction was seen in at least one final non-retry pass.

## Debug Helpers

The `debugTxstr()` and three overloads of `debugTostr()` produce short hash prefixes (4 characters) for transaction sets expressed as `OrderedTxs`, `SHAMap`, or `ReadView`. They exist purely for trace-level log messages during development. The `SHAMap` overload includes a `try/catch` around deserialization because `SHAMap` items are raw bytes whose integrity is not guaranteed during debugging.

## Construction and the `CachedLedger` Bridge

The constructor immediately calls `create()` to produce an initial `OpenView`. `create()` wraps the caller's closed ledger in `CachedLedger`, which couples a `CachedSLEs` reference (a node-wide SLE cache) to the ledger. Every SLE access during transaction application therefore hits the node cache before going to the underlying storage, which is critical for throughput since hundreds of transactions may read the same accounts in a single open ledger cycle.