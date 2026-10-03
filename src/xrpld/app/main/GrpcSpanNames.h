#pragma once

/**
 * Compile-time span name constants for the gRPC subsystem.
 *
 *  All span prefixes, operation names, and attribute keys used by gRPC
 *  tracing call sites are defined here. Built on the StaticStr/join()
 *  primitives from <xrpl/telemetry/SpanNames.h>.
 *
 *  Span hierarchy:
 *
 *    +-------------------------------------------------------+
 *    | grpc.<MethodName>  (e.g. grpc.GetLedger)              |
 *    | CallData::process(coro)                               |
 *    |   attrs: method, grpc_role, grpc_status               |
 *    +-------------------------------------------------------+
 *
 *  Unlike the HTTP/WS RPC path, gRPC has a flat single-span structure
 *  per request since each CallData handles exactly one RPC method.
 *  The method name is embedded in the span name (rather than only as
 *  an attribute) so dashboards can break out per-method latency and
 *  error rates without needing TraceQL attribute filters.
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::grpc_span {

// ===== Span prefixes =======================================================

namespace prefix {
/**
 * "grpc" — root prefix for gRPC transport spans. The full span name is
 * formed at the call site as `grpc.<MethodName>` (see GRPCServer.cpp).
 */
inline constexpr auto grpc = makeStr("grpc");
}  // namespace prefix

// ===== Attribute keys ======================================================

namespace attr {
/**
 * "method" — gRPC method name (e.g. GetLedger).
 */
inline constexpr auto method = makeStr("method");
/**
 * "grpc_role" — Domain-qualified: collides with rpc_role.
 */
inline constexpr auto grpcRole = makeStr("grpc_role");
/**
 * "grpc_status" — Domain-qualified: avoids OTel reserved span status.
 */
inline constexpr auto grpcStatus = makeStr("grpc_status");
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
using telemetry::attr_val::error;
using telemetry::attr_val::success;
inline constexpr auto admin = makeStr("admin");
inline constexpr auto user = makeStr("user");
inline constexpr auto resourceExhausted = makeStr("resource_exhausted");
}  // namespace val

}  // namespace xrpl::telemetry::grpc_span
