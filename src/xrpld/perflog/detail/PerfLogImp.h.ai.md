# `PerfLogImp.h` — Performance Log Concrete Implementation

`PerfLogImp` is the single concrete implementation of the abstract `PerfLog` interface. It lives in `xrpld/perflog/detail/` (a `detail/` subdirectory, signaling it is not part of the public API), and it is instantiated only through the `make_PerfLog()` factory function defined in the same translation unit.

Its purpose is twofold: accumulate real-time performance counters for every RPC method and every `JobQueue` task type, and periodically serialize those counters as compact JSON to a dedicated performance log file. Both concerns are handled inside a single background thread driven by a `std::condition_variable`.

---

## `Locked<T>` — A typed mutex box

The file opens with a small utility template `Locked<T>` that bundles a value of type `T` with a `mutable std::mutex`. This is not a general-purpose smart wrapper (it deliberately omits `operator->` or automatic RAII access); instead it is a naming convention—wherever you see a `Locked<Rpc>` in the counter map, the rule is *always lock the mutex before touching the value*. The copy and move constructors copy or move only the **value**, not the mutex, which is correct: a newly constructed copy starts with an unlocked mutex of its own.

---

## `Counters` — the bookkeeping core

The nested `Counters` struct owns all runtime state that multiple threads touch:

**`Rpc` sub-struct** records, per named RPC handler, three counters (`started`, `finished`, `errored`) and a cumulative `duration` in microseconds. The `rpc_` member is an `unordered_map<string, Locked<Rpc>>` keyed by the handler name string. It is populated entirely in the `Counters` constructor from the compile-time set of handler names returned by `xrpl::RPC::getHandlerNames()`, and no keys are ever inserted or removed after that. This **write-once-read-many** structure is the key insight: because the map topology is frozen before any worker threads start, `rpc_.find()` is safe without a map-level lock. Only the per-entry `Locked<Rpc>` mutex is needed when mutating a counter.

**`Jq` sub-struct** does the same for job queue tasks, tracking `queued`, `started`, `finished`, and two duration accumulators—how long a job waited in the queue versus how long it ran. The `jq_` map is similarly pre-populated from `JobTypes::instance()` before threads launch.

**`jobs_`** is a `vector<pair<JobType, steady_time_point>>` sized by the number of `JobQueue` worker threads (via `resizeJobs()`). Each slot corresponds to one worker by its `instance` index; a running slot holds the job type and when it started, an idle slot holds `jtINVALID`. This vector is protected by a dedicated `jobsMutex_`, separate from the per-entry mutexes.

**`methods_`** is an `unordered_map<uint64_t, MethodStart>` tracking in-flight RPC requests by their `requestId`. The value is a `(char const*, steady_time_point)` pair recording the method name pointer and start time. It uses a separate `methodsMutex_`. Erasing the entry at completion avoids unbounded growth and provides the start timestamp needed to compute call duration.

---

## `PerfLogImp` — the background reporter

The class holds a `Setup` (log file path and interval), an `Application&` reference, a `beast::Journal`, a `std::function<void()>` called `signalStop_` for fatal-error escalation, and the `Counters` instance. The log file itself is a plain `std::ofstream logFile_`.

**Thread lifecycle.** `start()` spawns a thread running `run()`. `run()` loops, sleeping on a `condition_variable` until either the log interval elapses or `stop_` or `rotate_` is set. The sleep uses `wait_until` with a deadline anchored to `lastLog_ + setup_.logInterval`, so drift is bounded. `stop()` sets `stop_ = true`, notifies the condition variable, and joins the thread—ensuring clean shutdown even in the destructor, which calls `stop()` unconditionally.

**Log rotation.** UNIX daemons rotate logs by sending a signal. `rotate()` sets `rotate_ = true` and wakes the thread via `cond_.notify_one()`. The thread checks this flag, calls `openLog()` (which closes and re-opens the file in append mode, creating parent directories if needed), then clears the flag. This is the standard signal-safe rotation pattern.

**Fatal file errors.** If `openLog()` cannot create the log directory or open the file, it calls `signalStop_()`, triggering a graceful application shutdown. A `PerfLog` that cannot write is effectively broken, and the server should not silently continue with a dead performance subsystem.

**Reporting.** `report()` builds a single JSON document containing a timestamp, the worker count (`jobs_.size()`), hostname, cumulative counters from `countersJson()`, a snapshot of currently executing jobs and methods from `currentJson()`, node store statistics, and server state accounting. It writes a single compact JSON line followed by `std::endl` (which flushes the stream, ensuring each record reaches disk atomically with respect to log rotation).

---

## Concurrency design

The locking strategy is deliberately tiered. The `rpc_` and `jq_` maps are structurally frozen—no map-level lock is needed for lookups, only for per-entry counter mutation. The `jobs_` vector uses one coarse mutex because its entire content is copied for a `currentJson()` snapshot, minimizing lock contention on the hot path. The `methods_` map uses its own mutex because insertions and deletions happen concurrently across request threads.

The `run()` loop's `mutex_`/`cond_` pair is used only to synchronize the `stop_` and `rotate_` flags with the background thread—it is never held while doing I/O, keeping the critical section minimal.

`rpcFinish()` and `rpcError()` are thin inline wrappers over the private `rpcEnd()` helper, which captures the boolean `finish` flag. This avoids duplicating the lookup-and-erase logic for `methods_` and the counter mutation for `rpc_`.