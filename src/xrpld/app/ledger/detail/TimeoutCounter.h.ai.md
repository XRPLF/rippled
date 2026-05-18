# `TimeoutCounter` — Asynchronous Timeout-and-Retry Base Class

## Role in the System

`TimeoutCounter` is the foundational "active object" for all peer-driven data acquisition tasks in the XRPL node. When the ledger subsystem needs to fetch a ledger delta, a transaction set, or a skip-list from the network, it cannot block waiting for peers to respond. Instead, each acquisition task is modeled as a `TimeoutCounter` subclass that drives itself through a recurring timer-plus-job-queue loop, retrying peer requests until the data arrives or a failure threshold is reached.

Three concrete subclasses exist in the same `detail/` directory: `TransactionAcquire` (fetches SHAMap transaction sets from peers), `LedgerDeltaAcquire` (fetches ledger header and transactions for replay), and `SkipListAcquire` (fetches the skip-list stored inside a particular ledger). All three inherit the loop machinery from `TimeoutCounter` and only override `onTimer()` and `pmDowncast()`.

## The Asynchronous Loop

The loop is documented in the class header itself, and the implementation in `TimeoutCounter.cpp` reveals its full mechanics:

1. **`setTimer(sl)`** arms a `boost::asio::basic_waitable_timer` for `timerInterval_` in the future. The completion handler captures a `weak_ptr<TimeoutCounter>` (obtained via the pure-virtual `pmDowncast()`), so the timer handler never extends the lifetime of the acquisition object.

2. When the timer fires, the handler locks the object through the `weak_ptr`, acquires `mtx_`, and calls **`queueJob(sl)`**. If the job queue already holds too many concurrent jobs of the same type (checked against `QueueJobParameter::jobLimit`), `queueJob` drops back to `setTimer` rather than piling work into an overloaded queue — a lightweight admission-control mechanism.

3. **`invokeOnTimer()`** is the private job body. It acquires the lock, checks `isDone()`, inspects the `progress_` flag, and invokes the virtual `onTimer(bool progress, ScopedLockType&)`. If `progress_` was true, it is cleared and `onTimer` is told progress was made; otherwise the `timeouts_` counter is incremented and `onTimer` hears `false`. After `onTimer` returns, if the object is still not done, `invokeOnTimer` immediately arms the next timer by calling `setTimer` again.

This sequencing means there is at most one live timer and one queued job at any moment; the chain is strict and self-restarting.

## The `progress_` Flag and Backpressure

The class deliberately runs two concurrent async streams: the timeout loop and whatever peer-communication logic the subclass implements to actually acquire data. Subclasses set `progress_ = true` whenever they receive a partial response that moves them closer to completion. `invokeOnTimer` then clears the flag and passes the `progress` boolean to `onTimer`, giving the subclass a chance to extend the deadline rather than immediately mark the task as failed. This pattern avoids premature failure on slow-but-alive peers while still catching completely silent peers within a bounded number of additional intervals.

## Terminal State and Cancellation

`isDone()` returns true when either `complete_` or `failed_` is set. Both `invokeOnTimer` and `queueJob` check `isDone()` at entry and short-circuit immediately, ensuring that once a task concludes no further timer arms or job submissions occur. The public `cancel()` method sets `failed_` under the lock but — crucially — **does not cancel the outstanding timer or dequeue the pending job**. The comment in the header explains why: cancelling a Boost ASIO timer or removing a queued job is complex and error-prone; instead, the next time either fires they see `isDone() == true` and return early. This is a deliberate "lazy cancellation" design that sacrifices one superfluous timer expiry in exchange for significantly simpler state management.

## Lifetime Safety via `pmDowncast()`

All subclasses also inherit `std::enable_shared_from_this<Derived>`. The pure-virtual `pmDowncast()` returns a `std::weak_ptr<TimeoutCounter>` by calling `weak_from_this()` on the concrete derived type. This is necessary because `enable_shared_from_this` is templated on the derived type, not on `TimeoutCounter` directly. Every timer completion handler and job closure captures only this `weak_ptr`, so if the acquisition object is destroyed before the callback fires (e.g., the inbound-ledger manager gave up on it), the `wptr.lock()` call in the handler returns `nullptr` and the callback exits cleanly without accessing a dangling object.

## Locking Model

The lock type is `std::unique_lock<std::recursive_mutex>`. Recursive mutexes are generally suspect, but here they are justified: `setTimer` and `queueJob` are called both from external paths (with the lock already held by the caller) and from within the locked `invokeOnTimer`, making a non-recursive mutex deadlock. The public `ScopedLockType` alias is exposed to subclasses so their `onTimer` implementations can call base-class helpers (`setTimer`, `queueJob`) without worrying about double-locking — the recursion handles it. The lock is passed by reference through the entire call stack, making the locking discipline explicit.

## Construction Invariants

The constructor asserts `timerInterval_ > 10ms && timerInterval_ < 30s`. This enforces a reasonable operating range: a sub-10ms interval would flood the job queue, while a 30-second-or-longer interval suggests misconfiguration. The `QueueJobParameter` struct bundles the `JobType`, a display name, and an optional concurrency cap together so each subclass can declare its job-queue identity in one place.