# `BasicApp.h` — IO Context Lifecycle Foundation

`BasicApp` is a small but architecturally critical base class that owns the `boost::asio::io_context` and its backing thread pool. Its entire purpose is captured in the source comment: *"This is so that the io_context can outlive all the children."* Everything else in the XRPL node application that performs asynchronous I/O — network connections, timers, async jobs — posts work onto this context and assumes it remains live throughout their own lifetimes.

## Lifetime Ordering via Inheritance

The key design decision is that `BasicApp` is used as a **base class**, not a member variable. In `Application.cpp`, `ApplicationImp` is declared as:

```cpp
class ApplicationImp : public Application, public BasicApp
```

C++ guarantees that base class subobjects are constructed before derived class members and destroyed after them. Because `BasicApp` appears in the inheritance list, the `io_context_` it owns is initialized before any `ApplicationImp` member, and crucially, `BasicApp::~BasicApp()` runs after all of `ApplicationImp`'s own members have been destroyed. This bracketing guarantees that any subsystem component (ledger store, peer manager, job queue, etc.) can safely dispatch to the `io_context_` at any point during its own destruction without touching a dangling context.

If the `io_context_` were instead held as a member of `ApplicationImp`, its destruction order relative to other members would be positional and fragile — a refactor could silently break the ordering guarantee. Embedding it in a base class makes the constraint part of the type system.

## The Work Guard Pattern

The `work_` member is a `std::optional<boost::asio::executor_work_guard<...>>`. The Boost.Asio `executor_work_guard` prevents an `io_context` from exiting `run()` when it has no pending handlers. Without it, threads calling `io_context_.run()` would return immediately if the work queue momentarily emptied, making the thread pool fragile during startup or quiet periods.

Wrapping it in `std::optional` is the idiomatic mechanism for controlled release: `work_.reset()` destroys the guard in `~BasicApp()`, signaling the `io_context` that it is free to stop once all outstanding handlers drain. The destructor then joins each worker thread, waiting for clean completion before the `io_context_` object itself goes out of scope.

## Thread Pool Construction

In the constructor, threads are launched eagerly — all `numberOfThreads` are spawned immediately and each calls `io_context_.run()`, blocking until the context stops. Each thread is named `"io svc #N"` via `beast::setCurrentThreadName()`, which aids in debugging by making threads identifiable in profilers and core dumps. The number of threads is reported by `get_number_of_threads()`, primarily used by subsystems that want to reason about parallelism capacity.

## Destructor Sequence

The destructor follows a strict two-phase shutdown:

1. `work_.reset()` — drops the work guard, unblocking `io_context_.run()` once pending handlers finish.
2. `for (auto& t : threads_) t.join()` — waits for every thread to exit before returning.

This ordering is essential. Resetting the work guard without joining would allow the `BasicApp` destructor to return while threads are still executing inside the `io_context_`, which would then be destroyed, producing undefined behavior. The join ensures that the context's internal state and all in-flight handlers complete before the destructor exits.

## Interface Surface

The public API is minimal by design: `get_io_context()` returns a reference to the `io_context_` so derived classes and collaborators can post work, and `get_number_of_threads()` exposes thread count for informational purposes. There is no mechanism to resize the pool at runtime — the thread count is fixed at construction, which is appropriate for a server process where the concurrency budget is determined at startup from configuration.