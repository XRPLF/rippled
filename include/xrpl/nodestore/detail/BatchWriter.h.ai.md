# `BatchWriter.h` — Scheduled Batch-Write Helper for NodeStore Backends

## Role in the System

`BatchWriter` is a reusable helper that sits between a NodeStore backend and its storage engine, coalescing individual `NodeObject` writes into larger batches before flushing them to disk. It exists because individual key-value store writes carry per-operation overhead (system call, WAL append, compaction pressure in RocksDB), and amortizing that overhead across a batch of 64 K entries can dramatically reduce I/O latency under write bursts. The class is deliberately optional — a backend can ignore it and implement its own write strategy — but it provides a clean, production-tested path for backends that want standard batching behavior.

## Architecture: Task + Callback Separation

`BatchWriter` uses two cooperating abstractions from the same module. It *privately inherits* `Task`, turning the writer itself into a schedulable unit of work. The `Scheduler` interface (defined in `Scheduler.h`) is responsible for deciding *when* and *on which thread* to invoke `performScheduledTask()`. This separation means `BatchWriter` has no threading policy of its own: it produces batches and hands them to a scheduler, which may invoke the flush on the current thread or an asynchronous worker thread.

The actual write operation is delegated through `BatchWriter::Callback`, a pure virtual inner interface with a single method `writeBatch(Batch const& batch)`. The concrete backend (e.g., `RocksDBBackend` in `RocksDBFactory.cpp`) inherits both `Backend` and `BatchWriter::Callback`, implementing `writeBatch` to call the storage engine. This indirection keeps batching logic entirely within `BatchWriter` while remaining storage-agnostic.

## Write Flow and Double-Buffer Pattern

When `store()` is called, the object is pushed into the live accumulation buffer `mWriteSet`. If no write is already scheduled (`mWritePending == false`), the writer sets the flag and calls `m_scheduler.scheduleTask(*this)`. The scheduler will eventually call `performScheduledTask()`, which calls the private `writeBatch()`.

Inside `writeBatch()`, a critical design choice enables low-latency accumulation during the flush: the mutex is held only long enough to *swap* `mWriteSet` with a fresh local vector, then immediately released. The actual `m_callback.writeBatch(set)` call — which may be slow, involving disk I/O — happens *outside* the lock. This means callers can continue pushing new objects into `mWriteSet` concurrently while the previous batch is being committed to storage.

The function loops (`for(;;)`) after each flush to check whether new objects accumulated while the write was in progress. This drains the queue completely before clearing `mWritePending` and broadcasting on the condition variable, preventing a race where a scheduler might not re-fire after the last item was enqueued.

## Backpressure: Flow Control at the Limit

The `batchWriteLimitSize` constant (65,536 entries, defined in `Types.h`) enforces an upper bound on `mWriteSet`. Inside `store()`, if the batch reaches this limit, the caller *blocks* on `mWriteCondition.wait()` until `writeBatch()` drains the queue and notifies. This provides backpressure that prevents unbounded memory growth during sustained write bursts, at the cost of blocking the calling thread.

The comment in `Types.h` notes that actual in-flight memory can be up to *twice* `batchWriteLimitSize` — one batch being actively written plus a new one accumulating — which must be accounted for when sizing memory budgets.

## Mutex Choice: `recursive_mutex` + `condition_variable_any`

The lock type is `std::recursive_mutex`. This is notable because `condition_variable_any` (rather than the cheaper `condition_variable`) is required to work with non-standard lockable types like recursive mutexes. The recursion capability matters in the `waitForWriting()` path: the destructor calls `waitForWriting()`, which acquires the lock and blocks on the condition variable. If a scheduler happens to be running `writeBatch()` on the same thread (e.g., a synchronous scheduler for testing), recursive acquisition prevents a deadlock.

## `getWriteLoad()` Metric

`getWriteLoad()` returns `max(mWriteLoad, mWriteSet.size())`. `mWriteLoad` is set to the size of the batch handed to `writeBatch()` just before the lock is released for the actual write, and `mWriteSet.size()` is the pending accumulation count. The maximum of the two gives callers an estimate of total outstanding write work: either what is being committed right now, or what is waiting for the next scheduled flush. This is used externally to provide I/O load signals for scheduling decisions.

## Destruction Guarantee

The destructor calls `waitForWriting()`, which blocks until `mWritePending` is false — meaning the scheduler has flushed and cleared all pending data. This guarantees that no data is silently dropped on shutdown and that the backend's storage layer receives every object that was handed to `store()` before the `BatchWriter` is destroyed.

## Relationship to `DummyScheduler`

For unit testing and synchronous backends, the `DummyScheduler` (also in the nodestore module) invokes `performScheduledTask()` immediately on the calling thread inside `scheduleTask()`. This makes `BatchWriter` behave as a synchronous accumulator-then-flush, still correctand safe due to the recursive mutex design.