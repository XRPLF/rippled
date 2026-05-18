# `include/xrpl/protocol/ErrorCodes.h`

## Role in the System

This header is the single source of truth for every RPC error the XRPL node can emit. It defines the stable numeric code space (`error_code_i`), a parallel warning code space (`warning_code_i`), a structure that binds each code to a human-readable token and HTTP status, and the entire vocabulary of helper functions that RPC handlers use to produce well-formed JSON error objects. Every component that rejects an RPC call — from malformed parameter checks to ledger-not-found conditions — funnels through this file.

## The `error_code_i` Enum and Stability Guarantee

The enum runs from `rpcBAD_SYNTAX = 1` through `rpcLAST`, with `-1` reserved for `rpcUNKNOWN` (out-of-range codes) and `0` for `rpcSUCCESS`. Although the comments acknowledge the values were never formally promised to be stable, real API consumers started depending on them, so the file now carries an explicit policy: **only append new codes at the end; never fill gaps; never reuse values**. Dead codes are commented out with "// unused" rather than reassigned. This append-only discipline is enforced socially through comments but backed structurally by `rpcLAST`, which must always equal the highest valid code — if it is not updated when a new code is added the compile-time validation in the `.cpp` file will throw.

The enum is deliberately divided into semantic groups: general failures, networking problems, ledger-state errors, malformed-command errors, bad-parameter errors, and internal errors. These categories don't affect runtime behaviour but help maintainers decide which group a new code belongs to, keeping gaps predictable.

## `warning_code_i`

A separate, smaller enum for warnings returned in the `warnings` array of some RPC responses (not in the top-level `error` field). Its values start at 1001 to be clearly distinct from error codes. The comment explicitly calls out `warnRPC_FIELDS_DEPRECATED = 2004` as needing to remain stable because Clio (the alternative XRPL API server) hardcodes this value — cross-implementation compatibility in a live network.

## `ErrorInfo` and the Compile-Time Lookup Table

`ErrorInfo` is a plain `constexpr`-constructible struct with four fields: the `error_code_i` value, a `Json::StaticString` token (e.g., `"invalidParams"`), a `Json::StaticString` default message, and an `int http_status` that defaults to 200. Using `Json::StaticString` avoids heap allocation when the token is injected into a JSON value, since `StaticString` holds a raw `const char*` pointer to a string literal.

The implementation in `ErrorCodes.cpp` stores the canonical data in `unorderedErrorInfos`, an unordered `constexpr` array of `ErrorInfo` entries, then sorts and validates it at compile time via `sortErrorInfos<rpcLAST>(...)`. This function:

1. Allocates a default-initialised `std::array<ErrorInfo, rpcLAST>`.
2. Places each entry at index `code - 1` (since `rpcSUCCESS == 0`, valid codes start at 1).
3. Throws `std::out_of_range` on out-of-range codes, and `std::invalid_argument` on duplicates or gaps — all at compile time, meaning mismatches are link errors not runtime surprises.
4. Validates the final entry count against `rpcLAST`.

The result, `detail::sortedErrorInfos`, is a zero-runtime-cost array indexed by `code - 1`. `get_error_info(code)` is therefore an O(1) bounds check and a single array access — no hash table, no binary search.

The comment in the implementation explains the reasoning behind HTTP status selection: the goal is **load balancer fail-over semantics**. Errors that indicate a node is temporarily unable to serve a request (ledger not found, server too busy, amendment blocked) return `503 Service Unavailable` or similar 4xx/5xx codes so a load balancer can redirect the client to a healthy peer. Errors that are definitively the client's fault (bad parameters, malformed account) return `400 Bad Request`, while some semantic states like "ledger not yet validated" return `202 Accepted`. Errors that historically returned 200 keep that default, preserving backward compatibility.

## The JSON Error API

`inject_error(code, json)` mutates an existing `Json::Value` object by writing three fields: `error` (the token string), `error_code` (the numeric code), and `error_message` (the default message). The overload `inject_error(code, message, json)` replaces the default message with a caller-supplied string, used when context-specific detail is needed (e.g., which specific field was malformed).

`make_error` is a convenience wrapper that creates a new `Json::Value`, calls `inject_error`, and returns it. RPC handlers that construct a response from scratch use `make_error`; handlers that need to annotate an existing response object use `inject_error`.

`contains_error(json)` checks for the presence of the `error` key — the canonical test for whether a JSON object represents an error response. `rpcErrorString(jv)` concatenates token and message for logging, with an `XRPL_ASSERT` asserting the input actually contains an error.

## Field-Error Helper Family

The inline helpers in `namespace RPC` (`missing_field_error`, `invalid_field_error`, `object_field_error`, `expected_field_error`, and their `_message` variants) exist purely to eliminate repetitive string formatting across the hundreds of RPC parameter-validation sites. They all bottom out in `make_param_error`, which is itself a thin wrapper around `make_error(rpcINVALID_PARAMS, message)`. Each helper is overloaded for both `std::string` and `Json::StaticString` to avoid unnecessary string construction when field names are compile-time literals.

`not_validator_error()` is the only non-field-specific helper, hardcoding the message "not a validator" for the handful of commands that require the node to be a validator.

## Relationship to `RPCErr.h`

The adjacent `RPCErr.h` exposes `isRpcError` and `rpcError`, which are marked deprecated. These are older wrappers predating the current injection/make API. New code uses the functions in `ErrorCodes.h` directly.