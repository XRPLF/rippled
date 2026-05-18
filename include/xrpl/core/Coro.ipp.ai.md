# `JobQueue::Coro` — Coroutine Lifecycle Implementation (`Coro.ipp`)

## Role in the System

`Coro.ipp` provides the method bodies for `JobQueue::Coro`, the coroutine abstraction that allows XRPL server tasks — most notably RPC handlers — to suspend mid-execution, release their worker thread back to the pool, and resume later when an awaited event (such as a pathfinding result) arrives. It is an implementation-only `.ipp` file, `#include`d at the bottom of `JobQueue.h` after the class declaration, following the common XRPL pattern of separating templated or inline bodies into a companion file.

The underlying mechanism is `boost::coroutines2`, which stores and restores a full call stack. This gives callers an imperative, synchronous-looking API: they call `yield()` to pause and `post()` to schedule resumption, without callbacks or state machines.

## Construction: The First Yield is Mandatory

The constructor is the most subtle part of the design. When `boost::coroutines2::coroutine<void>::pull_type` is constructed, boost **immediately** transfers execution into the coroutine body before the constructor returns. This would be a problem if the calling thread (e.g., a network handler thread) were left spinning inside the coroutine.

The lambda passed to the `pull_type` therefore starts with an unconditional `yield()` call:

```cpp
yield_ = &do_yield;   // capture the push_type handle for later calls
yield();              // immediately suspend — give control back to constructor
fn(shared_from_this()); // user code only runs later, on a worker thread
```

This bootstrap yield guarantees the constructor returns to the caller with the coroutine parked, ready to be dispatched via `post()`. Only after `post()` queues a job and that job is dispatched by a worker thread does `resume()` run, which re-enters the coroutine and falls through to `fn(shared_from_this())` — the user's code.

A 1.5 MB stack size is requested via `boost::context::protected_fixedsize_stack`. The comment explains why: the default 1 MB was insufficient for deeply nested XRPL processing paths, which ASAN tests exposed. The extra headroom prevents stack overflow in production.

## Yield and Resume: The Dual-Mutex Design

`Coro` uses two mutexes for distinct purposes:

- **`mutex_`** serializes access to the `coro_` object itself. `resume()` holds this lock for the entire duration the coroutine is executing. This is the key mechanism that prevents the documented post-before-yield race (described in detail in `JobQueue.h` lines 354–380).
- **`mutex_run_`** guards `running_` and is used by `join()` to wait until the coroutine finishes a scheduled execution slice.

The race condition `mutex_` prevents is worth understanding explicitly: a coroutine may call `post()` (scheduling its own resumption) and then call `yield()`, but in a concurrent system the scheduled job can start and call `resume()` before `yield()` has executed on the original thread. Since `resume()` holds `mutex_` for the full run of the coroutine body and `yield()` releases control from within that same execution, any competing `resume()` that tries to re-enter is blocked at the lock until the coroutine has actually yielded (and `mutex_` is released). This transforms a potential double-execution bug into a harmless serialized wait.

`resume()` also checks `if (coro_)` before calling `coro_()`. Once a `boost::coroutines2` pull_type's user function returns, the object converts to `false`. Calling `operator()` on a completed coroutine is undefined behavior, so this guard handles the case where the late-arriving resume job finds the coroutine has already run to completion.

## Coroutine-Local Storage: Thread Identity Swapping

`resume()` contains a pattern that deserves attention:

```cpp
auto saved = detail::getLocalValues().release();
detail::getLocalValues().reset(&lvs_);
// ... run coroutine ...
detail::getLocalValues().release();
detail::getLocalValues().reset(saved);
```

`detail::getLocalValues()` is a `boost::thread_specific_ptr<LocalValues>` — a per-thread slot. Because multiple coroutines can share a worker thread (each taking a turn), XRPL needs per-coroutine "thread-local" state, not per-thread state. The solution is to swap the thread's current `LocalValues` pointer with the coroutine's own `lvs_` before entering the coroutine body and restore the original pointer after the coroutine yields or returns. `LocalValue<T>` objects throughout the codebase transparently read from whichever `LocalValues` is currently installed on the thread.

## `nSuspend_`: Tracking the Drain Condition

`jq_.nSuspend_` is a counter on the parent `JobQueue` that tracks how many coroutines are currently suspended (i.e., have called `yield()` but have not yet been resumed). `yield()` increments it under the queue's own mutex; `resume()` decrements it. The queue's shutdown logic uses this counter alongside the running-job count to know when it is fully quiescent and can stop its worker threads.

`expectEarlyExit()` handles the case where a `Coro` is abandoned before it ever re-enters its user function — for example when `postCoro()` finds that `post()` fails (queue is stopping). The coroutine was never run past its bootstrap yield, so `nSuspend_` was incremented but will never be decremented by a `resume()`. `expectEarlyExit()` decrements it manually and sets `finished_` in debug builds to suppress the destructor assertion.

## Lifecycle Invariants

The debug-only `finished_` flag enforces a critical invariant: the `Coro` must not be destroyed while still suspended or mid-execution. The `XRPL_ASSERT` in `~Coro()` fires in debug builds if someone drops the last `shared_ptr` reference to a `Coro` that hasn't fully run to completion. Because `Coro` inherits from `std::enable_shared_from_this`, the `post()` lambda captures `shared_from_this()` to extend the coroutine's lifetime until the job runs, preventing premature destruction even if all external handles are dropped.

`runnable()` simply checks whether `coro_` is still truthy — a thin predicate used by higher-level code to decide whether a `Coro` can still be dispatched, avoiding redundant `post()` calls on a completed coroutine.