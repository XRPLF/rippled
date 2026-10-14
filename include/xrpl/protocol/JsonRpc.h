#pragma once

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

}  // namespace xrpl::rpc
