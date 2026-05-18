# `LedgerReplayer.h` — Top-Level Orchestrator for Ledger Replay

## Role in the System

Ledger replay is the mechanism by which an XRPL node fetches a contiguous range of historical ledgers from its peers by downloading each ledger's header and transactions (the "delta") and re-applying them against the prior state. `LedgerReplayer` is the single top-level manager for this subsystem — it creates, tracks, deduplicates, and tears down all replay work. Callers start a replay by calling `replay()` with a terminal ledger hash and a count; everything below that API is handled internally across a three-tier task hierarchy.

## The `LedgerReplayParameters` Namespace

All hard-coded tuning values live in `LedgerReplayParameters` rather than being scattered as magic numbers. The hierarchy has two timeout tiers: top-level `LedgerReplayTask` objects fire on `TASK_TIMEOUT = 500ms`, while subtasks (`SkipListAcquire` and `LedgerDeltaAcquire`) fire on the shorter `SUB_TASK_TIMEOUT = 250ms`. The asymmetry reflects their different scopes — a task orchestrates many subtasks and warrants more patience, while a subtask does exactly one network fetch.

The `TASK_MAX_TIMEOUTS_MULTIPLIER` formula — `max(10, N * 2)` — scales allowed timeouts with range size, preventing large replays from being cancelled too aggressively while bounding the wait for small ones. `MAX_TASKS = 10` and `MAX_TASK_SIZE = 256` are hard caps that prevent unbounded queue growth. A fallback path exists when peers don't support the replay feature: after `MAX_NO_FEATURE_PEER_COUNT = 2` non-supporting peers are encountered, subtasks switch to `SUB_TASK_FALLBACK_TIMEOUT = 1000ms` and retry via the legacy acquisition path.

## Task Hierarchy

`LedgerReplayer` owns the top level. Each `LedgerReplayTask` represents a request to replay a range of up to 256 ledgers and holds two kinds of subtask:

- `SkipListAcquire` — fetches the ancestor-hash list embedded in the finish ledger, which provides the exact hashes of all ledgers in the range.
- `LedgerDeltaAcquire` — fetches one ledger's header and transactions from peers. Created by `createDeltas()` after the skip list is known.

All three types extend `TimeoutCounter`, which drives an asynchronous retry loop via Boost.Asio timers and a `JobQueue`, calling `onTimer()` on each expiry.

## Ownership and Deduplication via `weak_ptr`

The most architecturally notable design in `LedgerReplayer` is how it holds subtasks. `tasks_` holds strong `shared_ptr<LedgerReplayTask>` objects, but `skipLists_` and `deltas_` are `hash_map<uint256, weak_ptr<...>>`. Each subtask is owned by the task (or tasks) that depend on it; `LedgerReplayer` merely tracks them for routing incoming network data.

This serves two purposes simultaneously. First, deduplication: `replay()` checks the `skipLists_` map before creating a new `SkipListAcquire`, and `createDeltas()` does the same for each `LedgerDeltaAcquire`. If another task already has an in-flight request for the same hash, the new task reuses the existing subtask — two tasks covering overlapping ledger ranges share the same delta fetches automatically. Second, automatic cleanup: when all tasks referencing a subtask are destroyed, the subtask destructs without `LedgerReplayer` needing to explicitly track lifetimes. `sweep()` later purges the dead `weak_ptr` entries from the maps.

The `replay()` method also checks whether a new request can be merged entirely into an existing `LedgerReplayTask` via `canMergeInto()`. If the new range is a sub-range of an existing task's range, the new request is silently dropped, preventing duplicate top-level tasks.

## Locking Strategy and Deadlock Prevention

`LedgerReplayer` uses a single `std::mutex mtx_` to protect `tasks_`, `skipLists_`, and `deltas_`. Subtasks each have their own internal `recursive_mutex` (from `TimeoutCounter`). The protocol in `gotSkipList()` and `gotReplayDelta()` is deliberate: acquire `mtx_`, look up the subtask, promote the `weak_ptr` to a `shared_ptr`, then **release** `mtx_` before calling `processData()` on the subtask. This lock-before-copy-then-release pattern ensures the outer lock is never held when the inner subtask lock is acquired, preventing inversion deadlocks between the two mutex levels.

## Lifecycle Methods

`stop()` cancels all live tasks and subtasks by calling `cancel()` on each, then clears all three collections. `cancel()` on a `TimeoutCounter` marks it done without forcing immediate cancellation of queued timer callbacks — those callbacks simply exit early when they see `isDone()`. `sweep()` is called periodically (from an external maintenance path) to remove finished tasks and dangling `weak_ptr` entries; it measures and logs the time spent under `mtx_` to detect contention.

The `test::LedgerReplayClient` friend class declared in both `LedgerReplayer` and its subtask classes grants white-box access to internal state for unit tests, keeping the production API clean without requiring extra accessors.