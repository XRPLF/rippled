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
 * kJsonRpcServerError is the code for an error an XRPL handler reports. The
 * XRPL token and code travel in the error's `data` member, so renumbering an
 * XRPL error cannot change a JSON-RPC code.
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

/**
 * The `message` the specification fixes for three of its reserved codes.
 *
 * A reply reporting kJsonRpcInvalidRequest, kJsonRpcMethodNotFound or
 * kJsonRpcInvalidParams carries this text in `message` and the server's own
 * reason in `data`. The specification defines no message for the other codes
 * above, so a reply reporting one of them carries the reason in `message`.
 */
inline constexpr std::string_view kJsonRpcInvalidRequestMessage{"Invalid Request"};
inline constexpr std::string_view kJsonRpcMethodNotFoundMessage{"Method not found"};
inline constexpr std::string_view kJsonRpcInvalidParamsMessage{"Invalid params"};

}  // namespace xrpl::rpc
