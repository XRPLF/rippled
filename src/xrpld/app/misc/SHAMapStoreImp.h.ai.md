# SHAMapStoreImp.h — Online Delete and Database Rotation Engine

`SHAMapStoreImp` is the concrete implementation of the `SHAMapStore` interface and serves as the engine behind XRPL's *online delete* feature — the ability to discard ledger history older than a configured threshold while the node continues to operate without interruption. Its header defines the full class layout, two concurrency primitives, a nested SQLite state tracker, and the tunable parameters that govern the deletion lifecycle.

## The Core Problem: Unbounded Ledger Growth

Without intervention, a full-history XRPL node accumulates data indefinitely. `SHAMapStoreImp` solves this by maintaining exactly two on-disk `NodeStore::Backend` instances — a *writable* backend for new writes and an *archive* backend for reads — via `DatabaseRotating`. When enough new ledgers have accumulated, the archive backend (which holds the oldest data) is atomically replaced with a fresh empty backend, and the old archive is discarded. This is the "rotation."

## SavedStateDB: Crash-Safe State Across Restarts

The inner class `SavedStateDB` wraps a `soci::session` (a SQLite connection) and a `std::mutex`. It records which backend paths were active at shutdown (`writableDb`, `archiveDb`) and the last rotated ledger index. This allows the node to resume correctly after a crash: `dbPaths()` reads this state, validates that both backend directories still exist on disk, removes any stale `rippledb.*` directories left by a previously interrupted rotation, and throws a descriptive `std::runtime_error` if the stored paths are inconsistent — which protects against serving corrupt or incomplete data.

The class defaults to a null journal so it compiles without error when online delete is disabled; `init()` is only called when `deleteInterval_` is non-zero.

## Configuration Parameters

All tunable values are read from the `[node_db]` section during construction. `deleteInterval_` sets the minimum number of ledgers to retain (minimum 256 in network mode, 8 in standalone). The constructor enforces `online_delete >= ledger_history` to ensure the node never attempts to retain more SQL history than the NodeStore retains. Additional knobs include:

- `deleteBatch_` (default 100): how many rows to delete per SQL batch to avoid long table locks
- `backOff_` (default 100 ms): pause between SQL batches to yield to other readers
- `ageThreshold_` (default 60 s): minimum age for a ledger before it qualifies for deletion
- `recoveryWaitTime_` (default 5 s): how long to sleep when the node is out-of-sync during a `healthWait()` call

## The Deletion Thread and `run()`

`start()` spawns a dedicated thread only when `deleteInterval_` is set. The thread body in `run()` waits on `cond_` for `onLedgerClosed()` to deliver a newly validated ledger, then evaluates a single condition: `validatedSeq >= lastRotated + deleteInterval_ && canDelete_ >= lastRotated - 1`. If true, it executes the rotation in several ordered phases:

1. **`clearPrior(lastRotated)`** — removes all SQL records (Transactions, AccountTransactions, etc.) for ledger sequences below `lastRotated`, using `clearSql()` which iterates in batches, pausing briefly after each to avoid lock contention.
2. **Snapshot copy** — calls `visitNodes()` on the current validated ledger's state map, invoking `copyNode()` for each node. `copyNode()` calls `fetchNodeObject()` on `dbRotating_`, which promotes the node from the archive backend to the writable backend if it only exists in the archive. Health checks are interleaved every `checkHealthInterval_` (1000) nodes.
3. **`freshenCaches()`** — walks the in-memory ledger and transaction caches, triggering the same promote-on-read for any cached keys, ensuring hot data survives the upcoming rotation.
4. **`makeBackendRotating()`** — creates a fresh, empty backend with a uniquely generated path (`rippledb.XXXX`).
5. **`clearCaches(validatedSeq)`** — drops in-memory caches to prevent stale references.
6. **`dbRotating_->rotate()`** — atomically swaps the new empty backend into the writable slot and demotes the current writable to archive. The callback passed to `rotate()` persists the new state to `SavedStateDB` *inside the lock*, guaranteeing that if the node crashes during rotation the state file always reflects reality.

## Health Gating and `healthWait()`

Every expensive operation in `run()` is bracketed by `healthWait()`, which blocks until the server is either fully synced or stopping. This prevents online delete from proceeding while the node is catching up to the network, which could cause it to discard data it still needs. The return type `HealthResult` is an enum with values `keepGoing` or `stopping`, and callers use `[[nodiscard]]` semantics to force handling at every call site.

## Advisory Delete and Operator Control

When `advisoryDelete_` is enabled, rotation only proceeds if `canDelete_` has been explicitly advanced by an operator via the `can_delete` RPC command. This decouples the automated scheduling from actual deletion, useful for operators who want predictable deletion windows. `setCanDelete()` updates both the in-memory `canDelete_` atomic (used in the hot rotation check) and the persistent `SavedStateDB` record. In non-advisory mode `canDelete_` starts at `std::numeric_limits<LedgerIndex>::max()`, effectively always permitting deletion.

## Synchronization Design

The class uses two condition variables against a single mutex. `cond_` gates the deletion thread waiting for a new closed ledger. `rendezvous_` allows external callers (e.g., during ordered shutdown) to block via `rendezvous()` until the deletion thread has signaled `working_ = false`. The separation avoids a subtle issue: if a single condition variable were used, a caller waiting for "done" could consume the "new ledger" notification intended for the thread. The `working_` atomic provides a fast non-locking path in `rendezvous()` to skip the wait entirely when the thread is already idle.

## `clampFetchDepth()` and `minimumOnline()`

`clampFetchDepth()` ensures the node never fetches more history from peers than it plans to retain — if `deleteInterval_` is 1000, fetching depth beyond 1000 is wasteful. `minimumOnline()` returns the lower bound of ledger history currently stored and is used by the peer-to-peer layer to decline requests for ledgers that have already been deleted, preventing the node from advertising data it can no longer serve.