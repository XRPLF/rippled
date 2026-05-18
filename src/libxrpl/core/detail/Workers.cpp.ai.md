# Workers.cpp — XRPL Thread Pool Implementation

## Role and Purpose

`Workers.cpp` implements the core thread-pool machinery used throughout the XRPL node to process asynchronous work items. It lives in `src/libxrpl/core/detail/` as an internal implementation detail, providing a fixed-interface thread pool where callers submit tasks via `addTask()` and receive callbacks through a single virtual method `Callback::processTask(int instance)`. The design intentionally keeps task semantics opaque — a "task" is simply one call to `processTask`; what that call does is entirely the caller's concern.

## Two-Class Architecture

The file defines two closely coupled classes: the outer `Workers` managing the pool policy and the inner `Worker` representing each OS thread. `Worker` inherits from **two** instantiations of `beast::LockFreeStack<Worker>::Node` — one untagged (for `m_everyone`, the ownership list) and one tagged with `PausedTag` (for `m_paused`, the reuse list). This dual-inheritance trick lets a single `Worker` object simultaneously live in both lock-free stacks without extra allocation, relying on the fact that each `LockFreeStack::Node` base carries its own independent `m_next` atomic pointer.

## Semaphore as the Single Dispatch Channel

The central synchronization primitive is a hand-rolled `semaphore` (a counting semaphore built on `std::mutex` + `std::condition_variable`). The project cannot use `std::counting_semaphore` because both GCC before 16.0 and Clang before 19.1 contained bugs in that facility (see `semaphore.h` for the linked bug reports). The custom type is an alias `xrpl::semaphore = basic_semaphore<std::mutex, std::condition_variable>`.

Crucially, `m_semaphore` carries **two kinds of signals** on the same channel: real work items posted by `addTask()`, and "please pause" signals posted by `setNumberOfThreads()` when the count is being reduced. There is no separate control channel. This is intentional: it keeps the worker loop simple. Every time a worker is woken, it first checks `m_pauseCount`. If the atomic is positive, the worker attempts a decrement-and-claim. If successful (`pauseCount >= 0` after decrement), it breaks out of the active loop and parks itself. If another racing thread already stole the pause slot (decrement went negative), the worker undoes its decrement and falls through to claim a real task. This compare-and-undo pattern at lines 196–207 is a lightweight optimistic-concurrency protocol that avoids a heavier CAS loop.

## Worker Lifecycle: Active → Paused → Active

`Workers` never destroys a thread until `Workers` itself is destroyed. When the target thread count drops, workers are paused rather than terminated. The lifecycle has three named states as documented in the header:

- **Active/Idle**: The thread is in the `for(;;)` loop at line 180, blocked on `m_semaphore.wait()`.
- **Paused**: The thread has pushed itself onto `m_paused`, decremented `m_activeCount`, and is blocked on its own `wakeup_` condition variable at the `[1]` label (line 247).
- **Active (resumed)**: `setNumberOfThreads()` increasing the count calls `Worker::notify()`, which increments `wakeCount_` and signals `wakeup_`. The paused thread unblocks, the `do/while` loop continues, and it re-enters the active state incrementing `m_activeCount` again.

The outer `do { ... } while (!shouldExit)` structure is the key: the exit check only happens when a thread wakes from a pause, not from a task wake. This is a deliberate invariant — a thread cannot be told to exit while it is idle-waiting for tasks; it must first be paused by `setNumberOfThreads(0)` (which `stop()` calls), then its destructor signals `shouldExit_`.

## Thread Teardown and the Double-Condition Stop

`stop()` calls `setNumberOfThreads(0)` to post one pause signal per active thread, then waits on a condition variable with a two-part predicate:

```cpp
m_cv.wait(lk, [this] { return m_allPaused && numberOfCurrentlyRunningTasks() == 0; });
```

Both conditions are necessary because `m_allPaused` (a `bool` protected by `m_mut`) and `m_runningTaskCount` (an `std::atomic<int>`) are **not** synchronized under the same lock. There is a window where the last worker has set `m_allPaused = true` and pushed itself onto the paused list, but has not yet returned from `processTask()` — meaning `m_runningTaskCount` is still non-zero. Waiting on only `m_allPaused` would race against that task completing.

The inverse race — missing the condition variable signal — is prevented by the code at lines 221–225. When `m_runningTaskCount` drops to zero, the worker acquires `m_mut` before calling `m_cv.notify_all()`. This lock acquisition serializes against the predicate evaluation inside `stop()`'s `cv.wait()`: it guarantees the notification cannot be delivered between the time `stop()` evaluates the predicate as false and the time it actually sleeps.

After `stop()` returns, `~Workers()` calls `deleteWorkers(m_everyone)`, which pops workers off the ownership stack and `delete`s each one. `Worker::~Worker()` sets `shouldExit_ = true`, increments `wakeCount_`, signals `wakeup_`, and calls `thread_.join()` — a clean, blocking teardown per thread.

## Dynamic Resize and Thread Reuse

`setNumberOfThreads()` checks the delta between the old and new counts. On increase, it first tries to pop a paused worker from `m_paused` and call `notify()` on it before creating a new `Worker` object. This reuse strategy avoids OS thread creation overhead when the pool is merely being expanded back to a previous size. A `static int instance` counter assigns a monotonically increasing identity to each newly created (not reused) worker; this `instance_` value flows into every `processTask(instance)` call, letting the `PerfLog` subsystem track per-slot telemetry without dynamic dispatch or thread-local storage.

The comment at line 36 flags an important limitation: rapid reduce-then-increase calls can create more paused threads than intended, because the pause signals are in-flight in the semaphore before any worker has consumed them. This is a known approximation, not a bug — the pool will eventually self-correct as workers claim the orphaned pause signals.

## Thread Naming

`beast::setCurrentThreadName()` is called at the top of each task-processing iteration to restore the configured name, guarding against callbacks that rename their own thread. Paused threads receive a parenthesized variant `"(Worker)"` to make them identifiable in debugger thread lists.