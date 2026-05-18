# `DummyScheduler` — Synchronous No-Op Scheduler for NodeStore

## Role and Purpose

`DummyScheduler` is a minimal, test-friendly implementation of the `Scheduler` interface within the `xrpl::NodeStore` subsystem. Its sole purpose is to provide a concrete `Scheduler` that satisfies the interface contract without any thread management, queuing, or performance instrumentation — executing every task immediately on the calling thread instead.

The `Scheduler` abstraction exists because the NodeStore's `BatchWriter` needs to hand off write work asynchronously in production: batching ledger object writes to the backend without stalling the caller. `DummyScheduler` strips all of that machinery away, collapsing the async boundary into a direct synchronous call.

## Design: Intentional Minimalism

The `Scheduler` base class defines three virtual hooks:

- `scheduleTask(Task&)` — submit a unit of backend work for later (or immediate) execution
- `onFetch(FetchReport const&)` — observe completed fetch operations for performance monitoring
- `onBatchWrite(BatchWriteReport const&)` — observe completed batch writes for performance monitoring

`DummyScheduler` overrides all three. The implementation of `scheduleTask` is a single line:

```cpp
task.performScheduledTask();
```

The two reporting callbacks, `onFetch` and `onBatchWrite`, are empty bodies. There is no thread pool, no queue, no timer, and no statistics collection.

This is a deliberate design choice: the `Scheduler` contract explicitly allows implementations to invoke the task "on the current thread of execution," so `DummyScheduler` takes that permission to its logical extreme. A caller cannot distinguish between a `DummyScheduler` and a production scheduler from the perspective of correctness — only from the perspective of latency and throughput.

## Usage Contexts

`DummyScheduler` appears in two distinct call sites:

**Database import during application startup** (`Application.cpp`): When the node launches with `doImport` set, it constructs a `DummyScheduler` as a transient scheduling context for the source database being read during import. Because this operation is already a sequential, offline migration step with no live peer traffic to serve, the overhead of synchronous scheduling is irrelevant and the simplicity is a net benefit.

**Unit and integration tests** (`Backend_test.cpp`, `Database_test.cpp`, `Timing_test.cpp`, `NuDBFactory_test.cpp`, `shamap/common.h`): Test fixtures across the NodeStore test suite construct a `DummyScheduler` to stand in for the real scheduler. Tests want deterministic, single-threaded execution — a production scheduler that dispatches to a thread pool would introduce non-determinism and require careful teardown. `DummyScheduler` eliminates all of that complexity while still satisfying every interface requirement.

## Relationship to `BatchWriter`

The `BatchWriter` class (referenced in `Scheduler.h`'s `@see` annotation) is the primary consumer of `Scheduler` in production. It accumulates write requests and calls `scheduleTask` with a `Task` that flushes the batch to the backend. With `DummyScheduler`, each call to `scheduleTask` causes the flush to happen inline before `scheduleTask` returns — effectively disabling batching. This is acceptable for import and test workloads but would be a serious performance regression under normal ledger-processing load, which is why the production application uses a real async scheduler for its live database.

## Summary

`DummyScheduler` is a null-object pattern applied to the `Scheduler` interface: it satisfies every contract requirement while doing the minimum possible work. Its value is precisely its emptiness — it removes concurrency from the equation wherever concurrency would be an obstacle rather than a benefit, and it serves as the canonical test double for anything in the NodeStore that depends on scheduling.