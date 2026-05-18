# `src/libxrpl/basics/contract.cpp`

This file is the implementation half of the XRPL "programming by contract" facility. It provides two concrete functions — `LogThrow` and `LogicError` — that form the runtime backbone for all contract-violation handling across the ledger codebase. The companion header `contract.h` builds on these with the inline `Throw<E>()` template and `Rethrow()`, but those functions delegate their logging to `LogThrow` defined here.

## Role in the Contract System

The header `contract.h` defines three layers of contract violation:

- **Recoverable throws** — `Throw<E>(args...)` constructs an exception, calls `LogThrow` to journal it, then throws.
- **Rethrows** — `Rethrow()` calls `LogThrow` with a fixed message, then re-throws the active exception.
- **Unrecoverable logic errors** — `LogicError(s)` terminates the process.

The `.cpp` file supplies the two non-inline implementations, keeping the instrumentation details out of every translation unit that includes the header.

## `LogThrow`

```cpp
void LogThrow(std::string const& title)
{
    JLOG(debugLog().warn()) << title;
}
```

This function's job is deliberately narrow: emit a warning-level journal entry immediately before an exception propagates. It does not inspect or rethrow anything. `debugLog()` returns a `beast::Journal` that drains to a debug sink (which may be a null sink if logging hasn't been configured). The header comment on `debugLog()` explicitly notes this — the journal's output "may never be seen." `LogThrow` is therefore not a reliable audit trail; it is a best-effort diagnostic hint in development and staging environments. For throw paths where the message must be preserved, callers use the exception object itself.

The ASAN annotation `XRPL_NO_SANITIZE_ADDRESS` lives on the callers in the header, not here, because this function does not itself perform the control-flow jump.

## `LogicError`

```cpp
[[noreturn]] void LogicError(std::string const& s) noexcept
{
    JLOG(debugLog().fatal()) << s;
    std::cerr << "Logic error: " << s << std::endl;
    UNREACHABLE("LogicError", {{"message", s}});
    std::abort();
}
```

`LogicError` is called when a broken invariant has been detected and recovery is impossible — situations where continuing would risk data corruption or incorrect ledger state. Several design choices here are deliberate:

**Dual output channels.** The function writes to both the XRPL journal (fatal level) and directly to `std::cerr`. This matters because the journal subsystem may itself be in a bad state, or may not have been initialized when a logic error fires during startup or shutdown. `std::cerr` is an unbuffered channel that will survive almost any application-level failure, ensuring the message reaches an operator even when logging infrastructure cannot.

**`noexcept` on a `[[noreturn]]` function.** Marking `LogicError` as `noexcept` communicates to the compiler and to callers that this function will never propagate an exception — it only terminates. This is consistent with its purpose (catching broken invariants) and prevents callers from being tempted to wrap it in a try/catch.

**`UNREACHABLE` before `std::abort()`.** The `UNREACHABLE` macro (from `instrumentation.h`) expands to `assert("LogicError" && false)` in non-Antithesis builds. In a debug build this immediately triggers the assert handler, giving debuggers a clean crash point with a readable message. In a release build the assert is a no-op, so `std::abort()` on the next line is the actual termination mechanism — providing a guaranteed crash regardless of build mode. This layered approach means debug builds surface the error through familiar assert machinery, while release builds still produce an `SIGABRT` that crash-reporting infrastructure can capture.

**Special `UNREACHABLE` naming.** The in-code comment explains why the contract is named `"LogicError"` (without a namespace qualifier) rather than following the normal fully-qualified naming convention. `LogicError` is the single convergence point for many unrelated execution paths across the codebase — `SHAMap`, `LedgerCleaner`, `Application`, and others all call it. Using a plain name without namespace lets this contract stand out in instrumentation telemetry as a cross-cutting terminal condition rather than being attributed to one subsystem.

**LCOV exclusion.** The entire function body is wrapped in `LCOV_EXCL_START` / `LCOV_EXCL_STOP`. This is correct: lines that are only reached when the program is about to die cannot be exercised by ordinary unit tests, and attempting to cover them would require instrumenting process-abort behavior. The exclusion keeps coverage metrics honest.

## Relationship to Instrumentation

When the codebase is compiled with `ENABLE_VOIDSTAR` (the Antithesis fuzzing platform), `UNREACHABLE` is replaced by the real `antithesis_sdk.h` macro, which reports the violation to the fuzzer's fault-injection framework rather than aborting immediately. This allows the fuzzer to observe and learn from logic-error paths without crashing its harness. The `std::abort()` that follows remains as a hard stop after the fuzzer has had its chance to record the event.

## Usage Pattern

Callers invoke `LogicError` at sites where a condition "can't happen" by design but the code cannot statically prove it — for example, `LedgerHolder` uses it if an uninitialized ledger reference is dereferenced, and `SHAMap` uses it when internal node accounting becomes inconsistent. It is never called in error-handling paths that are expected to trigger under normal load.