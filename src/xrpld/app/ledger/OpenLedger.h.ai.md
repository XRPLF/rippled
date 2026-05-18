# `OpenLedger.h` — The Pending Ledger State Machine

## Role in the System

Every ledger in XRPL passes through two phases: open (pending) and closed (validated). `OpenLedger` is the authoritative owner of the open phase — the accumulating ledger where new transactions land before a consensus round seals them into a closed, immutable ledger. It acts as the live transaction staging area that the rest of the application reads and writes concurrently during normal operation, and that the consensus engine replaces atomically when a new ledger is accepted.

The class is not copyable or default-constructible, reflecting its role as a unique, shared resource within `Application`. It is constructed from the most recently closed ledger and holds its state as an `OpenView const` shared pointer — a read-only snapshot that any thread can safely hold.

## Core Data Model

The internal state is deceptively simple: a single `std::shared_ptr<OpenView const>` named `current_` holds the entire open ledger at any moment. `OpenView` (from `include/xrpl/ledger/OpenView.h`) is a writable accumulator that layers state changes over a read-only base — in this case a `CachedLedger` wrapping the last closed ledger. Reads are cheap because they snapshot the pointer and share the immutable object; writes always produce a new `OpenView` copy and swap it in.

`CachedSLEs&` is held by reference and passed into `create()` when constructing new `CachedLedger` wrappers. It prevents repeated deserialization of the same ledger state entries across view copies, which is critical for throughput given how frequently modifications occur.

`OrderedTxs` is a type alias for `CanonicalTXSet`, a deterministically sorted map keyed on `(salted-account, seqProxy, txId)`. The salt comes from the parent ledger's hash, which prevents adversaries from mining account IDs to manipulate the sort order. This ordering is what makes transaction replay deterministic across nodes during consensus.

## Two-Mutex Concurrency Design

The class maintains two mutexes with a strict acquisition order:

- `modify_mutex_` serializes all mutation operations — both `modify()` and the write phase of `accept()`.
- `current_mutex_` protects only the pointer swap into `current_`.

This split exists because `current()` must be as cheap as possible — it's called from many read paths (RPC handlers, signing utilities, TxQ checks). Under this design, `current()` grabs only `current_mutex_`, loads the `shared_ptr`, and returns. The heavy work of building a new view happens under `modify_mutex_` alone, and the final pointer publish is a minimal critical section under `current_mutex_`.

`modify()` illustrates the pattern: it acquires `modify_mutex_`, copy-constructs a new `OpenView` from `*current_`, calls the user-supplied functor on it, and if the functor returns `true`, acquires `current_mutex_` to swap in the new view. The functor never touches the lock, and the outer `modify_mutex_` prevents two concurrent modifications from racing on their copies.

## The `accept()` Transition

`accept()` is the most complex operation — it drives the ledger close sequence. Its logic, after acquiring `modify_mutex_` to block concurrent `modify()` calls, proceeds in layers:

1. **Retries first (optional):** If `retriesFirst` is true, the previously-collected set of retriable transactions is re-applied to the new open view *before* acquiring `modify_mutex_`. This handles disputed transactions that need an early shot at the fresh ledger without blocking new transaction ingestion.

2. **Current open transactions:** All transactions in the outgoing `current_` view are applied to the new view via the `apply()` template. Any that fail transiently go into the `retries` output set for the caller to manage.

3. **Modify callback:** The optional `modify_type` functor `f` is called (still under `modify_mutex_`) to perform additional modifications — in practice this is where `TxQ` injects queued transactions.

4. **Local transactions:** The `locals` set is fed through `app.getTxQ().apply()` one by one.

5. **Relay recovered transactions:** For every transaction that made it into the new open view, `accept()` consults `HashRouter::shouldRelay()` and, if appropriate, serializes the transaction and broadcasts it via the overlay. Inner batch transactions (flagged `tfInnerBatchTxn`) are skipped here since they should not be independently relayed.

6. **Atomic publish:** `current_mutex_` is acquired and `current_` is replaced with the new view.

The careful ordering — retries outside the lock, then `modify_mutex_` to block new submissions, then `current_mutex_` only for the pointer swap — ensures that no transaction submitted concurrently via `modify()` is silently lost during ledger close.

## Transaction Application and Retry Logic

The `apply()` function template (defined inline in the header) implements a multi-pass retry algorithm. The constants `LEDGER_TOTAL_PASSES = 3` and `LEDGER_RETRY_PASSES = 1` govern its behavior:

- In the first pass, every candidate transaction is attempted with `retry = true`. Transactions returning `Result::retry` (transient failures such as sequence gaps or insufficient reserves that might resolve once other transactions apply) go into the `retries` set.
- Subsequent passes re-attempt retries. A pass switches out of retry mode once it stops making progress (`changes == 0`) or after `LEDGER_RETRY_PASSES` additional retry-enabled passes.
- A final non-retry pass is always guaranteed, ensuring that no transaction sits in the set without at least one definitive attempt.

`apply_one()` maps `xrpl::apply()` results to the three-valued `Result` enum. Applied transactions and those sent to the queue (`terQUEUED`) are `success`. Transactions with `tef*`, `tem*`, or `tel*` codes are `failure` (discarded permanently). Everything else is `retry`.

The template accepts any forward range via `FwdRange`, and the comment "Dereferencing the iterator can throw since it may be transformed" explains why each iteration is wrapped in a `try/catch`: the `boost::adaptors::transform` range used in `accept()` lazily extracts the transaction pointer from the `txs` pair, and that transformation can throw.

## Debug Utilities

The four `debugTostr()` / `debugTxstr()` free functions provide abbreviated transaction-set representations for log output. Each truncates the transaction ID to four hex characters — enough to visually distinguish transactions in trace logs without the noise of full hashes. These are intentionally not part of the `OpenLedger` class since they operate on `OrderedTxs`, `SHAMap`, `ReadView`, and `STTx` types directly.