# `LedgerCleaner.h` — Interface for Ledger/Transaction Database Continuity Maintenance

## Role in the System

The XRP Ledger node maintains two distinct persistence layers: a key-value node store (SHAMap nodes) and SQLite-backed relational databases for account and transaction history. Over time — through crashes, incomplete syncs, or bugs in older software versions — these stores can develop inconsistencies: missing SHAMap nodes, SQL rows referencing wrong ledger hashes, or gaps in ledger history. `LedgerCleaner.h` defines the abstract interface for a dedicated background service that detects and repairs these inconsistencies without blocking normal ledger operation.

## Interface Design and Inheritance

`LedgerCleaner` inherits from `beast::PropertyStream::Source`, registered under the fixed name `"ledgercleaner"`. This is a recurring XRPL pattern: subsystems that need operational visibility inherit from `PropertyStream::Source` so they can expose runtime state through the server's diagnostic introspection system — the `get_counts` RPC command and similar tools — with no additional wiring at the call site. The `onWrite()` implementation in `LedgerCleanerImp` publishes current `status`, `min_ledger`, `max_ledger`, `check_nodes`, `fix_txns`, and a `fail_counts` counter under the lock, giving operators a live view of cleaning progress.

The three pure virtual methods — `start()`, `stop()`, and `clean()` — model an explicit lifecycle. The concrete `LedgerCleanerImp` creates a single dedicated `std::thread` named `"LedgerCleaner"` in `start()`, blocks it on a `std::condition_variable`, and tears it down gracefully in `stop()` by setting `shouldExit_` and joining the thread. The destructor asserts that `stop()` was called before destruction, catching misuse at runtime via `LogicError`.

## Non-blocking `clean()` and JSON Parameters

`clean()` is documented as non-blocking: it configures the cleaning task and signals the worker thread via `wakeup_.notify_one()` but returns immediately. This is essential because cleaning can involve scanning hundreds of thousands of ledger sequences with I/O-intensive SHAMap walks and database comparisons — invoking that work synchronously on any consensus or RPC thread would be catastrophic.

The method accepts a `Json::Value` rather than a typed parameter struct. This reflects the standard XRPL pattern for admin commands: the JSON arrives from the RPC layer with minimal transformation, and the implementation interprets well-known keys (`ledger`, `min_ledger`, `max_ledger`, `full`, `fix_txns`, `check_nodes`, `stop`) directly from the payload. A single-ledger shortcut exists: providing just `"ledger"` forces both `fixTxns_` and `checkNodes_` true, since a single-ledger repair is likely being triggered precisely because that ledger is known to be broken. The `"stop"` key sets both range bounds to zero, causing the worker loop to return after its current unit of work.

The RPC handler in `rpc/handlers/admin/data/LedgerCleaner.cpp` is a trivial one-liner: it calls `context.app.getLedgerCleaner().clean(context.params)` and returns a status string. The interface absorbs the full JSON without further parsing at the handler layer.

## The Worker Loop and Concurrency Model

The worker thread in `doLedgerCleaner()` iterates from `maxRange_` down to `minRange_`, processing one ledger per iteration. Before each ledger it checks `app_.getFeeTrack().isLoadedLocal()` — if the server is under high load, the cleaner backs off with a five-second sleep. On each successful ledger, it pauses 100 ms to reduce I/O pressure. On failure (hash not found, or `doLedger()` returns false) it sleeps two seconds to allow `InboundLedgers` to make progress before retrying the same ledger. This adaptive pacing prevents the cleaner from competing with consensus or network I/O during busy periods.

All shared state (`minRange_`, `maxRange_`, `checkNodes_`, `fixTxns_`, `failures_`, `state_`, `shouldExit_`) is guarded by `mutex_`. The worker acquires the lock to snapshot parameters at the start of each iteration, then releases it before doing any I/O work, keeping the critical section narrow.

## What `doLedger()` Actually Checks

The per-ledger cleaning logic in `doLedger()` performs three distinct validations:

1. **SQL database consistency**: It loads the ledger from the SQL index via `loadByIndex()` and compares the resulting header hash and parent hash against the authoritative node-store version. Any mismatch sets `doTxns = true`, triggering a rewrite via `pendSaveValidated()`.

2. **History index integrity**: `LedgerMaster::fixIndex()` verifies the ledger's entry in the history table and corrects it if wrong.

3. **SHAMap node completeness** (when `checkNodes_` is set): `nodeLedger->walkLedger()` traverses every node in the ledger's state and transaction trees. If any node is missing, the ledger is evicted from the master cache via `clearLedger()` and re-acquired via `InboundLedgers::acquire()`.

To resolve the hash of a target ledger, `getHash()` walks backward through the reference ledger's skip list using `hashOfSeq()`. If the target is too far back for the current validated ledger's skip list to reach directly, it uses `getCandidateLedger()` to find an intermediate ledger that holds the needed hash, acquires that ledger first, then looks up the target. This handles arbitrarily deep history while minimizing node-store lookups.

## Factory and Ownership

`make_LedgerCleaner()` is the sole construction path. It takes `Application&` — the top-level context providing access to `LedgerMaster`, `InboundLedgers`, `FeeTrack`, and the journal system — and returns a `std::unique_ptr<LedgerCleaner>`. This factory pattern keeps the concrete `LedgerCleanerImp` class entirely invisible to callers and confines the heavy include surface (`LedgerMaster.h`, `InboundLedgers.h`, `LedgerPersistence.h`) to the implementation translation unit.