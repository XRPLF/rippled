# `LedgerReplayer.cpp` — Ledger Replay Coordinator

`LedgerReplayer` is the top-level manager for reconstructing historical ledgers from the network. When an XRPL node needs a contiguous range of ledgers it cannot derive locally — for example during consensus recovery, validation, or full-history catch-up — it calls `replay()` here. The class orchestrates a two-phase network acquisition process and owns the lifetime of all sub-tasks involved.

## System Architecture

The replay subsystem has three layers. `LedgerReplayer` sits at the top and coordinates zero or more `LedgerReplayTask` objects, each of which represents a request to rebuild a range of up to 256 consecutive ledgers. Each task in turn depends on two kinds of network sub-tasks:

- **`SkipListAcquire`** — fetches the skip list entry embedded in a ledger's state tree. The skip list is a compact data structure that lets a node walk backwards through ledger history without downloading every header; it names the ancestors of the target ledger that the replayer must acquire in sequence.
- **`LedgerDeltaAcquire`** — fetches the header and the ordered transaction set for one specific ledger so the ledger can be reconstructed by applying those transactions to its parent.

`LedgerReplayer` owns the authoritative collections for both sub-task types via `hash_map<uint256, std::weak_ptr<...>>` maps keyed on ledger hash, plus a `std::vector<std::shared_ptr<LedgerReplayTask>>` for the active tasks.

## `replay()` — Entry Point and Deduplication

When a caller asks to replay `totalNumLedgers` ledgers ending at `finishLedgerHash`, `replay()` performs several guard checks before creating anything:

1. An `XRPL_ASSERT` enforces that the hash is non-zero and that the count is in `(0, MAX_TASK_SIZE]` (currently 256). This is a hard precondition for callers.
2. If the application is shutting down, the request is silently dropped.
3. If the active task count has hit `MAX_TASKS` (10), the request is dropped with a log message. This prevents runaway memory growth during pathological network conditions.
4. The new request's `TaskParameter` is tested against every existing task via `canMergeInto()`. If the requested range is a subset of an already-running task, the new request is discarded — the existing task will cover it.

Only after all four checks pass is a new `LedgerReplayTask` created and appended to `tasks_`.

## Shared `SkipListAcquire` Ownership

Before creating the `SkipListAcquire` for the finish ledger, `replay()` consults `skipLists_` using `weak_ptr::lock()`. If a live `SkipListAcquire` for the same finish hash already exists (because another task is sharing it), the new task reuses that object. Only if the weak pointer is null or expired is a fresh `SkipListAcquire` constructed and the map updated.

This sharing pattern avoids redundant network requests: two concurrent tasks with the same finish ledger hash issue only one round of peer queries for that skip list. The same pattern is applied to `LedgerDeltaAcquire` objects in `createDeltas()`.

The reason for weak pointers rather than shared pointers in the maps is that the maps must not keep sub-tasks alive after all referencing tasks are done. The actual lifetime is held by the `LedgerReplayTask` objects; the maps are a lookup index, not an ownership register.

Initialization ordering is deliberate. `skipList->init(1)` is called before `task->init()`, because if the skip list is already satisfied locally the callback chain fires immediately. Starting the task first could produce a brief window where `createDeltas()` is called before the skip list object is ready.

## `createDeltas()` — Building the Delta Chain

After a `LedgerReplayTask` resolves its skip list, it calls back into `LedgerReplayer::createDeltas()`. This method walks the skip list between `startSeq_` and `finishSeq_`, creating one `LedgerDeltaAcquire` per intermediate ledger. The iterator walk over `skipList_` provides the hashes; `seq` is incremented in lockstep to supply the sequence number to each `LedgerDeltaAcquire` constructor.

A guard checks that the `startHash_` is found in the skip list and that there is at least one entry after it (the first delta). If either condition fails, the method logs an error and returns without creating any deltas — the task will eventually time out rather than produce garbage.

Each `LedgerDeltaAcquire` follows the same weak-pointer reuse pattern as `SkipListAcquire`. Because multiple tasks can overlap in their ledger ranges, two different `LedgerReplayTask` instances may share individual `LedgerDeltaAcquire` sub-tasks. A stub comment in the method notes a future optimization: if the local node already has a validated ancestor within the range, the task could be narrowed to skip acquiring those known ledgers.

## Inbound Data Dispatch: `gotSkipList()` and `gotReplayDelta()`

These two methods are called by the network message handler layer when a peer responds with a `TMProofPathResponse` or `TMReplayDeltaResponse`. Both follow the same pattern: take the ledger hash from the inbound header, look it up in the appropriate weak-pointer map, attempt to lock, erase stale entries on failure, and forward the payload to the live object via `processData()`.

The lock is dropped before calling `processData()`. This avoids holding `mtx_` during the potentially expensive `processData()` callbacks, which may trigger ledger rebuilds or further network requests.

## Concurrency Model

All mutable state — `tasks_`, `skipLists_`, and `deltas_` — is protected by a single `std::mutex mtx_`. The locking discipline is consistent: acquire the lock, manipulate the collections, release it, then call methods on the objects retrieved while the lock was held. This means no lock re-entry is possible from within `processData()` calls back into `LedgerReplayer`.

The destructor acquires `mtx_` before clearing `tasks_`, ensuring that any concurrent `replay()` call that is mid-execution will complete or observe the cleared state. However, `stop()` is more thorough: it explicitly calls `cancel()` on every task and sub-task before clearing the maps, propagating a shutdown signal down the hierarchy so that pending timer callbacks are suppressed.

## `sweep()` — Periodic Cleanup

`sweep()` is called periodically (presumably by the application's maintenance timer). It removes finished tasks from `tasks_` using `std::remove_if`, and prunes stale entries from `skipLists_` and `deltas_` via a small lambda that attempts to lock each weak pointer. Entries that can no longer be locked — because all referencing tasks have released them — are erased. Lock-hold time is measured and logged at debug level, since `sweep()` runs under the same global mutex and a slow sweep could delay inbound data dispatch on other threads.