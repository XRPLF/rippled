# `yield_to.h` — Coroutine Test Harness Mix-in

## Role and Problem Statement

Testing asynchronous Boost.Asio code presents a structural challenge: async operations expect to run inside an `io_context` event loop, but unit test functions are synchronous entry points. Naively spinning up an `io_context` per test, manually posting work, and waiting for results produces boilerplate that obscures intent and is error-prone to write correctly.

`enable_yield_to` solves this by providing a reusable mix-in base class that encapsulates the `io_context`, a thread pool, and a synchronization barrier. Test classes derive from it and call `yield_to()` to launch one or more async test functions — each wrapped in a Boost.Asio stackful coroutine — then block until all have completed. The result is test code that reads sequentially despite driving genuinely asynchronous operations. Both `io_latency_probe_test` and `ServerStatus_test` use this pattern, inheriting from both `beast::unit_test::suite` and `beast::test::enable_yield_to` simultaneously.

## Class Design

`enable_yield_to` is intentionally a protected-state mix-in rather than a standalone fixture helper. `ios_` is `protected` so derived test classes can post their own work or pass it directly to the components under test without requiring an accessor. The remaining synchronization state (`work_`, `threads_`, `m_`, `cv_`, `running_`) is `private`, enforcing that subclasses interact only through the `yield_to()` interface and `get_io_context()`.

The constructor accepts a `concurrency` parameter (defaulting to 1) and immediately starts that many threads running `ios_.run()`. An `executor_work_guard` is installed on construction to prevent the `io_context` from returning prematurely before any work is posted. The destructor tears this down cleanly: resetting the `work_` guard (via `boost::none`) allows the `io_context` to drain and the threads to exit naturally, after which they are `join()`ed. This is the canonical Asio shutdown pattern, and using `boost::optional` for the guard makes the reset explicit without requiring a heap allocation.

## The `yield_to` / `spawn` Split

`yield_to()` is the public entry point. It takes one or more callable objects, sets `running_` to their count, recursively posts all of them via `spawn()`, then blocks on the condition variable until `running_` reaches zero.

`spawn()` uses a recursive variadic template to peel callables one at a time, posting each to `ios_` via `boost::asio::spawn`. The base-case overload (empty parameter pack, no-op body) terminates the recursion. This pattern avoids the need for an intermediate container of type-erased callables — each lambda captures its specific callable by reference and is posted directly.

Each spawned coroutine body calls `f(yield)`, then decrements `running_` under the mutex and notifies the condition variable if it reaches zero. This is the only cross-thread synchronization point: `yield_to()` blocks on the calling thread while all coroutines execute on the `io_context` threads.

## Stack Sizing Decision

Each coroutine is allocated a fixed 2 MB stack via `boost::context::fixedsize_stack(2 * 1024 * 1024)`. This is a deliberate tradeoff: the default segmented or pooled stack allocators can be tricky to tune, and test coroutines often call deeply into the system under test. 2 MB is generous enough to avoid stack overflow in almost any test scenario at the cost of higher virtual memory usage. Since this is a test utility and coroutine counts are small, the cost is acceptable.

## Exception Handling

The completion token passed to `boost::asio::spawn` includes an exception handler lambda that rethrows any non-null `std::exception_ptr`. This ensures that exceptions thrown inside a coroutine — such as a failing assertion — propagate out and cause the test to fail visibly, rather than being silently swallowed by the Asio machinery. Without this handler, exceptions from spawned coroutines would be lost entirely.

## Concurrency Considerations

`running_` is written before `spawn()` is called and before any coroutine can complete, so there is no race between the initial write and coroutine decrements. All decrements happen under `m_`, and the condition variable check in `yield_to()` holds the lock, making the termination detection race-free. However, `yield_to()` itself is not reentrant — calling it from multiple threads simultaneously would produce undefined behavior on `running_`. This is acceptable because test fixtures are normally driven from a single test thread.

The `yield_context` type alias re-exported as a public member allows consuming test files to accept `enable_yield_to::yield_context` as the coroutine argument type without directly including Boost.Asio coroutine headers, keeping per-test includes minimal.