#pragma once

/** Compile-time span name constants for the gRPC subsystem.
 *
 *  All span prefixes, operation names, and attribute keys used by gRPC
 *  tracing call sites are defined here. Built on the StaticStr/join()
 *  primitives from <xrpl/telemetry/SpanNames.h>.
 *
 *  Span hierarchy:
 *
 *    +-------------------------------------------------------+
 *    | grpc.request                                          |
 *    | CallData::process(coro)                               |
 *    |   attrs: method, role, status                         |
 *    +-------------------------------------------------------+
 *
 *  Unlike the HTTP/WS RPC path, gRPC has a flat single-span structure
 *  per request since each CallData handles exactly one RPC method.
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl {
namespace telemetry {
namespace grpc_span {

// ===== Span prefixes =======================================================

namespace prefix {
/// "grpc" — root prefix for gRPC transport spans.
inline constexpr auto grpc = makeStr("grpc");
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto request = makeStr("request");
}  // namespace op

// ===== Attribute keys ======================================================

namespace attr {
inline constexpr auto xrplGrpc = join(seg::xrpl, makeStr("grpc"));

/// "xrpl.grpc.method"
inline constexpr auto method = join(xrplGrpc, makeStr("method"));
/// "xrpl.grpc.role"
inline constexpr auto role = join(xrplGrpc, makeStr("role"));
/// "xrpl.grpc.status"
inline constexpr auto status = join(xrplGrpc, makeStr("status"));
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
using telemetry::attr_val::error;
using telemetry::attr_val::success;
inline constexpr auto resourceExhausted = makeStr("resource_exhausted");
inline constexpr auto failedPrecondition = makeStr("failed_precondition");
}  // namespace val

}  // namespace grpc_span
}  // namespace telemetry
}  // namespace xrpl
