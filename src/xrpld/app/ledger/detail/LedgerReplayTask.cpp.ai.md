# `LedgerReplayTask.cpp` — Orchestrating Sequential Ledger Range Replay

## Role in the System

`LedgerReplayTask` is the top-level orchestrator for replaying a contiguous range of historical ledgers from the network. When the XRPL node needs to catch up on a sequence of ledgers it never validated — for example during a gap in consensus — it instantiates a `LedgerReplayTask` to coordinate the work. The task is not itself a peer-communication primitive; rather, it delegates to two lower-level subtask types: `SkipListAcquire` (to determine the exact ledger hashes in the range) and `LedgerDeltaAcquire` (to fetch each individual ledger's header and transactions from peers). This file implements both the `TaskParameter` nested class and the `LedgerReplayTask` lifecycle.

## Two-Phase Parameter Resolution

The nested `TaskParameter` class captures a fundamental asymmetry in what the caller knows at request time versus what the network must supply. At construction, the caller provides only three things: an `InboundLedger::Reason` categorizing why this replay is needed, a `finishHash_` identifying the last ledger in the desired range, and a `totalLedgers_` count. The starting ledger hash — and the exact hashes of all intermediate ledgers — are unknown until the skip list arrives from the network.

`TaskParameter::update()` completes the picture once the skip list is available. It appends `finishHash_` to the skip list (making it fully inclusive), then derives `startHash_` by indexing backward exactly `totalLedgers_` positions from the end. The `full_` flag gates all downstream work; nothing can progress until this flag is set. The `XRPL_ASSERT` on `startHash_.isNonZero()` catches a scenario where the skip list is too short to cover the requested range — a data integrity check rather than graceful recovery.

The `update()` method is also deliberately idempotent-preventing: if `full_` is already true, it immediately returns false. Once a task's range is resolved, it cannot be re-resolved.

## Task Deduplication via `canMergeInto()`

Before creating a new `LedgerReplayTask`, `LedgerReplayer` checks whether an incoming replay request can be satisfied by an existing task. `canMergeInto()` handles two cases:

1. **Same finish hash, smaller or equal range**: the new request's ledgers are a strict suffix of what the existing task is already fetching.
2. **Finish hash falls inside an existing task's skip list**: once the existing task's `full_` is set, the new request's `finishHash_` can be located in the already-known skip list, and if the existing task covers at least as many ledgers from that point forward, the new request is redundant.

The second case requires careful arithmetic: `existingTask.totalLedgers_ >= totalLedgers_ + (exList.end() - i) - 1`, where `(exList.end() - i) - 1` is the number of ledgers in the existing task that come after the new request's finish point. This is non-obvious but ensures the existing task's range genuinely supersedes the new one.

## Lifecycle and the Init/Trigger Pattern

`LedgerReplayTask` inherits from `TimeoutCounter`, which provides a timer-driven retry loop running on the job queue. The separation of constructor and `init()` is intentional: construction sets up state, while `init()` registers the callback with `skipListAcquirer_` and starts the timer. This separation allows the task to be created and stored before it begins network activity.

`init()` registers a lambda on `skipListAcquirer_` using a `std::weak_ptr` capture — a deliberate lifetime safety measure. If the task is cancelled or destroyed before the skip list arrives, `wptr.lock()` will return null and the callback silently discards its result. This prevents use-after-free and avoids the need for explicit cancellation signals flowing from parent to child.

## The Sequential Build Loop

Once the skip list resolves the full parameter set, `replayer_.createDeltas()` is called to spawn one `LedgerDeltaAcquire` per ledger in the range (excluding the start ledger itself, which is the parent). Each delta is registered via `addDelta()`, which attaches a callback and appends to the `deltas_` vector. The assertion in `addDelta()` that `deltas_.back()->ledgerSeq_ + 1 == delta->ledgerSeq_` enforces strict ordering — deltas must be added in sequence because `tryAdvance()` depends on this to maintain a valid `parent_` chain.

The core of the task is `tryAdvance()`. It advances a cursor `deltaToBuild_` through the `deltas_` vector, calling `delta->tryBuild(parent)` on each. `tryBuild` returns null if the delta's network data hasn't arrived yet, causing `tryAdvance()` to suspend at that index. When any delta later signals readiness via `deltaReady()`, `tryAdvance()` resumes from where it stopped — not from the beginning. This means each ledger in the range is applied at most once, keeping replay efficient even when deltas arrive out-of-order relative to when they're actually needed.

The loop only starts when three conditions are all true: a parent ledger is available, `parameter_.full_` is set, and all `totalLedgers_ - 1` deltas have been registered. This prevents partial builds from beginning before the full work set is known.

## Error Handling and Timeout Policy

`tryAdvance()` wraps the build loop in a `try/catch` for `std::runtime_error`. `LedgerDeltaAcquire::tryBuild()` may throw this if the replayed ledger's resulting hash doesn't match the expected hash — indicating corrupt or malicious data from a peer. The task responds by setting `failed_` rather than propagating the exception, keeping the error contained within the task.

The `onTimer()` override scales the allowed timeout budget with the size of the replay range: `maxTimeouts_ = max(TASK_MAX_TIMEOUTS_MINIMUM, totalLedgers_ * TASK_MAX_TIMEOUTS_MULTIPLIER)`. This is important because a 200-ledger replay genuinely requires more clock time than a 2-ledger replay, and a fixed timeout budget would produce spurious failures for large ranges. The constants from `LedgerReplayParameters` place a floor of 10 timeouts at 500ms each, guaranteeing at least 5 seconds before a task is abandoned regardless of size.

## Concurrency and Lock Discipline

The most subtle locking pattern appears in `updateSkipList()`. It acquires the mutex to call `parameter_.update()`, then explicitly releases the lock before calling `replayer_.createDeltas()`. This is necessary because `LedgerReplayer` holds its own mutex, and calling into it while holding the task's mutex would risk deadlock if the replayer simultaneously tries to interact with this task. After `createDeltas()` returns, the lock is re-acquired to call `trigger()`. This deliberate lock-release-reacquire pattern is a form of lock ordering enforcement by design.

The base class `TimeoutCounter` uses a `recursive_mutex`, which permits `trigger()` and `tryAdvance()` to be called from `onTimer()` (which holds the lock) without deadlocking on reentrance. Functions annotated with `ScopedLockType&` parameters document the convention that the caller must hold the lock — a pattern used consistently across the ledger replay subsystem.