# `src/xrpld/rpc/detail/Status.cpp`

This file contains the method implementations for `RPC::Status`, the unified error-result type for XRPL's RPC layer. The header (`Status.h`) declares the class and its constructors; this translation unit provides all the non-trivial method bodies that convert internal status state into human-readable strings and JSON output.

## Context: Why `Status` Exists

XRPL's RPC layer sits atop two separate legacy error code systems. `TER` (Transaction Engine Result) codes cover outcomes like `tesSUCCESS` and `tecNO_DST_INSUF_XRP`, expressed as an enum with a range spanning several hundred values. `error_code_i` covers RPC-level failures like `rpcINVALID_PARAMS` or `rpcNO_NETWORK`. Neither type is directly interchangeable. `Status` bridges both under a single type that can be uniformly tested, formatted, and serialised — including a third fallback mode (`Type::none`) for raw integer codes that belong to neither domain.

## `codeString()`: Type-Discriminated Dispatch

The central method is `codeString()`. Its structure mirrors a tagged union dispatch: it checks `type_` sequentially and delegates to the appropriate lookup function for each code space.

For `Type::none`, it simply converts `code_` with `std::to_string`, making this the escape hatch for callers who pass bare integers at construction.

For `Type::TER`, it calls `transResultInfo(toTER(), s1, s2)`, which fills two strings with the TER token (e.g., `"tecNO_DST_INSUF_XRP"`) and its human-readable description. The return value is captured with `[[maybe_unused]]` but immediately checked via `XRPL_ASSERT`. This pattern acknowledges that in release builds the assertion compiles away, yet the call is still needed for its output arguments `s1` and `s2`. The assert enforces an invariant rather than handling a runtime condition: if a `Status` was constructed from a `TER` value, `transResultInfo` *must* succeed, so failure here means the object was constructed incorrectly.

For `Type::error_code_i`, it calls `get_error_info(toErrorCode())` to retrieve the `ErrorInfo` struct, then formats `token: message` using an `ostringstream`. The use of `.c_str()` on the returned strings is defensive — the underlying `ErrorInfo` fields are C-string-backed string views in some implementations.

After all three branches, `UNREACHABLE` terminates any code path representing an unknown `type_` value, wrapped in `LCOV_EXCL_START`/`STOP` directives. This tells the coverage toolchain to ignore the dead branch explicitly, documenting the design intent: this path should never execute in a well-formed program. The `[[maybe_unused]]`/`XRPL_ASSERT`/`UNREACHABLE` pattern across the TER branch demonstrates a consistent defensive stance — assert invariants in debug, exclude unreachable paths from coverage metrics, never silently produce garbage.

## `fillJson()`: JSON-RPC 2.0 Error Object

`fillJson()` populates a `Json::Value` with a structured `error` sub-object adhering to the JSON-RPC 2.0 error object shape. The fields written are `error.code` (the raw integer `code_`) and `error.message` (the result of `codeString()`). If `messages_` is non-empty, an additional `error.data` array is appended, providing supplemental diagnostic strings beyond what the base code conveys.

The early-return guard `if (!*this)` (equivalent to `if (code_ == OK)`) means callers can invoke `fillJson()` unconditionally — no-op on success, error population on failure. The header comment notes this method is "not currently used," which is architecturally notable: it represents a more spec-compliant error serialisation path than the `inject()` method (which calls `inject_error` from the `ErrorCodes` infrastructure), preserved as a future migration target toward proper JSON-RPC 2.0 conformance.

## `message()` and `toString()`

`message()` concatenates `messages_` with `/` delimiters, making the vector of freeform strings into a single diagnostic string. The `/` separator is a simple convention rather than structured formatting.

`toString()` is the composite diagnostic representation: `codeString() + ":" + message()`. If the `Status` is OK (`!*this`), it returns an empty string immediately, consistent with the convention that a falsy `Status` produces no output from any serialisation method.

## Design Cohesion

The `operator bool()` defined inline in the header (`return code_ != OK`) is the pivotal invariant that all three output methods respect. Because `code_ == 0` means success across all three error domains (TER's `tesSUCCESS` is 0, `rpcSUCCESS` is 0, and raw integer 0 is the conventional success sentinel), this single comparison correctly short-circuits output for successful statuses regardless of `type_`. The uniformity is intentional and carefully preserved in every method body here.