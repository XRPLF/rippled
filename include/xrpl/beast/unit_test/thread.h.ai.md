# `include/xrpl/beast/unit_test/thread.h`

This file provides `beast::unit_test::thread`, a thin wrapper around `std::thread` that bridges C++ thread semantics with the Beast unit testing framework's pass/fail reporting and abort propagation machinery.

## Why This Exists

The standard `std::thread` has a fatal flaw in test contexts: if an exception escapes a thread function, `std::terminate` is called. The unit test framework uses exceptions — specifically `suite::abort_exception` — as a control-flow mechanism to short-circuit a failing test suite when `abort_on_fail` is requested. A raw `std::thread` running test logic would crash the process on any unhandled exception rather than recording a test failure. `beast::unit_test::thread` solves both problems: it converts unexpected exceptions into test failures and silently absorbs deliberate abort signals.

## Exception Handling in `run()`

The core logic is the private `run()` method, which serves as the actual thread entry point. It wraps the user-supplied callable in a three-clause catch block:

1. **`suite::abort_exception`** — caught and silently discarded. This exception is thrown internally by `suite::fail()` when the suite is configured with `abort_on_fail`. The fact that it reaches the thread boundary means the abort has already been recorded; eating the exception here prevents `std::terminate` from being called.

2. **`std::exception`** — caught and forwarded to `s_->fail()`, which records a test failure in the runner with the exception message prefixed by `"unhandled exception: "`.

3. **Catch-all (`...`)** — handles any other thrown object with a generic `"unhandled exception"` failure message.

This mirrors the exception-handling pattern used in `suite::run(runner&)` itself, maintaining consistent failure semantics whether test code runs on the main suite thread or on a helper thread.

## Abort Propagation Across Thread Boundaries

The subtler design concern is handled in `join()`. After `t_.join()` completes, it immediately calls `s_->propagate_abort()`. This method checks whether the suite is in `abort_on_fail` mode and has already recorded an abort, and if so, re-throws `abort_exception` on the calling thread.

This two-step dance is necessary because an abort originating in the worker thread is consumed by `run()` to avoid termination. The abort state (`aborted_` flag) is preserved on the `suite` object, and `propagate_abort()` on `join()` restores the abort signal to the parent thread. Without this, a test suite with `abort_on_fail` could silently continue after a worker thread failure rather than stopping the suite as intended.

## Relationship to `suite`

The `thread` class is a `friend` of `suite`, granting it access to `abort_exception` (a private nested struct) and `propagate_abort()` (a private method). `suite.h` forward-declares `class thread` before the `suite` class definition, and `thread.h` includes `suite.h`, creating a clean one-way dependency.

The `s_` member is a raw non-owning pointer to the `suite` that launched the thread. This is safe because unit test threads are expected to be `join()`ed before the suite object is destroyed — a contract enforced by convention in test code rather than by any RAII mechanism here. Calling `join()` on a `thread` whose parent `suite` has already been destroyed would be undefined behavior, but this mirrors the same lifetime discipline required of `std::thread` with respect to its arguments.

## Interface Fidelity

The class deliberately mirrors the `std::thread` interface: it exposes `joinable()`, `get_id()`, `hardware_concurrency()`, `detach()`, and `swap()` as pass-throughs to the underlying `t_` member. Copy construction and assignment are deleted (as with `std::thread`); only move semantics are supported. This allows `beast::unit_test::thread` to act as a drop-in replacement for `std::thread` in test code with minimal friction, requiring only that the `suite` reference be supplied as an additional first constructor argument.