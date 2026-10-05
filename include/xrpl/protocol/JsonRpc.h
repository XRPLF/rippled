#pragma once

#include <xrpl/json/json_forwards.h>

#include <string_view>

namespace xrpl::rpc {

/**
 * Constants of the JSON-RPC 2.0 protocol itself.
 *
 * Kept apart from the `api_version` and `ripplerpc` constants in ApiVersion.h,
 * which go when support for API versions 1 and 2 goes.
 */

/**
 * Value of the `jsonrpc` member of a request and of its reply, fixed at "2.0"
 * by the JSON-RPC specification.
 */
inline constexpr std::string_view kJsonRpcVersion{"2.0"};

/**
 * Codes for the `code` member of a JSON-RPC error object.
 *
 * The specification reserves -32768 to -32000 for the protocol and leaves
 * -32000 to -32099 of it to the implementation.
 *
 * kJsonRpcServerError is the code for an error an XRPL handler reports.
 *
 * The codes from kJsonRpcServerOverloaded on lie outside the
 * implementation-defined sub-range, which the specification does not allow.
 * They are the codes every shipped version reports, so moving one breaks the
 * clients matching on it.
 */
inline constexpr json::Int kJsonRpcServerError{-32000};
inline constexpr json::Int kJsonRpcInvalidRequest{-32600};
inline constexpr json::Int kJsonRpcMethodNotFound{-32601};
inline constexpr json::Int kJsonRpcInvalidParams{-32602};
inline constexpr json::Int kJsonRpcServerOverloaded{-32604};
inline constexpr json::Int kJsonRpcForbidden{-32605};
inline constexpr json::Int kJsonRpcWrongVersion{-32606};

}  // namespace xrpl::rpc
