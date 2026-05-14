/** @file
 *  Deprecated compatibility shim for the XRPL RPC error API.
 *
 *  Provides `rpcError()` and `isRpcError()` — older entry points that predate
 *  the `RPC` namespace error API. New code should use `RPC::makeError()` and
 *  `RPC::containsError()` from `ErrorCodes.h` instead.
 */

#include <xrpl/protocol/RPCErr.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

// Orphaned forward declaration — no definition exists; retained as an artifact.
struct RPCErr;

/** Construct a JSON error object for the given error code.
 *
 *  Delegates to `RPC::injectError()`, which populates the result with the
 *  canonical `error` token, `error_code`, and `error_message` fields from
 *  the static `ErrorInfo` registry.
 *
 *  @param iError  The RPC error code to encode.
 *  @return A new `Json::Value` object suitable for returning to an API client.
 *  @deprecated Use `RPC::makeError(code)` instead.
 */
json::Value
rpcError(ErrorCodeI iError)
{
    json::Value jvResult(json::ValueType::Object);
    RPC::injectError(iError, jvResult);
    return jvResult;
}

/** Return `true` if @p jvResult represents an RPC error response.
 *
 *  Checks that the value is a JSON object and that it contains a member
 *  named `jss::error` — the key written by `RPC::injectError()` for every
 *  error response.
 *
 *  @param jvResult  The JSON value to inspect (taken by value).
 *  @return `true` if @p jvResult is an object containing an `"error"` member.
 *  @note The argument is taken by value rather than const reference; this is a
 *      minor inefficiency inherited from the original implementation.
 *  @deprecated Use `RPC::containsError(json)` instead.
 */
bool
isRpcError(json::Value jvResult)
{
    return jvResult.isObject() && jvResult.isMember(jss::error);
}

}  // namespace xrpl
