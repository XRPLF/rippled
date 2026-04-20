#pragma once

/** Compile-time span name constants for the RPC subsystem.
 *
 *  All span prefixes, operation names, and attribute keys used by RPC
 *  tracing call sites are defined here. Built on the StaticStr/join()
 *  primitives from <xrpl/telemetry/SpanNames.h>.
 *
 *  Usage:
 *  @code
 *      #include <xrpld/rpc/detail/RpcSpanNames.h>
 *      using namespace telemetry;
 *
 *      auto span = SpanGuard::span(
 *          TraceCategory::Rpc, rpc_span::prefix::command, "submit");
 *      span.setAttribute(rpc_span::attr::command, "submit");
 *      span.setAttribute(rpc_span::attr::status, rpc_span::val::success);
 *  @endcode
 *
 *  Span hierarchy (automatic nesting via OTel thread-local context):
 *
 *      HTTP path:
 *        rpc.http_request            ServerHandler::processSession(Session)
 *          rpc.process               ServerHandler::processRequest()
 *            rpc.command.{name}      RPC::callMethod()  [repeats for batch]
 *
 *      WebSocket path:
 *        rpc.ws_message              ServerHandler::processSession(WSSession)
 *          rpc.command.{name}        RPC::callMethod()
 *
 *  Covered paths:
 *    - HTTP JSON-RPC (single and batch requests)
 *    - WebSocket RPC commands
 *    - Admin CLI (connects via HTTP internally)
 *    - Command execution: timing, success/failure, exceptions
 *    - Per-command attributes: name, API version, role, status
 *
 *  Known gaps (not yet instrumented):
 *    - gRPC endpoints (GRPCServer.cpp) — no spans at all
 *    - Early validation errors in processRequest() before rpc.process span
 *      (malformed JSON, auth failures, oversized requests)
 *    - fillHandler() rejections in doCommand() before rpc.command span
 *      (unknown command, too busy, permission denied)
 *    - WebSocket upgrade failures in onHandoff()
 *    - WebSocket message parse errors in onWSMessage()
 *    - Subscription push notifications (server-initiated, not RPC)
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl {
namespace telemetry {
namespace rpc_span {

// ===== Span prefixes =======================================================

namespace prefix {
/// "rpc" — root prefix for transport-level spans.
inline constexpr auto rpc = seg::rpc;
/// "rpc.command" — prefix for individual RPC command spans.
inline constexpr auto command = join(seg::rpc, makeStr("command"));
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto wsMessage = makeStr("ws_message");
inline constexpr auto httpRequest = makeStr("http_request");
inline constexpr auto process = makeStr("process");
}  // namespace op

// ===== Attribute keys ======================================================

namespace attr {
inline constexpr auto xrplRpc = join(seg::xrpl, seg::rpc);

/// "xrpl.rpc.command"
inline constexpr auto command = join(xrplRpc, makeStr("command"));
/// "xrpl.rpc.version"
inline constexpr auto version = join(xrplRpc, makeStr("version"));
/// "xrpl.rpc.role"
inline constexpr auto role = join(xrplRpc, makeStr("role"));
/// "xrpl.rpc.status"
inline constexpr auto status = join(xrplRpc, makeStr("status"));
/// "xrpl.rpc.payload_size"
inline constexpr auto payloadSize = join(xrplRpc, makeStr("payload_size"));
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
using telemetry::attr_val::error;
using telemetry::attr_val::success;
inline constexpr auto admin = makeStr("admin");
inline constexpr auto user = makeStr("user");
}  // namespace val

}  // namespace rpc_span
}  // namespace telemetry
}  // namespace xrpl
