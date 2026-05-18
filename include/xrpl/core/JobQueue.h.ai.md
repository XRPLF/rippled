# `include/xrpl/core/JobQueue.h`

## Role in the System

`JobQueue` is the central work-dispatch mechanism for the XRPL node. Every significant operation — consensus steps, ledger validation, RPC handling, peer-message processing, disk I/O, path-finding — is dispatched as a typed job through this class. It sits between the event-driven networking layer (which produces work) and the `Workers` thread pool (which consumes it), adding priority ordering, per-type concurrency limits, load monitoring, and first-class coroutine support.

The file contains three closely related declarations: `JobQueue` itself, the nested `Coro` class that embeds a stackful coroutine on top of the job queue, and the inline implementations of `postCoro` and the entire `Coro` body in the companion `Coro.ipp`.

## Thread Pool Architecture

`JobQueue` privately inherits `Workers::Callback` and overrides the single virtual method `processTask(int instance)`. This means `JobQueue` is not a general-purpose callback adapter — it _is_ the callback; the private inheritance expresses that relationship without polluting the public API.

The `Workers` layer is purely a thread pool abstraction: it holds a semaphore (one unit per pending task), maintains a fixed number of threads, and calls `Callback::processTask` whenever a thread wakes. It knows nothing about priorities, types, or limits. All of that logic lives in `JobQueue::processTask`, which calls `getNextJob` to dequeue the highest-priority runnable job and then executes it.

Jobs live in `m_jobSet`, a `std::set<Job>` ordered by priority (encoded in the `JobType` enum value). `getNextJob` takes the first job whose type has running-count below its configured limit, decrements the waiting count, increments the running count, and removes it from the set. Job types with a `limit` of 0 in `JobTypes.h` are unlimited (`getJobLimit` returns `INT_MAX`). Types with tighter limits — for example, `jtPACK` and `jtSWEEP` capped at 1, `jtLEDGER_DATA` at 3 — prevent expensive background work from crowding out latency-sensitive operations.

## Job Lifecycle and Shutdown Safety

`addJob` does not accept a raw closure. Instead it passes the handler through `jobCounter_.wrap()`, which returns a `ClosureCounter::Substitute`. This wrapper increments an atomic reference count on construction and decrements it on destruction. During shutdown, `ClosureCounter::join()` sets a "stop accepting" flag and blocks until the count reaches zero. Any `addJob` call that races with shutdown gets back `std::nullopt` from `wrap()`, and `addJob` returns `false` — no job is enqueued. This guarantees that the `Workers` thread pool is never given tasks after the `JobQueue` has decided to stop.

The `stopping_` atomic flag is separate from `ClosureCounter::join()` and exists purely for callers who need to gate new work eagerly (e.g. checking `isStopping()` before even attempting `addJob`).

`rendezvous()` blocks on `cv_` until both `m_processCount == 0` (no active `processTask` invocations) and `nSuspend_ == 0` (no suspended coroutines). This two-part condition is essential: a coroutine suspended mid-flight is not visible in `m_processCount`.

## Coroutine Support (`Coro`)

The `Coro` class wraps a `boost::coroutines2::coroutine<void>::pull_type`, providing a suspendable job abstraction. Its primary consumer is the RPC handler: when an RPC request arrives on a network thread, `postCoro` creates a `Coro` and returns immediately; the actual RPC work runs later, on a `Workers` thread, and can yield mid-execution to wait for async events (e.g., path-finding results) without blocking the worker thread.

### Construction Bootstrap

Constructing a `boost::coroutines2::pull_type` immediately transfers execution _into_ the coroutine body. The `Coro` constructor exploits this: the lambda passed to `coro_` immediately assigns `yield_` (the push-type sink) and calls `yield()` to bounce back to the constructor's frame. The coroutine is now suspended, ready to run when a worker picks it up. Only after construction completes does `postCoro` call `coro->post()` to schedule a job that will call `resume()`.

### The Post-Before-Yield Race

The header documents this precisely at lines 354–380. Consider: the coroutine is running user code (`fn`), decides to suspend, calls `post()` to schedule a wakeup, and _then_ calls `yield()`. If the scheduler is fast, the wakeup job can execute `resume()` before `yield()` runs. Without protection this would drive two threads simultaneously into the same coroutine stack — undefined behavior.

The fix is `mutex_`: `resume()` acquires `mutex_` before calling `coro_()`. The running coroutine itself holds `mutex_` (it was acquired when it was last resumed). So the competing `resume()` job blocks at the lock. After the coroutine yields, the lock is released, and the waiting `resume()` can proceed. By then, `coro_` may already be exhausted if the coroutine returned rather than yielded. `resume()` guards against this with `if (coro_)` — invoking `operator()` on a completed `pull_type` is undefined behavior.

### `mutex_` vs `mutex_run_`

Two mutexes serve different purposes. `mutex_` serializes re-entry into the coroutine body itself — it is held for the entire duration a coroutine is executing. `mutex_run_` protects the `running_` flag and the `cv_` condition variable used by `join()`, which only needs to know whether the coroutine is _currently executing_ (not whether it has finished). A caller that needs to wait for a coroutine to drain (e.g., during shutdown) calls `join()`, which blocks on `cv_` until `running_` is false.

### `nSuspend_` Accounting

`yield()` increments `jq_.nSuspend_` before suspending; `resume()` decrements it before re-entering. `expectEarlyExit()` decrements it without resuming — for the case where a `Coro` is destroyed while suspended (e.g., failed `postCoro`). This keeps `rendezvous()` accurate: a suspended coroutine that has not yet cleaned up is still "in flight" from the queue's perspective.

### LocalValues Swap

`resume()` saves and restores the thread's `detail::LocalValues` pointer around the coroutine body invocation. Each `Coro` owns its own `lvs_` storage so that per-thread state (e.g., job-scoped context) is isolated to the coroutine's logical thread of execution even though multiple coroutines share the same OS threads.

## Load Monitoring and Statistics

Each `JobType` has an associated `JobTypeData` holding a `LoadMonitor` configured with the target average and peak latencies from `JobTypes.h`. When a job dequeues or finishes, `LoadMonitor` accumulates timing data. `isOverloaded()` queries whether any type is consistently exceeding its peak latency threshold, and `getJson()` serialises current waiting/running/deferred counts for the admin RPC endpoint. Two `beast::insight` event handles (`dequeue`, `execute`) fire into the metrics collector for external monitoring.

## Summary of Key Invariants

- A job added to the queue always runs to completion unless the `Workers` object is destroyed or `ClosureCounter` is joined before the job starts.
- A `Coro` constructed and suspended by `postCoro` increments `nSuspend_`; it must eventually either resume-to-completion or call `expectEarlyExit()` to decrement the count.
- `mutex_` is never held across a `yield()` — that would deadlock on the next `resume()`.
- `m_mutex` (the queue-wide lock) is held only briefly for accounting updates, never while executing job logic.