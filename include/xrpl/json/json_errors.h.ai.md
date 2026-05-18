# `json_errors.h` — JSON Exception Type

This header is the single point of definition for `Json::error`, the exception type thrown throughout the XRPL JSON subsystem whenever a structural or semantic constraint on JSON data is violated.

## Role in the JSON Subsystem

The file is deliberately minimal: one `struct`, one inheritance line, no other declarations. Its purpose is to give the JSON layer a *named* exception type that is distinct from the broader `std::runtime_error` hierarchy. Without this separation, callers could not selectively catch JSON-specific failures without also catching every other runtime error the system might raise.

`Json::error` inherits from `std::runtime_error` via the `using` constructor declaration, meaning it accepts any argument that `std::runtime_error` accepts — most commonly a `const char*` or `std::string` error message — with no additional state or interface of its own.

## How It Is Thrown

`json_errors.h` is not used directly at throw sites. Instead, `detail/json_assert.h` wraps it in the `JSON_ASSERT_MESSAGE` macro:

```cpp
#define JSON_ASSERT_MESSAGE(condition, message) \
    if (!(condition))                           \
        xrpl::Throw<Json::error>(message);
```

`xrpl::Throw<E>` (from `include/xrpl/basics/contract.h`) is the project-wide logging throw helper. Before re-raising the exception it calls `LogThrow`, which records a call stack. This means every `Json::error` automatically produces a diagnostic trace in the logs — a valuable property for a type that surfaces programmer errors (violated JSON invariants) rather than purely user-input errors.

## How It Is Caught

At the RPC boundary in `src/xrpld/rpc/handlers/ledger/LedgerEntry.cpp`, `Json::error` is caught explicitly to convert it into a well-formed RPC error response:

```cpp
catch (Json::error& e)
{
    if (context.apiVersion > 1u)
    { ... }
}
```

This catch block is version-aware, reflecting the fact that API v1 and v2+ have different error-surface contracts. The named exception type is what makes this selective handling possible — a plain `std::runtime_error` catch would sweep up far too much.

## Design Rationale

The `struct` keyword (rather than `class`) and the single `using` declaration are intentional minimalism. There is no need for extra fields or methods because the error message string carried by `std::runtime_error::what()` is sufficient for all current throw sites. Keeping the type lean also means it remains trivially copyable into `std::exception_ptr` contexts and imposes no ABI friction when propagating across translation-unit boundaries.

The placement of this definition in the `Json` namespace mirrors the broader project convention of scoping domain-specific exceptions close to the subsystem that raises them, rather than in a global error registry.