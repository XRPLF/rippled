# `include/xrpl/json/detail/json_assert.h`

This tiny header defines a single macro that serves as the JSON subsystem's internal assertion mechanism. Its entire purpose is to bridge failed runtime conditions in JSON parsing and value-access code into XRPL's structured exception-throwing infrastructure.

## What It Does

```cpp
#define JSON_ASSERT_MESSAGE(condition, message) \
    if (!(condition))                           \
        xrpl::Throw<Json::error>(message);
```

When `condition` evaluates to false, the macro calls `xrpl::Throw<Json::error>(message)`, which constructs and throws a `Json::error` exception — a thin `std::runtime_error` subtype defined in `json_errors.h`.

## Why This Design

The macro is intentionally narrow in scope. Rather than using a raw `throw` or a standard `assert()` (which would abort the process in debug builds and do nothing in release), this approach routes all JSON invariant failures through `xrpl::Throw<E>`, the codebase's canonical exception-dispatching function. `xrpl::Throw` performs two things before throwing: it calls `LogThrow()` to log a call stack, and it constructs the exception with perfect-forwarded arguments. This means every failed JSON assertion produces a log entry with a stack trace — critical for diagnosing malformed or unexpected JSON values in a live ledger node.

Throwing `Json::error` specifically (rather than a generic `std::runtime_error`) lets callers catch JSON failures distinctly from other XRPL runtime errors, enabling fine-grained error handling at API and RPC boundaries.

## Usage Context

In practice the macro appears exclusively in `src/libxrpl/json/json_value.cpp`, the implementation of the `Json::Value` type-conversion machinery. Every type accessor — `asString()`, `asInt()`, `asUInt()`, `asDouble()` — guards its type-dispatch branches with `JSON_ASSERT_MESSAGE`. For example, requesting an integer representation from a JSON value whose internal tag is neither integer, real, nor string triggers `JSON_ASSERT_MESSAGE(false, "Type is not convertible to int")`. This makes type mismatch errors immediately visible and attributable rather than silently producing garbage values.

## Relationship to `detail/`

The file lives in the `detail/` subdirectory, signaling it is an implementation concern not intended for direct inclusion by consumers of the public JSON API. The `detail/` convention in this codebase isolates internal helpers that could change without breaking the public interface contract.