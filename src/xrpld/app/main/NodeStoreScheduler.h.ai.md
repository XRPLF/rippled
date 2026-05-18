# NodeStoreScheduler.h

`NodeStoreScheduler` is a narrow adapter that connects the `NodeStore::Scheduler` interface to the application's central `JobQueue`. Its entire purpose is to let NodeStore backends remain ignorant of the application's threading model while still benefiting from it.

## Role in the System

The `NodeStore::Scheduler` abstract interface (defined in `include/xrpl/nodestore/Scheduler.h`) exists so that NodeStore backends can dispatch asynchronous work and report I/O timing metrics without depending on any particular execution environment. `NodeStoreScheduler` is the concrete implementation of that interface for the production `rippled` application: it routes scheduled tasks through the `JobQueue` worker pool and feeds timing data back into `JobQueue`'s load-tracking system.

## Task Scheduling

`scheduleTask()` is the workhorse method. It posts the given `NodeStore::Task` as a `jtWRITE` job named `"NObjStore"` on the application `JobQueue`. The lambda captures `task` by reference — safe because `NodeStore::BatchWriter` (the primary `Task` implementor) waits for completion before destroying itself.

The shutdown handling in `scheduleTask()` deserves attention. There are two distinct failure points, and each is handled differently:

1. If `jobQueue_.isStopped()` is already true, the method returns immediately without executing the task at all. At this point the process is unwinding and persistent state is not a concern.

2. If `addJob()` returns `false` (which can happen during the brief window when the queue is draining but not yet fully stopped), the task is executed **synchronously on the caller's thread**. This fallback is critical: a dropped write task could leave the node store's batch in a partially-flushed state, so the code ensures the work always completes, even if it blocks the calling thread.

This two-tier approach reflects a real operational concern: batch writes that are silently discarded could corrupt the node object database.

## Performance Reporting

`onFetch()` and `onBatchWrite()` exist purely for telemetry. When a NodeStore backend completes a read or a batch write, it calls these methods with a report containing elapsed time (and, for reads, whether the fetch was synchronous or asynchronous, and whether the object was found). `NodeStoreScheduler` maps these directly to `JobQueue::addLoadEvents()`, using the job types `jtNS_ASYNC_READ`, `jtNS_SYNC_READ`, and `jtNS_WRITE` respectively. This wires NodeStore I/O metrics into `JobQueue`'s broader load-monitoring infrastructure, where they can influence scheduling decisions and be surfaced in admin diagnostics.

Both reporting methods guard with `isStopped()` and return early if the queue has shut down. Unlike `scheduleTask()`, there is no synchronous fallback — telemetry data during shutdown is not meaningful, and there is no state correctness at risk.

## Design Notes

The class holds `jobQueue_` as a reference, not a pointer or shared ownership, encoding a strict lifetime dependency: the `JobQueue` must outlive any `NodeStoreScheduler` that references it. In practice this is guaranteed by the application's object construction and destruction order in `ApplicationImp`.

The separation between task scheduling and performance reporting in the `Scheduler` interface is deliberate: it allows a mock or test scheduler to implement lightweight no-op reporting while still correctly driving task dispatch, which is useful when testing NodeStore backends in isolation.