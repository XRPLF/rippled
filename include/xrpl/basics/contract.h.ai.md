# `include/xrpl/basics/contract.h` — Programming by Contract Utilities

This header implements the XRPL ledger's Programming by Contract (DbC) discipline — a small but critical set of primitives for handling precondition failures, invariant violations, and structured exception throwing throughout the codebase. Rather than leaving each subsystem to throw exceptions ad-hoc, `contract.h` centralises the pattern so that every exception path produces a log entry, and every logic error terminates predictably.

## The Two Failure Modes

The file cleanly distinguishes two fundamentally different failure categories:

**Recoverable runtime errors** are handled by `Throw<E>` and `Rethrow`. These represent conditions the caller is expected to handle — malformed data, failed I/O, invalid configurations. The exception propagates up the stack normally, and callers can `catch` and recover.

**Unrecoverable logic errors** are handled by `LogicError`. These represent violated invariants — bugs in the program itself, not unexpected input. `LogicError` is declared `noexcept` and ends with `std::abort()`, signalling that there is no safe recovery path.

## `Throw<E>` — Logged Exception Throwing

```cpp
template <class E, class... Args>
[[noreturn]] XRPL_NO_SANITIZE_ADDRESS inline void
Throw(Args&&... args)
```

`Throw<E>` is the standard mechanism for raising exceptions across the XRPL codebase. It constructs the exception object, logs a warning that includes the exception's demangled type name (via `beast::type_name<E>()`) and its `what()` message, and then throws by move. The `static_assert` enforces that `E` derives from `std::exception`, preventing careless use of non-standard exception types that would bypass catch-all handlers expecting `std::exception&`.

The logging before the throw is the key design choice here. Because exceptions can be silently swallowed by broad `catch(...)` handlers or can propagate across subsystem boundaries, having an unconditional warning log at the throw site creates a reliable audit trail even if no handler ever logs the caught exception. In practice, callers across `nodestore`, `json`, and `net` subsystems use this pattern:

```cpp
Throw<std::runtime_error>("lz4_decompress: integer overflow (input)");
xrpl::Throw<Json::error>(message);
```

## `Rethrow` — Logged Rethrow

`Rethrow` wraps the bare `throw;` statement, prepending a warning log entry. The comment in the source is honest about why this exists: `throw;` inside a catch block re-raises the active exception, but this is transparent to logging infrastructure. `Rethrow` makes the re-throw visible in log output, which is valuable when tracking exception propagation chains in production.

## `LogThrow` — The Logging Primitive

`LogThrow(title)` is the shared sink called by both `Throw<E>` and `Rethrow`. The implementation (in `contract.cpp`) routes to `JLOG(debugLog().warn())`, feeding into the structured ledger logging framework. It takes a human-readable title string — in `Throw<E>`, this is assembled as `"Throwing exception of type <typename>: <what()>"`.

## `LogicError` — Unrecoverable Invariant Violations

```cpp
[[noreturn]] void LogicError(std::string const& how) noexcept;
```

`LogicError` is a terminate path. Its implementation logs at `fatal` severity, writes directly to `std::cerr` as a belt-and-suspenders safeguard, fires the `UNREACHABLE` instrumentation macro (which is `assert(false)` in debug builds and a no-op in release/fuzzing mode via the Antithesis SDK integration), and then calls `std::abort()`. The entire body is wrapped in `// LCOV_EXCL_START/STOP` because correctly-operating code should never reach it — coverage tools would flag it as untested dead code without the exclusion.

The `noexcept` marker is meaningful: a function signalling a logic error must not throw, because it's called from sites that are already in an undefined or corrupted state. `noexcept` also helps the compiler understand this is a hard termination point.

The comment in `contract.cpp` explains a deliberate naming convention: `UNREACHABLE("LogicError", {{"message", s}})` is the only callsite that passes a dynamic message parameter to the instrumentation macro, because `LogicError` is a convergence point for many different unrelated execution paths — unlike the named per-feature `XRPL_ASSERT` calls elsewhere in the codebase.

## ASAN Suppression

Both `Throw<E>` and `Rethrow` are annotated with `XRPL_NO_SANITIZE_ADDRESS`, defined in `sanitizers.h` as `__attribute__((no_sanitize("address", "hwaddress")))` on GCC/Clang. Address Sanitizer tracks memory state through normal control flow but poorly handles the non-local jumps caused by C++ exceptions — the unwinding path can trigger false positive stack-use-after-scope or heap-use-after-free reports. The annotation suppresses instrumentation for these specific functions without disabling ASAN globally, keeping sanitizer coverage intact everywhere exceptions are not thrown.

## Design Rationale

The primitive set is deliberately minimal. There is no `Precondition()` or `Postcondition()` macro — callers simply call `Throw<E>` at the point of detected violation, with a descriptive message. This keeps the abstraction thin while providing the essential guarantees: every exception is logged before it leaves its origin, type safety is statically enforced, and logic errors terminate loudly rather than silently corrupting state.