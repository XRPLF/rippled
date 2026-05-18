# `include/xrpl/core/JobTypeData.h`

## Role in the System

`JobTypeData` is the runtime counterpart to `JobTypeInfo`. The XRPL job queue system divides per-type knowledge into two layers: static configuration that never changes (`JobTypeInfo` — limits, latency targets, name, type enum) and mutable runtime state that changes constantly as jobs flow through the system. `JobTypeData` owns that second layer for a single job category. Every `JobType` that the `JobQueue` manages gets exactly one `JobTypeData` instance, stored in a map keyed by `JobType`.

## What It Tracks

Three plain `int` members cover the lifecycle of jobs through the queue: `waiting` (enqueued but not yet dispatched), `running` (currently executing on a worker thread), and `deferred` (would have been enqueued but the running-job limit was hit). These are not atomic — they are protected by the `JobQueue`'s own mutex, which owns and manipulates them directly. The struct makes no concurrency promises of its own; it relies entirely on the caller.

The private `m_load` member is a `LoadMonitor`, which tracks average and peak latency over a rolling window and can signal when a job type is overloaded. Its targets are wired up in the constructor via `m_load.setTargetLatency(info.getAverageLatency(), info.getPeakLatency())` — pulling the configured thresholds from the immutable `JobTypeInfo`.

## Insight Event Integration

The two `beast::insight::Event` public members — `dequeue` and `execute` — are notification hooks for the metrics collection infrastructure. When a job finishes, `JobQueue` checks elapsed times and calls `dequeue.notify(q_time)` and `execute.notify(x_time)` if either the queue wait or execution exceeded 10ms. The event names follow a naming convention: queue-time events get the suffix `_q` (e.g., `"clientCommand_q"`), and execution-time events use the bare name (e.g., `"clientCommand"`). This makes it straightforward to distinguish queueing latency from processing latency in any connected metrics backend.

The constructor conditionally creates these events — only for non-special job types (`!info.special()`). Special jobs have a limit of zero, meaning they bypass normal queue dispatch entirely and are never timed in the standard way. Creating insight events for them would be both wasteful and potentially misleading.

## Design: Reference to Static Config

`info` is a `const&` to `JobTypeInfo`, not a copy. This is intentional: `JobTypeInfo` objects are themselves stored in a separate lookup table in `JobTypes.h`, constructed once at startup and never modified. Holding a reference instead of a copy avoids duplication while enforcing the contract that static configuration is not accidentally altered through a `JobTypeData` accessor.

Copy construction and copy assignment are explicitly deleted. Because `JobTypeData` holds a reference member and owns a `LoadMonitor` with internal mutex state, copying would be semantically wrong — there is no meaningful interpretation of "a copy of the runtime state for job type X." The delete makes this a hard compiler error rather than a silent bug.

## How `JobQueue` Uses It

`JobQueue` exposes a `getJobTypeData(JobType)` method returning a `JobTypeData&` and keeps a `JobDataMap` (a `std::map<JobType, JobTypeData>`). The dispatch path consults `data.waiting + data.running < getJobLimit(type)` before submitting a task to the worker pool; if over limit, it increments `data.deferred`. When `finishJob()` is called, the queue decrements `running`, checks `deferred > 0` to requeue a previously held-back job, and triggers the appropriate insight events with actual timing data.

The `stats()` method delegates to `m_load.getStats()`, returning a `LoadMonitor::Stats` snapshot containing count, average latency, peak latency, and an overloaded flag. `JobQueue` calls this during queue introspection (e.g., for RPC status responses) to surface per-type load information without exposing the `LoadMonitor` directly.