# `PerfLogImp.cpp` — Performance Logging Implementation

## Purpose and Role

`PerfLogImp.cpp` provides the concrete implementation of the `PerfLog` interface declared in `include/xrpl/core/PerfLog.h`. Its job is to give the XRPL server operator a time-series view of server internals: how many RPC methods started, finished, or errored; how long jobs waited in the queue before executing; what tasks are running right now; and how the node store is behaving. The output is a stream of JSON lines written to a dedicated log file at a configurable interval (defaulting to one second).

The file is the only implementation of `PerfLog` in the codebase. It is instantiated via the `make_PerfLog()` factory function and exists for the lifetime of `Application`.

## Class Structure

`PerfLogImp` extends `PerfLog` and owns a private `Counters` struct that consolidates all tracking state. `Counters` in turn holds two pre-built lookup maps — `rpc_` keyed by method-name string and `jq_` keyed by `JobType` enum — plus two secondary maps and a vector for tracking in-flight work. The separation lets the hot-path increment methods (`rpcStart`, `jobStart`, etc.) do a single map lookup and a single lock/increment, keeping the critical section minimal.

The header defines a `Locked<T>` template that bundles a value with its own `std::mutex`. Each entry in `rpc_` and `jq_` is a `Locked<Rpc>` or `Locked<Jq>`, meaning fine-grained per-entry locking rather than a single global counter lock. This matters because RPC and job queue events arrive on many threads concurrently.

## Pre-Population and Map Immutability

The constructor populates `rpc_` from `xrpl::RPC::getHandlerNames()` and `jq_` from `JobTypes::instance()` before any worker threads are spawned. After that, the maps' structure is frozen — keys are never added or removed at runtime. The header comment states this explicitly: *"rpc_ and jq_ do not need mutex protection because all keys and values are created before more threads are started."* This is a deliberate design choice: instead of a read-write lock protecting the whole map, callers only need to lock the individual `Locked<T>` entry they found. Map lookups by key are lock-free and safe because the map itself is never mutated.

If a caller passes an unrecognized method name or job type — which would be a programming error — the `UNREACHABLE` macro fires. These paths are marked `LCOV_EXCL_START`/`LCOV_EXCL_STOP` because they should never execute in production.

## RPC Lifecycle Tracking

`rpcStart(method, requestId)` increments `started` on the per-method entry and inserts a `MethodStart` pair (a `char const*` into the map key and a `steady_clock::now()` timestamp) into `methods_`, keyed by `requestId`. Using a `char const*` pointer rather than copying the string is a small but intentional optimization — the `std::string` key is stable in the unordered_map and will never move, so the pointer is safe and avoids allocation.

`rpcEnd()` (invoked by the public `rpcFinish()` and `rpcError()` wrappers) looks up and erases the `methods_` entry, captures the start timestamp, then locks the per-entry mutex and increments either `finished` or `errored` and accumulates elapsed microseconds. The `bool finish` parameter is the only branch. Both the `methodsMutex_` and the per-entry counter mutex are released before the function returns.

## Job Queue Lifecycle

Job tracking spans three distinct phases:
1. `jobQueue(type)` — increments `queued` when a job enters the queue.
2. `jobStart(type, dur, startTime, instance)` — called when a worker thread picks up the job. `dur` is the already-computed wait time (passed in by the caller rather than recomputed here). The `instance` parameter is the integer slot index of the worker thread and indexes directly into the `jobs_` vector, recording `{type, startTime}` so `currentJson()` can report what each thread is doing right now.
3. `jobFinish(type, dur, instance)` — accumulates running duration and resets the `jobs_[instance]` slot to `{jtINVALID, steady_time_point()}`, signaling that the slot is idle.

`resizeJobs(resize)` extends the `jobs_` vector when the job queue creates a new worker thread. New slots are pre-filled with `jtINVALID`. Resizing only ever grows; shrinking is not supported because threads are not removed in practice.

## Background Reporting Thread

`start()` launches a background thread that runs `run()`, named `"perflog"` via `beast::setCurrentThreadName`. The loop uses `cond_.wait_until()` against `lastLog_ + setup_.logInterval`, meaning it sleeps for the remainder of the interval and wakes either on expiry or when `stop_` is set. This avoids drift accumulation compared to a naive `sleep_for`.

On each wakeup the loop checks the `rotate_` flag (set by `rotate()`, typically called on SIGHUP). If set, `openLog()` is called to close and reopen the file before `report()` runs. The flag is only tested and cleared inside the locked section of `run()`, ensuring that a `rotate()` call racing with the report cycle is not lost.

`report()` assembles a JSON object with: ISO wall-clock timestamp, worker count (from `jobs_.size()`), hostname, aggregate counters from `countersJson()`, node store counts via `app_.getNodeStore().getCountsJson()`, currently-running activities from `currentJson()`, and server-state accounting from `app_.getOPs().stateAccounting()`. The assembled object is serialized as a single compact JSON line via `Json::Compact`, then `std::endl` flushes the stream. Each line in the log file is therefore self-contained and parseable independently.

## Dual-Clock Design

The implementation deliberately uses two clocks for different purposes. `std::chrono::steady_clock` is used for all duration measurements — it is monotonic and immune to NTP jumps or operator clock adjustments that would corrupt elapsed-time values. `std::chrono::system_clock` is used for the wall-clock timestamp recorded in the log file and for scheduling the `lastLog_` interval. This distinction ensures that duration statistics remain reliable even when system time is adjusted.

## Log File Management

`openLog()` is called from the constructor (to open the file immediately) and from `run()` after a rotation signal. If the parent directory does not exist, `boost::filesystem::create_directories` is called. On failure, the fatal journal log is written and `signalStop_()` is invoked to bring the server down rather than silently continuing without performance data. The file is opened in append mode (`std::ios::app`) so rotation does not truncate existing data before an external log manager has processed it.

## Configuration

`setup_PerfLog()` reads two keys from the `[perf]` section of `xrpld.cfg`: `perf_log` (a file path, resolved relative to the config directory if not absolute) and `log_interval` (an integer number of seconds). If `perf_log` is empty, the `PerfLogImp` instance still exists but the background thread is never started and `report()` returns immediately — the counters remain valid and can be queried via `countersJson()` / `currentJson()` for the `get_counts` RPC command.