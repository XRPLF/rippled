# `include/xrpl/nodestore/Scheduler.h`

## Role and Purpose

This header defines the scheduling and telemetry interface for the NodeStore's asynchronous backend subsystem. It occupies a narrow but structurally important position: it decouples the database backends from any particular threading strategy, allowing the same backend code to run synchronously in tests or asynchronously on top of the production `JobQueue` without modification.

The file defines three things: a discriminated enum (`FetchType`) that distinguishes synchronous from asynchronous fetches, two plain-old-data report structs (`FetchReport`, `BatchWriteReport`) that carry performance telemetry, and the abstract `Scheduler` interface that backends call to hand off work and report results.

## The `Scheduler` Interface

`Scheduler` is a pure abstract base class with three virtual methods:

- **`scheduleTask(Task& task)`** — the core dispatch point. A backend calls this when it has deferred work ready to run (typically a batch flush). The contract is deliberately loose: the scheduler may invoke the task immediately on the calling thread, or post it to an unspecified foreign thread. This ambiguity is intentional — it lets production code post write jobs to the `JobQueue` while test code runs them inline.

- **`onFetch(FetchReport const& report)`** — called after any fetch completes, reporting elapsed time and whether the object was found. This is a telemetry hook, not a control path; backends call it to give the scheduler visibility into I/O performance.

- **`onBatchWrite(BatchWriteReport const& report)`** — called after a batch write completes, reporting elapsed time and the number of objects written.

The interface takes the task by non-const reference rather than by value or smart pointer. This is deliberate: `BatchWriter` implements `Task` privately and manages its own lifetime, so no heap allocation is needed for the common case.

## Report Structs

`FetchReport` captures whether a fetch was synchronous or asynchronous (via `FetchType`), whether the object was found (`wasFound`), and how long it took (`elapsed`). The `fetchType` member is `const` and set at construction, reinforcing that a report's nature is fixed at the moment of creation. `elapsed` is left zero-initialized via the brace initializer so a partially-filled report can still be passed without undefined fields.

`BatchWriteReport` captures elapsed time and a `writeCount`. Both structs are simple value types — no virtual methods, no reference members — so they can be created on the stack and passed directly to `onFetch` / `onBatchWrite` without allocating.

## Two Concrete Implementations

The header is paired with two concrete schedulers that reveal the full design intent:

**`DummyScheduler`** (used in tests and unit benchmarks) runs `performScheduledTask()` synchronously and ignores the report callbacks entirely. Its `scheduleTask` is a single-line call-through, making test behavior completely deterministic.

**`NodeStoreScheduler`** (production) wraps the application's `JobQueue`. Its `scheduleTask` posts a `jtWRITE` job and falls back to synchronous execution if the queue is stopped — a defensive measure to ensure pending flushes complete even during shutdown. Its `onFetch` and `onBatchWrite` call `jobQueue_.addLoadEvents(...)` to feed the load-balancing subsystem, mapping `FetchType::async` to `jtNS_ASYNC_READ` and `FetchType::synchronous` to `jtNS_SYNC_READ`. This means the `Scheduler` interface is simultaneously a dispatch mechanism and a metrics ingestion point, both concerns flowing through the same three methods.

## Relationship to `BatchWriter`

`BatchWriter` is the primary consumer of `Scheduler`. It implements `Task` privately, accumulates `NodeObject` stores under a mutex, and calls `scheduler_.scheduleTask(*this)` to trigger a deferred flush. After the flush completes, it constructs a `BatchWriteReport` and calls `scheduler_.onBatchWrite(report)`. This pattern means the scheduler sees every write batch complete without `BatchWriter` knowing anything about threads or job queues.

## Design Observations

The design cleanly separates three concerns: *work dispatch* (`scheduleTask`), *fetch telemetry* (`onFetch`), and *write telemetry* (`onBatchWrite`). Grouping all three into a single `Scheduler` interface is slightly surprising — a purist might split telemetry into a separate observer — but it avoids a second injection point and keeps the backends' constructor signatures simple.

The choice of a raw reference for `scheduleTask(Task& task)` rather than `std::function` or `std::unique_ptr<Task>` is a performance-conscious one: it sidesteps heap allocation for the common batch-write case and relies instead on the caller (`BatchWriter`) to manage lifetime and ensure the task object outlives the scheduled execution.