# `RPCErr.cpp` — Deprecated RPC Error Utilities

This file is a thin compatibility shim, providing two free functions that predate the `RPC` namespace error API. Both are explicitly marked deprecated by the original author (VFALCO), and callers should migrate to the richer facilities in `ErrorCodes.h` instead.

## Role in the System

XRPL's RPC layer communicates errors to clients as JSON objects. The canonical, current API for constructing and inspecting those objects lives in `RPC::inject_error()`, `RPC::make_error()`, and `RPC::contains_error()` — all declared in `include/xrpl/protocol/ErrorCodes.h`. The two functions here, `rpcError()` and `isRpcError()`, are older entry points that wrap that same machinery but expose a less expressive interface. They exist only to avoid breaking call sites that have not yet been updated.

## Function Details

`rpcError(error_code_i iError)` constructs a fresh `Json::Value` object and delegates immediately to `RPC::inject_error()`. That function consults the statically-compiled `ErrorInfo` table — also in `ErrorCodes.h` — to populate the result with the error code's canonical `token` (a machine-readable string like `"invalidParams"`), its human-readable `message`, and the numeric code itself. The returned JSON value is what gets sent back to an API client. The newer equivalent is `RPC::make_error(code)`, which does exactly the same thing but with an explicit and non-deprecated name.

`isRpcError(Json::Value jvResult)` performs a two-step structural check: it verifies the value is a JSON object and that it contains a member named `jss::error`. Because `RPC::inject_error()` always writes the error details under the `error` key, this check is sufficient to distinguish an error response from a normal result. The replacement is `RPC::contains_error(json)`, declared in `ErrorCodes.h`, which serves the same purpose. Note that `isRpcError` takes its argument by value rather than const reference — a minor inefficiency that is inconsequential given the deprecated status of the function.

## The `RPCErr` Struct Forward Declaration

The `.cpp` file contains `struct RPCErr;` — a forward declaration with no corresponding definition anywhere in the translation unit. This is an orphaned artifact, likely left over from an earlier refactoring. It has no effect on compilation or behaviour.

## Migration Guidance

New code should use `RPC::make_error(code)` instead of `rpcError(code)`, and `RPC::contains_error(json)` instead of `isRpcError(json)`. For invalid-parameter errors specifically, `RPC::make_param_error(message)` and the family of `missing_field_error`, `invalid_field_error`, and `expected_field_error` helpers provide more expressive, self-documenting alternatives. All of these are inline functions defined directly in `ErrorCodes.h`, so there is no additional link-time cost to switching.