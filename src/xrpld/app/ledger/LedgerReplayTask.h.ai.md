# LedgerReplayTask.h

`LedgerReplayTask` is the top-level coordinator for replaying a contiguous range of historical ledgers during XRPL network synchronization. When a node needs to catch up by re-executing a known sequence of ledgers — rather than acquiring the full state tree — this class orchestrates the two-phase network acquisition process and drives incremental ledger construction.

## Role in the Ledger Replay Subsystem

The replay subsystem provides an alternative to full state-tree synchronization. Instead of downloading entire ledger state maps, a node can ask peers for the compact "delta" representation of each ledger (its header plus ordered transactions), then re-apply those transactions against the parent ledger. `LedgerReplayTask` sits at the top of this hierarchy, with `SkipListAcquire` and `LedgerDeltaAcquire` (both also `TimeoutCounter` subclasses) as subordinate network-acquisition tasks. `LedgerReplayer` acts as the factory and registry, while `LedgerReplayTask` focuses solely on the lifecycle of one range.

## TaskParameter: Deferred Initialization by Design

The nested `TaskParameter` struct captures the two-stage nature of the problem. At construction only three things are known: the replay `reason_`, the hash of the last ledger in the range (`finishHash_`), and how many ledgers to cover (`totalLedgers_`). Everything else — the sequence numbers, the full skip list, and the start hash — can only be known once the skip list embedded in the finish ledger is retrieved from the network.

The `full_` boolean gates all downstream work. `trigger()` and `tryAdvance()` both check `parameter_.full_` first and return immediately if it is false. This prevents any attempt to locate the start ledger or build deltas before the range boundaries are established.

The `update()` method populates the deferred fields when verified skip list data arrives. It validates that the provided hash matches `finishHash_`, that the skip list is long enough, appends `finishHash_` to the skip list, then computes `startHash_` by indexing from the end of the list. This is the only place where `full_` is set to `true`.

`canMergeInto()` enables deduplication at the `LedgerReplayer` level. A new task can be silently absorbed into an existing one if the two share the same `reason_` and the existing task's range fully covers the new request — either because they share the same finish hash with the existing range being at least as large, or (once the existing task is `full_`) because the new task's finish hash appears within the existing skip list at an offset where the existing task's total count subsumes the new task's count plus the remaining distance. This avoids redundant network traffic for overlapping catch-up requests.

## Asynchronous Execution Model

`LedgerReplayTask` inherits from `TimeoutCounter`, which provides an asynchronous loop: a repeating timer (`TASK_TIMEOUT = 500ms`) fires a job into the job queue, which calls `onTimer`. While the timer loop runs in the background, a separate callback-based flow makes forward progress.

On `init()`, the task registers a `weak_ptr`-captured lambda with its `SkipListAcquire` subtask. This callback fires when the skip list either succeeds or fails. On success, `updateSkipList()` is called, which fills `parameter_`, then calls `replayer_.createDeltas()` (outside the task lock, to avoid deadlock with `LedgerReplayer`'s own mutex), then re-acquires the lock and calls `trigger()`.

When each `LedgerDeltaAcquire` subtask completes, it invokes its own callback into `deltaReady()`, which calls `tryAdvance()` under the task lock.

## Sequential Chain Building in tryAdvance

`tryAdvance()` implements the core ledger construction loop. It only proceeds when three conditions are simultaneously true: the parent ledger is available (`parent_` is non-null), the parameter is `full_`, and all expected deltas have been added (`totalLedgers_ - 1 == deltas_.size()`). This last condition is important: deltas are created by `LedgerReplayer::createDeltas` after the skip list is known, so the task must wait for both the skip list and all delta subtasks to be registered before it can start building.

The loop walks `deltas_` from `deltaToBuild_` forward, calling `tryBuild(parent)` on each `LedgerDeltaAcquire`. Each successfully built ledger replaces `parent_`, becoming the base for the next delta — a strict sequential dependency that `XRPL_ASSERT` enforces by requiring consecutive sequence numbers. If any delta is not yet ready, the loop returns early; it will be re-entered when the next `deltaReady()` callback fires.

On completion of all deltas, `complete_ = true` is set. Any `std::runtime_error` from `tryBuild` (data corruption or replay failure) sets `failed_ = true` instead, stopping the task without retrying.

## Timeout Budget

The maximum number of timeouts before failure is computed as `max(TASK_MAX_TIMEOUTS_MINIMUM, totalLedgers_ * TASK_MAX_TIMEOUTS_MULTIPLIER)` — i.e., `max(10, n * 2)`. Longer ranges get proportionally more time before being abandoned. On each `onTimer()` call, `trigger()` is re-entered to retry acquiring the start ledger or re-driving `tryAdvance` for any newly ready deltas. If the timeout budget is exhausted, `failed_` is set and the `TimeoutCounter` loop exits.

## Lifetime and Concurrency

All three mutable internal pointers — `parent_`, `skipListAcquirer_`, and `deltas_` — are protected by the `recursive_mutex` inherited from `TimeoutCounter`. The `skipListAcquirer_` is held as a `shared_ptr` (not `weak_ptr`) to guarantee the subtask is not destroyed while the callback is still pending. Subtask callbacks capture `weak_ptr<LedgerReplayTask>` so that a destroyed task does not produce dangling calls. The public `finished()` method acquires the lock to safely read `isDone()`.

`test::LedgerReplayClient` is friended to allow test code to inspect internal state — a deliberate white-box testing escape hatch rather than a production coupling.