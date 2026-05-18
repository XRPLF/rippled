# `include/xrpl/basics/scope.h`

This header provides four RAII scope-guard utilities for the `xrpl` namespace: `scope_exit`, `scope_fail`, `scope_success`, and `scope_unlock`. The first three follow the design specified in the C++ Library Fundamentals TS v3 (N4873, §[scopeguard]), giving XRPL the same ergonomics that the standard committee intended for a future `<scope>` header but without waiting for compiler support. `scope_unlock` is an independent addition that solves a recurring pattern in the ledger's concurrency model.

## The Three Scope Guard Templates

All three share a common structure: they wrap a callable `EF`, an `execute_on_destruction_` flag, and (for `scope_fail` and `scope_success`) a snapshot of `std::uncaught_exceptions()` taken at construction time. The difference lies entirely in the condition evaluated in each destructor.

**`scope_exit`** is the unconditional guard. Its destructor calls the stored functor whenever `execute_on_destruction_` is `true`, regardless of whether the scope is leaving normally or due to an exception. This is the go-to tool for "always clean up this resource" situations — a direct replacement for wrapping teardown code in a dedicated RAII type. The `CheckCash` transactor uses it precisely this way: after temporarily tweaking a trust-line limit during a payment, a `scope_exit` guarantees the original value is restored before the function returns, even if an error path is taken mid-function.

**`scope_fail`** fires only when the scope is unwinding due to an exception. It compares `std::uncaught_exceptions()` at destruction against the value snapshotted at construction; if the count has grown, an exception is in flight. This is the "rollback on failure" idiom: register compensating actions at the top of an operation, and they will run automatically if anything throws before completion.

**`scope_success`** is the mirror of `scope_fail`. It fires only when `std::uncaught_exceptions()` has *not* grown since construction — the scope exited cleanly. Because its exit function only runs on the happy path, the destructor is conditionally `noexcept(noexcept(exit_function_()))`, propagating the callable's exception specification correctly. Unlike `scope_exit` and `scope_fail`, the constructor of `scope_success` does not force `noexcept` construction through `static_assert`, since the implementation already handles non-noexcept construction via its `noexcept(...)` specifier on the constructor itself.

### The Deviation from the TS Specification

The spec's constructors for `scope_exit` and `scope_fail` contain a `try/catch` block to handle the case where the functor's construction (not invocation) throws. In practice, almost all callers pass a lambda literal, making those constructors trivially noexcept. Several compilers flagged the try/catch as superfluous in these cases. The implementation resolves this by marking the constructors `noexcept` unconditionally and substituting a `static_assert` on `std::is_nothrow_constructible_v<EF, ...>`. The effect is identical — throwing constructors are rejected — but via a compile-time diagnostic rather than a runtime branch.

### Move Semantics and `release()`

All three scope guards are move-constructible but not move-assignable. The move constructor forwards the functor and copies the `execute_on_destruction_` flag, then calls `rhs.release()` to disarm the source. This makes ownership transfer unambiguous: exactly one live instance holds responsibility for running the functor. Calling `release()` directly sets `execute_on_destruction_ = false`, cancelling the deferred action — useful when a resource has been successfully handed off and cleanup is no longer needed.

CTAD guides (`scope_exit(EF) -> scope_exit<EF>`, etc.) allow clean brace-initialization without spelling out the template parameter.

## `scope_unlock`

This template solves a specific problem in XRPL's mutex-heavy ledger management code: a function holds a `std::unique_lock` for most of its work but must temporarily release it for a blocking call or an outward-facing callback, then re-acquire it before continuing. Without a RAII wrapper, every early-return path must remember to re-lock before leaving.

`scope_unlock` inverts the conventional lock-guard contract. Construction immediately calls `plock->unlock()` (after asserting via `XRPL_ASSERT` that the lock is owned), and the destructor calls `plock->lock()`. The mutex is held for exactly the surrounding scope minus the inner block where `scope_unlock` lives. `LedgerMaster` uses this repeatedly — temporarily releasing its recursive mutex while publishing ledgers or fetching history — and `InboundLedgers` uses it to release a lock before calling `acquire()`.

Unlike the three scope guards above, `scope_unlock` is intentionally immovable: both the copy constructor and copy-assignment operator are deleted, and there is no move constructor. The semantics of transferring "who will re-lock this mutex" are too error-prone to support safely, so the type is tied to the scope in which it is created.

The `XRPL_ASSERT` at construction time (`plock->owns_lock()`) acts as a defensive invariant check: calling `scope_unlock` on an already-unlocked `unique_lock` would double-unlock the mutex, a precondition violation that the assertion catches in debug builds.