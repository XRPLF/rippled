# `SHAMapStoreImp.cpp` — Online Ledger Deletion and Backend Rotation

## Purpose and Role

`SHAMapStoreImp` is the concrete implementation of the `SHAMapStore` interface and owns the entire lifecycle of the XRPL node's persistent ledger object store. Its central responsibility is **online deletion**: continuously and safely purging old ledger data from a running node without stopping it, ensuring that disk usage stays bounded over the lifetime of a validator or full-history node.

The file implements a background thread that watches for newly validated ledgers and — when enough history has accumulated — rotates the underlying node store backends, deletes old SQL rows, and invalidates stale caches, all while the node continues to process new ledgers.

## Architecture: Two-Backend Rotation

The core design insight is that the node store uses **two physical backends** simultaneously: a `writableBackend_` that accepts new writes, and an `archiveBackend_` that holds older data still potentially needed for reads. During a rotation:

1. The current writable backend becomes the new archive.
2. The old archive is scheduled for deletion.
3. A fresh empty backend becomes the new writable.

This swap is performed atomically inside `DatabaseRotatingImp::rotate()`, which holds a mutex across the pointer reassignments and only calls the state-persistence callback after the swap, ensuring the saved state always reflects a valid configuration. The result is that the node never needs to stop to reclaim disk space, and reads transparently fall through to either backend.

## `SavedStateDB`: Crash-Safe State Persistence

The inner class `SavedStateDB` wraps a small SQLite database (named `"state"`) that records three things across process restarts: the paths of the writable and archive backends on disk, and the ledger sequence at which the last rotation occurred. Every operation acquires a `std::mutex` lock, making this safe to call from both the deletion thread and any external RPC handler.

The reason for a dedicated SQLite record rather than just reconstructing state from the filesystem is crash safety. If the process dies mid-rotation, the stored names tell `dbPaths()` exactly which directories are valid. `dbPaths()` runs during construction and cross-checks the recorded paths against what actually exists in the filesystem directory. If only one backend is present when two are expected — or one is missing entirely — the constructor throws `std::runtime_error` with a detailed recovery message rather than silently starting in an inconsistent state. It also handles the case where the operator has moved their database to a new path, updating the parent-directory component while preserving the backend's filename.

## The Deletion Thread: `run()`

`start()` spawns a single background thread running `run()` if `deleteInterval_` is non-zero. The thread waits on a condition variable for notifications from `onLedgerClosed()`, which is called by `LedgerMaster` every time a ledger validates. The signal passes the validated ledger through `newLedger_` protected by a mutex, and the thread extracts it at wakeup.

The rotation condition (`readyToRotate`) requires two things: the validated sequence must be at least `deleteInterval_` ledgers past the last rotation point, and `canDelete_` must be at least `lastRotated - 1`. The second condition enables **advisory delete** — when configured, an external RPC call must explicitly advance `canDelete_` to authorize any deletion, giving operators manual control. Without advisory delete, `canDelete_` starts at `std::numeric_limits<LedgerIndex>::max()`, meaning the interval alone gates rotation.

When `readyToRotate` is true, the thread executes a multi-phase sequence:

1. **`clearPrior()`** sets `minimumOnline_` to block peer-to-peer fetching of ledgers about to be deleted, clears in-memory ledger objects, then deletes rows from the `Ledgers`, `Transactions`, and `AccountTransactions` SQLite tables via `clearSql()`.
2. **Node copy**: The validated ledger's state SHAMap is snapshot-traversed via `visitNodes`, and each node is re-fetched through `copyNode()` with `duplicate=true`. This re-inserts live objects into the writable backend before the old archive disappears.
3. **`freshenCaches()`**: The tree-node cache and transaction cache hold keys whose backing storage is in the soon-to-be-discarded archive. By re-fetching each key with `duplicate=true`, the objects are migrated into the writable backend.
4. A new empty backend is created via `makeBackendRotating()`.
5. **`clearCaches()`** flushes `LedgerMaster`'s prior-ledger cache and resets the `FullBelowCache` — critically, its generation counter must be bumped to prevent stale "full below" markers from causing incorrect assumptions about SHAMap completeness after the rotation.
6. **`dbRotating_->rotate()`** performs the atomic backend swap and, inside the callback, writes the new `{writableDb, archiveDb, lastRotated}` triple to `SavedStateDB`.

Health checks gate every major phase: `healthWait()` is called before and after each step and blocks the deletion thread if the node is not in `FULL` operating mode or if the last validated ledger is older than `ageThreshold_` (default 60 seconds). The thread sleeps for `recoveryWaitTime_` (default 5 seconds) between re-checks, logging a warning on each iteration. If `stop_` is set during any wait, `healthWait()` returns `stopping` and the caller unwinds. The inner `copyNode()` and `freshenCache()` loops also call `healthWait()` every `checkHealthInterval_` (1000) nodes, so even a copy of a large ledger can be interrupted cleanly.

## `clearSql()`: Batched, Interruptible SQL Cleanup

Deleting large ranges of rows in a single SQL transaction would lock the database and starve concurrent readers (e.g., RPC queries). `clearSql()` instead deletes in configurable batches (`deleteBatch_`, default 100 rows) and sleeps for `backOff_` milliseconds (default 100ms) between batches. It also checks `healthWait()` between batches, allowing the operation to be aborted safely if the node loses sync. The same pattern applies to all three tables (`Ledgers`, `Transactions`, `AccountTransactions`), and transaction tables are skipped entirely if `useTxTables()` is false.

## `rendezvous()`: External Synchronization

The `rendezvous()` method allows external callers to block until the deletion thread has become idle. The `working_` atomic flag is set to `true` when `onLedgerClosed` delivers a new ledger, and the thread clears it and notifies `rendezvous_` at the top of each idle loop. This is used during graceful shutdown and in tests that need to assert on post-rotation state.

## `makeBackendRotating()` and Backend Naming

New backends are created under the configured `path` directory with a `rippledb.XXXX` name, where `XXXX` is a random four-character suffix from `boost::filesystem::unique_path`. When a specific path is provided (on startup, from saved state), that path is used directly. After construction, `dbPaths()` removes any directories in the node store path that match the `rippledb` prefix but are neither the active writable nor archive backend — cleaning up after crash-interrupted rotations from previous runs.

## RocksDB Defaults

The constructor silently injects `cache_mb` and `filter_bits` defaults when the backend type is RocksDB and these keys are absent. The `filter_bits = 10` Bloom filter default is only applied for `NODE_SIZE >= 2` (medium and large node configurations), where the memory cost is justified by read-performance gains on large datasets.

## Concurrency Model

The deletion thread, the main application thread (which calls `onLedgerClosed`), and potential RPC threads (which call `setCanDelete` and `minimumOnline`) all share state. The design uses a layered concurrency strategy: `mutex_` + `cond_` for ledger delivery and `working_`/`stop_` signaling; `SavedStateDB::mutex_` for SQLite access; `DatabaseRotatingImp::mutex_` for backend pointer swaps; and `std::atomic<LedgerIndex>` for `minimumOnline_` and `canDelete_`, which are read on hot paths without acquiring a lock.