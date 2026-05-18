# `include/xrpl/core/PerfLog.h`

## Role in the System

`PerfLog` is the performance telemetry interface for the `rippled` node. It provides a centralized, always-on instrumentation layer that tracks two classes of activity: RPC method calls arriving from clients and internal jobs dispatched through the `JobQueue`. The interface is intentionally abstract — the concrete `PerfLogImp` lives in `src/xrpld/perflog/detail/` — which permits a true no-op implementation for contexts where the logging overhead or file I/O is unwanted.

The node documentation describes this as a singleton that must exist before other `Application` objects are launched. That ordering guarantee matters because call sites in the job queue and RPC handler call into `PerfLog` without null-checking; the object must always be valid.

## Configuration and Lifecycle

The nested `Setup` struct captures the two knobs from the `[perf]` section of `xrpld.cfg`:

- `perfLog` — path to the output file. If empty, the background writer thread is never started and all file I/O is suppressed, but counter tracking still works.
- `logInterval` — how frequently a full JSON snapshot is flushed to disk. Stored as `milliseconds` (rather than seconds) explicitly to support faster test cadences.

`setup_PerfLog()` parses a raw config `Section` into this struct, resolving relative paths against the config directory. `make_PerfLog()` constructs the concrete `PerfLogImp` via factory, taking a `signalStop` callback that the implementation invokes if it cannot open its log file — a fatal condition that should halt the node rather than silently drop metrics.

`start()` and `stop()` control the background writer thread. Both have default empty implementations in the base class so that a hypothetical no-op `PerfLog` subclass needs no extra plumbing.

## Clock Strategy

The header defines two clock type aliases: `steady_clock` and `system_clock`. The design separates their purposes. `steady_clock` (monotonic) measures elapsed durations — queue wait times, RPC execution times, current job run times. `system_clock` provides wall-clock timestamps for the JSON log entries written to disk. Using `steady_clock` for duration measurement prevents distortions from NTP adjustments, while `system_clock` is required for human-readable timestamps in the output.

## Two-Track Instrumentation API

### RPC Tracking

RPC calls are tracked with a start/finish/error triple, keyed by both method name and a `requestId`:

```
rpcStart(method, requestId)   // call begins, start time recorded
rpcFinish(method, requestId)  // call completed successfully
rpcError(method, requestId)   // call completed with an error
```

The `requestId` is a `uint64_t` that serves as a correlation key to pair the start event with its end event, allowing the implementation to compute exact per-call duration. The distinction between `rpcFinish` and `rpcError` matters for observability: operators can see the error rate per method separately from the success rate, and both contribute to cumulative duration.

### Job Queue Tracking

Job tracking follows a three-event lifecycle with additional context:

```
jobQueue(type)                             // job enters the queue
jobStart(type, dur, startTime, instance)   // job begins executing
jobFinish(type, dur, instance)             // job completes
```

The `instance` parameter identifies which `JobQueue` worker thread is running the job. This maps directly to a slot in a fixed-size `jobs_` vector (sized by `resizeJobs()`), letting the implementation record a per-worker snapshot of what's currently executing without a hash map lookup. `dur` in `jobStart` is the queued duration (time spent waiting); `dur` in `jobFinish` is the running duration. Both are accumulated in aggregate counters.

`resizeJobs()` must be called when worker threads are added to the `JobQueue`. It extends the `jobs_` vector, filling new slots with `{jtINVALID, steady_time_point()}` sentinel values. The implementation only grows this vector, never shrinks it, avoiding invalidation issues.

## JSON Output: Counters vs. Current

Two query methods expose the collected data:

- `countersJson()` returns aggregate historical counters — totals for each RPC method and job type, broken down by started/finished/errored counts and cumulative duration. Entries for methods/types that were never invoked are omitted to keep output compact. A synthetic `total` entry rolls up all RPC and job activity.

- `currentJson()` returns a live snapshot of in-flight work — which jobs are currently executing and for how long, and which RPC methods are still pending. This is the data that appears in the `current_activities` field of each periodic log entry.

The separation matters for different diagnostic use cases: counters answer "how busy has the node been?", while current answers "what is the node doing right now?".

## Concurrency Model

The implementation uses a fine-grained locking strategy. The `rpc_` and `jq_` maps are populated once at construction time (pre-threading) and never structurally modified afterward, so no lock is needed to look up a bucket. Each bucket is wrapped in a `Locked<T>` box that bundles the counter value with its own `std::mutex`, allowing independent increment of unrelated method or job type counters. Global mutexes cover only the two cross-bucket structures: `methodsMutex_` for the in-flight RPC map and `jobsMutex_` for the per-worker job vector.

The background writer thread (`run()`) sleeps on a `condition_variable` until either the log interval expires or `stop_`/`rotate_` flags are set. Log rotation (triggered by `rotate()`, typically on SIGHUP) sets `rotate_ = true` and wakes the thread, which reopens the file before writing the next snapshot.

## `measureDurationAndLog` Utility

The header also exposes a standalone template utility outside the `PerfLog` class:

```cpp
template <typename Func, class Rep, class Period>
auto measureDurationAndLog(Func&& func, std::string const& actionDescription,
                           std::chrono::duration<Rep, Period> maxDelay,
                           beast::Journal const& journal);
```

This wraps any callable, measures its wall time with `high_resolution_clock`, and emits a `JLOG(warn)` if execution exceeded `maxDelay`. It is not connected to the `PerfLog` counter infrastructure — it has no aggregation, no persistent state. Call sites include peer message dispatch (`PeerImp.cpp`, threshold 350ms), database session acquisition (`DatabaseCon.h`), inbound ledger acquisition (500ms), and consensus validation ledger lookup (10ms). The pattern suits transient instrumentation points where you want a warning but not a full counter, without needing access to the `PerfLog` singleton.