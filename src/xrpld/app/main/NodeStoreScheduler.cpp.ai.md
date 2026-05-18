# NodeStoreScheduler.cpp

`NodeStoreScheduler` is a narrow adapter that bridges the `NodeStore::Scheduler` interface — defined in the storage layer — with the application-level `JobQueue`. It exists because the NodeStore backend is designed to be scheduler-agnostic: it knows nothing about how tasks get dispatched or how performance telemetry gets recorded. `NodeStoreScheduler` supplies those answers without coupling the storage layer to application internals.

## Architecture Role

The class inherits from `NodeStore::Scheduler`, a pure-virtual interface in `include/xrpl/nodestore/Scheduler.h` that declares three hooks: `scheduleTask()`, `onFetch()`, and `onBatchWrite()`. The first schedules actual deferred write work; the latter two are observability callbacks invoked after I/O completes. `NodeStoreScheduler` is the only concrete implementation in rippled, wiring both concerns into the application's central `JobQueue`.

## Task Scheduling: Correctness Under Shutdown

`scheduleTask()` posts the incoming `NodeStore::Task` reference as a `jtWRITE` job — a job type with defined concurrency limits and latency thresholds (1750ms warning, 2500ms critical per `JobTypes.h`). The method first checks `jobQueue_.isStopped()` for a fast exit when the system is clearly shutting down.

The subtler case is the fallback path:

```cpp
if (!jobQueue_.addJob(jtWRITE, "NObjStore", [&task]() { task.performScheduledTask(); }))
{
    task.performScheduledTask();
}
```

There is an unavoidable time-of-check-time-of-use gap between the `isStopped()` guard and the `addJob()` call. During a concurrent shutdown, the queue can refuse the job even though `isStopped()` returned false a microsecond earlier. The fallback makes `scheduleTask()` unconditionally safe: the task is never silently dropped, it just runs synchronously on the caller's thread instead of on a job-queue worker. This is a deliberate correctness tradeoff — doing the write synchronously is preferable to losing it entirely during a graceful shutdown.

## Load Telemetry: onFetch and onBatchWrite

`onFetch()` and `onBatchWrite()` have no fallback because they record metrics rather than perform mandatory work. Both call `jobQueue_.addLoadEvents()` with a dedicated job type and timing data. `onFetch()` distinguishes between `jtNS_SYNC_READ` and `jtNS_ASYNC_READ` based on the `FetchType` in the `FetchReport`. `onBatchWrite()` submits `jtNS_WRITE` along with the write count and elapsed duration from the `BatchWriteReport`.

Looking at `JobTypes.h`, these three measurement types (`jtNS_SYNC_READ`, `jtNS_ASYNC_READ`, `jtNS_WRITE`) are registered with zero concurrency limits — they are not executable job slots but purely measurement labels used to populate the job queue's load-monitoring subsystem. This is how the server produces the NodeStore I/O statistics visible in administrative RPCs. Both callbacks silently return if the queue is stopped; there is no point recording metrics that will never be read.

## Design Observations

Holding `jobQueue_` as a reference rather than a pointer makes the dependency non-optional and avoids a null check on every call. The class carries no state beyond this single reference and no ownership of any resources, keeping its lifetime semantics trivial. Because the `NodeStore::Scheduler` interface is virtual, the NodeStore layer can be tested in isolation by substituting a mock scheduler without touching any `JobQueue` machinery.