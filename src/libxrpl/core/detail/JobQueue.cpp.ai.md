# `src/libxrpl/core/detail/JobQueue.cpp`

## Role in the System

`JobQueue` is the central asynchronous dispatch engine for a running XRPL node. Nearly every non-trivial operation — processing transactions, publishing ledgers, responding to RPC calls, validating consensus proposals — is ultimately submitted here as a typed job and executed on a background worker thread. The file implements the `JobQueue` class declared in `include/xrpl/core/JobQueue.h`, which privately inherits from `Workers::Callback`. That inheritance is the key architectural seam: `JobQueue` owns the `Workers` thread pool and is simultaneously its sole callback, meaning `processTask()` is called by pool threads whenever work is available.

## The `Workers` / `processTask` Contract

`Workers` is a classic semaphore-based thread pool. Each call to `m_workers.addTask()` increments the semaphore and unblocks one idle thread, which then calls `JobQueue::processTask()` exactly once. The 1:1 mapping is strict — every `addTask()` call commits to exactly one `processTask()` invocation. This means the `JobQueue` cannot over-signal the pool; it must track when real work is actually ready to run, not merely enqueued.

## Job Submission: `addRefCountedJob()`

The public-facing `addJob()` template wraps a callable in a `ClosureCounter` (via `jobCounter_.wrap()`) before delegating to `addRefCountedJob()`. This ref-counting is what makes graceful shutdown possible: `jobCounter_.join()` in `stop()` blocks until every wrapped closure has been destroyed, meaning every job has completed.

Inside `addRefCountedJob()`, the submission logic implements a per-type concurrency gate. Each `JobType` has a maximum running-jobs limit defined in `JobTypes.h` (e.g., `jtPACK` allows 1, `jtLEDGER_REQ` allows 3, most client types are unbounded). When a job is added:

- If `data.waiting + data.running < limit`, `m_workers.addTask()` is called immediately — a worker thread will pick it up soon.
- Otherwise, `data.deferred` is incremented. No thread is woken. The job sits in `m_jobSet` but no `addTask()` is issued yet.

This deferred mechanism prevents over-committing to the thread pool for high-volume job types. The corresponding release happens in `finishJob()`: when a running job of that type completes, if `deferred > 0`, one deferred count is consumed and `m_workers.addTask()` is called — restoring the 1:1 balance.

## Job Selection: `getNextJob()`

Jobs are stored in `m_jobSet`, a `std::set<Job>` ordered by job priority (lower enum value = higher priority, since the set is iterated from `begin()`). `getNextJob()` walks the set linearly, skipping any job type that is currently at its running limit. The first type with available slots is claimed: `waiting` is decremented, `running` is incremented, and the job is removed from the set.

This scan-to-find approach is simple but has a subtle invariant: the `Workers` callback contract guarantees that when `processTask()` is called, at least one runnable job must exist in `m_jobSet`. The assertion `XRPL_ASSERT(iter != m_jobSet.end())` at the end of `getNextJob()` enforces this. Violating it would indicate a logic error in the deferred accounting.

## `processTask()`: Execution and Timing

`processTask()` runs on a pool worker thread. It acquires `m_mutex`, calls `getNextJob()` to claim the next runnable job, increments `m_processCount`, then releases the lock before executing the job. The timing structure is deliberate:

- `q_time` is measured from the job's `queue_time()` to the moment execution starts — this captures queue latency.
- `x_time` is measured from execution start to completion — this captures execution latency.

Both metrics are forwarded to `perfLog_` (a `PerfLog` instance) and, if either exceeds 10ms, to the per-type `dequeue` and `execute` event instruments exposed through `beast::insight`. These feed into the node's metrics/monitoring infrastructure.

After `job.doJob()` returns, the `Job` object itself goes out of scope before the mutex is re-acquired. This ordering is intentional: the comment at line 367 explains that job destructors may have side effects (e.g., releasing `LoadEvent` references), and these must complete while the parent objects they may reference are still alive — before any shutdown path proceeds.

The final block re-acquires the mutex, calls `finishJob()` to decrement `running` and potentially wake a deferred task, decrements `m_processCount`, and signals `cv_` if both `m_processCount == 0` and `m_jobSet.empty()` are true. That condition variable is the rendezvous point for both `rendezvous()` and `stop()`.

## Shutdown Sequence

`stop()` proceeds in three phases:

1. `stopping_ = true` is set atomically, preventing new `addJob()` calls from being accepted (via the `jobCounter_` which stops wrapping closures once joined).
2. `jobCounter_.join()` blocks until all `ClosureCounter`-wrapped job functions have been destroyed — i.e., all job *executions* have finished and returned from `Job::doJob()`.
3. Even after all jobs have returned from `doJob()`, threads may still be between returning from `doJob()` and exiting `processTask()`. The code waits on `cv_` for `m_processCount == 0 && m_jobSet.empty()` before setting `stopped_ = true` and asserting all coroutine suspensions have also completed (`nSuspend_ == 0`).

This three-phase drain is why the final assertions are trustworthy — each phase closes off a different window of concurrent activity.

## Metrics and Observability

The constructor registers a `beast::insight::Hook` (`collect()`) that fires periodically to push the current `m_jobSet.size()` into a gauge named `job_count`. Per-type latency data is surfaced through `getJson()`, which reports waiting counts, running counts, peak queue latency, and average execution latency for every active job type. The `isOverloaded()` predicate checks all `LoadMonitor` instances for whether any type has exceeded its target latency thresholds — this is used upstream to throttle or shed load.

## Design Notes

The `m_invalidJobData` sentinel and the associated `getJobTypeData()` fallback reflect an acknowledged technical debt: the codebase still has paths that can query `jtINVALID`, and returning a harmless default is safer than crashing. The comment ("I hate it. We must remove jtINVALID completely") documents the intent to fix this properly.

The `XRPL_ASSERT` in `addRefCountedJob()` that permits zero-thread queues for `jtCLIENT` through `jtCLIENT_WEBSOCKET` is annotated as a workaround for "incorrect client shutdown ordering" — client-facing job types can be submitted during a shutdown window where worker threads have already been stopped, and silently dropping those jobs is preferable to an assertion failure.