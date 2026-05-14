#pragma once

/** @file
 *  Deprecated compatibility shim for the XRPL RPC error API.
 *
 *  Declares `rpcError()` and `isRpcError()` — legacy entry points in the
 *  `xrpl` namespace that predate the richer `RPC`-namespaced error
 *  infrastructure in `ErrorCodes.h`. New code should use `RPC::makeError()`
 *  and `RPC::containsError()` from `ErrorCodes.h` directly.
 */

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>

namespace xrpl {

/** Return `true` if @p jvResult represents an RPC error response.
 *
 *  Duck-types the JSON value by checking for the presence of the `"error"`
 *  key — the same structural sentinel that `RPC::containsError()` tests,
 *  making it the direct modern equivalent.
 *
 *  @param jvResult  The JSON value to inspect (taken by value, not
 *      `const` reference — an inefficiency inherited from the original
 *      implementation that was never corrected given this function's
 *      deprecated status).
 *  @return `true` if @p jvResult is a JSON object containing an `"error"`
 *      member.
 *  @deprecated Use `RPC::containsError(json)` from `ErrorCodes.h` instead.
 */
bool
isRpcError(json::Value jvResult);

/** Construct a fresh JSON error object for the given error code.
 *
 *  Delegates to `RPC::injectError()`, which populates a new `Json::Value`
 *  object with the canonical `error` token, `error_code`, and
 *  `error_message` fields drawn from the static `ErrorInfo` registry.
 *  The return-by-value produces a self-contained error object ready for
 *  direct return from an RPC handler.
 *
 *  @param iError  The RPC error code to encode.
 *  @return A new `Json::Value` object with `error`, `error_code`, and
 *      `error_message` populated.
 *  @deprecated Use `RPC::makeError(code)` from `ErrorCodes.h` instead.
 */
json::Value
rpcError(ErrorCodeI iError);

}  // namespace xrpl
