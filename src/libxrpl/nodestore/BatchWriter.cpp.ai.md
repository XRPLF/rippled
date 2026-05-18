# `BatchWriter.cpp` — Batched NodeStore Write Coalescing

`BatchWriter` exists to solve a fundamental I/O efficiency problem: ledger processing generates a high-frequency stream of individual `NodeObject` writes, but backends like RocksDB perform far better when writes arrive in bulk. This file implements the coalescing buffer that bridges those two rates, scheduling one deferred batch flush per accumulation window rather than one disk operation per object.

## Architecture: Task Inheritance and the Scheduler Contract

`BatchWriter` privately inherits from `Task`, which is the `Scheduler`'s unit of deferred work. This single-dispatch interface — just `performScheduledTask()` — gives the backend-agnostic `Scheduler` a handle to invoke the flush without knowing anything about the writer's internals. In practice, the `RocksDBBackend` both owns a `BatchWriter` member and inherits `BatchWriter::Callback`, making itself the sink for the writes the batch drains into.

The `Scheduler` is intentionally abstract; implementations range from the production thread-pool scheduler to `DummyScheduler`, which invokes the task *synchronously on the calling thread*. This is precisely why the mutex type is `std::recursive_mutex`: when `store()` holds the lock and calls `m_scheduler.scheduleTask(*this)`, a synchronous scheduler will immediately re-enter `writeBatch()` on the same thread, which then tries to re-acquire the same mutex. A plain `std::mutex` would deadlock. The recursive variant is a deliberate choice to support both synchronous and asynchronous scheduler backends.

## The Double-Buffer Flow

The central data structure is `mWriteSet` (a `Batch`, i.e., `std::vector<std::shared_ptr<NodeObject>>`), which accumulates objects from producer threads via `store()`. When `writeBatch()` runs, it atomically *swaps* `mWriteSet` with a local `set` under the lock, then releases the lock before calling `m_callback.writeBatch(set)`. This is the classic double-buffer pattern: the lock is held only for the O(1) swap, never during the actual I/O. After the swap, `mWriteSet` is empty and immediately available for producers to fill again while the previous batch is being written to disk.

The inner loop in `writeBatch()` continues draining until it finds `mWriteSet` empty after a swap, at which point it clears `mWritePending` and signals `mWriteCondition`. An `XRPL_ASSERT` verifies the invariant that `mWriteSet` is indeed empty after the swap — a defensive check that guards against any future refactoring that might violate the atomicity of the swap.

## Back-Pressure and Flow Control

`store()` enforces an upper bound via `batchWriteLimitSize` (65,536 objects). When the pending batch reaches that ceiling, the caller blocks on `mWriteCondition`. This back-pressure prevents unbounded memory growth when the write thread falls behind producers — a critical safety valve in scenarios where disk I/O is temporarily stalled. Actual memory usage can reach roughly twice the limit because a second batch may be accumulating in `mWriteSet` while the first batch (already swapped out) is being written.

The `mWritePending` flag ensures only one scheduler task is outstanding at a time. The first `store()` that finds the flag clear raises it and calls `scheduleTask()`; subsequent `store()` calls during the same window simply append to `mWriteSet` without re-scheduling. This single-task-at-a-time invariant is safe because `writeBatch()` loops until the buffer is empty before clearing `mWritePending`, so no objects are silently dropped.

## Load Estimation

`getWriteLoad()` returns `std::max(mWriteLoad, static_cast<int>(mWriteSet.size()))`. `mWriteLoad` is set to the size of the batch at the moment of the swap and represents the in-flight write count; `mWriteSet.size()` represents the objects queued but not yet dispatched. Taking the maximum gives a conservative high-water estimate that correctly reflects pressure in both the "writing" and "accumulating" phases simultaneously.

## Lifecycle and Shutdown Safety

The destructor calls `waitForWriting()`, which blocks on `mWriteCondition` until `mWritePending` is false. This guarantees that no pending objects are silently abandoned when a backend is torn down — a critical correctness property for ledger data integrity. Because the condition variable is `std::condition_variable_any` (compatible with `std::recursive_mutex`), this wait integrates cleanly with the same mutex used by all other methods.

## Performance Telemetry

After each successful flush, `writeBatch()` records both the object count and the wall-clock duration in a `BatchWriteReport` and passes it to `m_scheduler.onBatchWrite()`. This gives the scheduler (or its owner, the production `NodeStore::Database`) a real-time view of write latency, enabling adaptive behavior or metric reporting without coupling the writer itself to any specific monitoring infrastructure.