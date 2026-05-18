# BasicApp.cpp — `io_context` Thread Pool Bootstrap

`BasicApp` exists to solve a C++ object lifetime problem that arises throughout the rippled application: many subsystems post work to Boost.Asio's `io_context`, and those subsystems must be guaranteed to complete their async operations and be fully destroyed *before* the `io_context` and its driving threads are torn down. The class is deliberately minimal — just 27 lines of implementation — so that all the lifetime guarantees derive from straightforward RAII and C++ construction/destruction ordering rather than from any complex shutdown protocol.

## What the Class Owns

Three members, declared in this exact order:

```
std::optional<executor_work_guard<...>> work_;
std::vector<std::thread>               threads_;
boost::asio::io_context                io_context_;
```

The ordering is not coincidental. C++ destroys members in reverse declaration order, so `io_context_` is the last member to be destroyed. But the real lifetime guarantee comes from the inheritance relationship: `ApplicationImp` (in `Application.cpp`) inherits from `BasicApp`. Because base-class destructors run *after* the derived class destructor, every member of `ApplicationImp` — the job queue, the ledger master, the network peers, and hundreds of other objects that hold references to `io_context_` — is torn down before `BasicApp`'s destructor ever fires. The header comment documents this intent explicitly: *"This is so that the io_context can outlive all the children."*

## Construction: Work Guard and Thread Pool

The constructor first installs a `boost::asio::executor_work_guard` via `work_.emplace(...)`. Without this guard, `io_context::run()` would return immediately if no handlers are pending, causing every thread to exit before any real work has been scheduled. The work guard keeps the run loop alive unconditionally until the guard itself is released.

With the guard in place, the constructor spins up `numberOfThreads` threads, each calling `io_context_.run()`. Boost.Asio's `io_context` is thread-safe for concurrent `run()` calls, so all threads participate in dispatching handlers from the same shared queue. The thread count is determined by `ApplicationImp`'s constructor calling a local `numberOfThreads()` heuristic, which returns 1 on single- or dual-core machines (or when the node is configured for minimal resources) and 6 otherwise.

Each thread names itself `"io svc #N"` using `beast::setCurrentThreadName()`. The name index comes from the post-decrement loop variable, so threads are numbered from `N-1` down to `0`. This is a diagnostic convenience: crash dumps, `top`, or `htop` output will label these threads clearly rather than showing a generic process name.

## Destruction: Draining the Pool

The destructor has a precise two-step sequence:

1. `work_.reset()` — destroying the `optional` drops the work guard. The `io_context` is now free to return from `run()` once all remaining queued handlers complete.
2. `for (auto& t : threads_) t.join()` — blocks until every thread has exited its `run()` loop.

The key invariant is that by the time `~BasicApp()` is called, all derived-class members have already been destroyed (because `ApplicationImp`'s destructor ran first). Any async operations those members may have posted will have already been cancelled or completed. Releasing the work guard therefore allows the threads to drain cleanly and exit without missing in-flight work.

If the work guard were not used — or if the destructor only called `io_context_.stop()` instead — there would be a risk of handlers being abandoned mid-execution. The chosen approach is the safe path: work drains naturally, then threads join.

## Role in the Larger Application

`ApplicationImp` inherits `BasicApp` as its first base class and passes a computed thread count at the top of its initializer list, before any other members are initialized. This guarantees the thread pool is running before any subsystem tries to post work. The `get_io_context()` accessor exposes the `io_context` reference to the rest of the application, and `get_number_of_threads()` lets callers — such as the job queue configuration — query pool size at runtime.

The design is a clean separation of concerns: `BasicApp` handles exactly the problem of owning and safely shutting down an `io_context`-backed thread pool; everything else is `ApplicationImp`'s responsibility.