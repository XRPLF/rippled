# `TimeoutCounter.cpp` — Recurring Timeout Loop for Ledger Acquisition

`TimeoutCounter` is the base class for all network ledger-acquisition tasks in the XRPL node. It solves a specific infrastructure problem: how to repeatedly attempt an asynchronous fetch (transaction sets, ledger deltas, skip lists) from network peers, safely manage the object's lifetime across timer callbacks and job-queue callbacks, apply backpressure under load, and distinguish between genuine timeouts and partial progress — all without busy-waiting or blocking threads.

## The Async Loop

The class comment in the header describes `TimeoutCounter` as an "active object", and the implementation in this file delivers exactly that. The loop is:

1. `setTimer` arms a Boost.Asio `basic_waitable_timer` for `timerInterval_`.
2. When the timer fires, its async callback locks the mutex and calls `queueJob`.
3. `queueJob` posts a job to the application's `JobQueue`. If the queue is already saturated (see backpressure below), it restarts the timer instead.
4. The job queue eventually invokes the job, which calls `invokeOnTimer`.
5. `invokeOnTimer` acquires the lock, calls the pure-virtual `onTimer`, then restarts the timer via `setTimer` — unless `isDone()` is true, which terminates the loop.

Every step checks `isDone()` before doing any real work. `isDone()` returns true when either `complete_` or `failed_` is set. This makes the loop terminate cleanly regardless of which event — success from the subtype or cancellation from outside — happens first.

## Lifetime Safety via `pmDowncast()`

Both the timer callback and the job callback capture only a `weak_ptr<TimeoutCounter>`, obtained via the pure-virtual `pmDowncast()`. Each concrete subclass (`TransactionAcquire`, `LedgerDeltaAcquire`, `SkipListAcquire`) inherits from `std::enable_shared_from_this<ConcreteType>` and implements `pmDowncast()` by returning `weak_from_this()` cast to `weak_ptr<TimeoutCounter>`.

The weak-pointer pattern means that if the acquisition object is dropped by the `InboundLedgers` map while a timer or job is still in flight, the `lock()` call in the callback simply returns null and returns immediately. There is no dangling reference, and no explicit "deregister" step is required. The alternative — storing a `shared_ptr` in the lambda — would create a reference cycle that prevents the object from ever being destroyed once the loop starts.

## Backpressure via `jobLimit`

`queueJob` checks the optional `QueueJobParameter::jobLimit` before submitting to the `JobQueue`. If `getJobCountTotal(jobType)` is at or above the limit, the method logs a deferral message and calls `setTimer` again rather than adding another job. This is a deliberate backpressure valve: under heavy network load the node may accumulate many concurrent acquisition tasks, and piling more jobs on an already-saturated queue would only make things worse. The retry-via-timer approach lets the queue drain naturally before re-trying.

## Progress Tracking in `invokeOnTimer`

The `progress_` flag is the mechanism through which subtypes communicate partial forward progress to the base class. When the subtype receives some useful peer data but hasn't yet assembled a complete ledger object, it sets `progress_ = true` while holding the mutex. When `invokeOnTimer` runs next, it sees `progress_` is set, resets it to `false`, and calls `onTimer(true, sl)` — signalling that progress was made. If `progress_` is still `false`, the subtype has made no observable progress since the last tick, so `invokeOnTimer` increments `timeouts_` and calls `onTimer(false, sl)`. The subtype's `onTimer` implementation uses the boolean argument to decide whether to persist with more peers or to set `failed_ = true` and end the loop.

## `cancel()` Is Deliberately Soft

`cancel()` only sets `failed_ = true` under the lock. It does not call `timer_.cancel()` or attempt to remove any pending job. As the header comment notes: when the outstanding timer or job wakes up and tries to lock the weak pointer, it will call `isDone()`, see the failed state, and return immediately. This design avoids the complexity and potential races of canceling an Asio timer mid-flight, at the cost of a small delay before the loop fully stops. The approach is safe because the Boost.Asio `operation_aborted` check in `setTimer`'s lambda handles the case where the timer fires with an error code, ensuring no spurious `queueJob` call if the timer is naturally cancelled by the io_context shutting down.

## Constructor Invariant

The constructor asserts `timerInterval_ > 10ms && timerInterval_ < 30s`. The lower bound prevents the timer from hammering the job queue with effectively zero delay; the upper bound caps the worst-case wait before a failed acquisition is detected and retried. These bounds are wide enough to accommodate all three known subtypes but narrow enough to catch misconfigured callers at startup.

## Relationship to Subtypes

All three concrete subtypes — `TransactionAcquire` (SHAMap transaction sets), `LedgerDeltaAcquire` (ledger header and transactions for replay), and `SkipListAcquire` (skip-list data) — follow the same pattern: they inherit `TimeoutCounter`, implement `onTimer` and `pmDowncast`, call `setTimer` to start the loop, set `progress_` when partial data arrives, and set `complete_` when the fetch is fully satisfied. The base class owns the timing and queuing machinery; the subtypes own only the protocol-specific data handling.