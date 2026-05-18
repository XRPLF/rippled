# `Workers.h` — Thread Pool with Pause/Resume Lifecycle

## Role in the System

`Workers` is the XRPL node's general-purpose thread pool. It sits inside `xrpl::core::detail` and is consumed by the job queue subsystem to dispatch work across a fixed or dynamically-adjusted set of OS threads. The design deliberately separates *task ownership* from *thread lifecycle*: callers never manage threads directly; they call `addTask()` and implement `Callback::processTask()`. The pool handles all the creation, pausing, reuse, and teardown of threads.

## Callback Contract

The `Callback` pure interface has a single method: `processTask(int instance)`. The `instance` parameter is the index of the worker thread that is executing the call, which allows callbacks to index into per-thread structures (such as `perf::PerfLog` job slots) without locking. The contract is strict: each call to `addTask()` produces exactly one call to `processTask()`, so the callback should process exactly one unit of work per invocation and return promptly. Work enqueued via `addTask()` is not lost even if the pool is shrunk, because the semaphore retains its count.

## Three-State Thread Lifecycle

Each `Worker` exists in one of three states:

- **Active/Idle** — the thread is inside its task loop, either executing `processTask` or blocked on `m_semaphore.wait()` waiting for the next task.
- **Paused** — the thread has exited its task loop and is sleeping on its own per-worker `wakeup_` condition variable. The worker still exists as an OS thread; it is merely dormant.

No worker thread is ever destroyed until `Workers` itself is destroyed. Reducing the thread count via `setNumberOfThreads()` pauses surplus workers; enlarging it later re-activates them from the `m_paused` stack. This worker-reuse strategy avoids the overhead of creating and joining OS threads on every resize, which matters because the job queue can resize frequently.

## The Semaphore as a Unified Signal Channel

`m_semaphore` (a `basic_semaphore` wrapping a mutex and condition variable — a temporary stand-in for `std::counting_semaphore` which has known bugs in GCC < 16 and Clang < 19.1) serves double duty. A call to `addTask()` does nothing but call `m_semaphore.notify()`, incrementing its count. Pause requests issued by `setNumberOfThreads()` also call `m_semaphore.notify()`, having first incremented `m_pauseCount`. The worker's inner loop cannot distinguish the two cases until after it has successfully acquired the semaphore — at that point it checks the atomic `m_pauseCount`. If `m_pauseCount > 0` and the decrement succeeds (guard against over-decrement by concurrent workers racing on the same pause signal), the worker breaks out to the pause path. Otherwise it assumes a real task and calls `processTask`.

This design is elegant: the semaphore is the single point of activation for all worker wake-ups, and the `m_pauseCount` counter overlays control messages onto the same queue without a separate channel.

## Dual LockFreeStack Membership via Tag Types

`Worker` inherits from two instantiations of `beast::LockFreeStack<Worker>::Node` simultaneously, distinguished by the empty `PausedTag` type:

```cpp
class Worker : public beast::LockFreeStack<Worker>::Node,
               public beast::LockFreeStack<Worker, PausedTag>::Node
```

`m_everyone` (untagged) tracks every worker ever created; `m_paused` (tagged `PausedTag`) tracks only those currently dormant. This two-stack pattern enables `deleteWorkers` to iterate all workers at teardown and `setNumberOfThreads` to pop reusable paused workers for cheap reactivation, all with lock-free compare-exchange semantics. The ABA hazard normally associated with lock-free stacks is avoided here because popped workers are either immediately re-pushed by the worker thread itself (into `m_paused`) or deleted — they are never re-pushed into a stack they just left without a full lifecycle transition.

## `stop()` and the Double-Barrier Shutdown

`stop()` calls `setNumberOfThreads(0)` to issue pause signals for every active thread, then waits on `m_cv` under `m_mut` for the predicate `m_allPaused && numberOfCurrentlyRunningTasks() == 0`. Both conditions are necessary and distinct. `m_allPaused` is set under `m_mut` by the last worker to leave the active loop, so the condition variable wait never misses it. However, `m_runningTaskCount` is an atomic updated *outside* `m_mut`; a worker could decrement `m_activeCount` to zero while still inside the tail of `processTask`. The combined predicate prevents `stop()` from returning while a task is still running. The task completion path also acquires `m_mut` before `notify_all()`, serialising against the predicate evaluation inside `stop()`'s `cv.wait()` and eliminating the lost-wakeup race.

The destructor then calls `deleteWorkers(m_everyone)`, which pops each `Worker` from the untagged stack and `delete`s it. `~Worker` sets `shouldExit_ = true`, increments `wakeCount_`, signals `wakeup_`, then joins the thread — a clean blocking teardown for each worker.

## Thread Safety Notes

`addTask()` is explicitly documented as thread-safe (it only calls `m_semaphore.notify()`, which is internally locked). `getNumberOfThreads()`, `setNumberOfThreads()`, and `stop()` are *not* thread-safe and must be called from a single controlling thread. `numberOfCurrentlyRunningTasks()` is a bare atomic load and is safe but advisory only, as the comment warns.

The thread name is reset at the top of every iteration of the worker's inner loop via `beast::setCurrentThreadName(threadName_)`, because the callback is permitted to change it for debugging purposes. Paused threads rename themselves `"(ThreadName)"` to make them visually distinct in debugger thread listings.