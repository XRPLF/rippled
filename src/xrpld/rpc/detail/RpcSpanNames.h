#pragma once

/**
 * Compile-time span name constants for the RPC subsystem.
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
 *      span.setAttribute(rpc_span::attr::rpcStatus, rpc_span::val::success);
 *  @endcode
 *
 *  Span hierarchy (automatic nesting via OTel thread-local context):
 *
 *  HTTP JSON-RPC path (single request):
 *
 *    +-------------------------------------------------------+
 *    | rpc.http_request                                      |
 *    | ServerHandler::processSession(Session)                |
 *    |                                                       |
 *    |  +--------------------------------------------------+ |
 *    |  | rpc.process                                      | |
 *    |  | ServerHandler::processRequest()                  | |
 *    |  |                                                  | |
 *    |  |  +---------------------------------------------+ | |
 *    |  |  | rpc.command.{name}                          | | |
 *    |  |  | rpc::callMethod()                           | | |
 *    |  |  | attrs: command, version, rpc_role, rpc_status | | |
 *    |  |  +---------------------------------------------+ | |
 *    |  +--------------------------------------------------+ |
 *    +-------------------------------------------------------+
 *
 *  HTTP batch path (multiple commands per request):
 *
 *    +-------------------------------------------------------+
 *    | rpc.http_request                                      |
 *    |                                                       |
 *    |  +--------------------------------------------------+ |
 *    |  | rpc.process                                      | |
 *    |  |                                                  | |
 *    |  |  +------------------+  +------------------+      | |
 *    |  |  | rpc.command.{a}  |  | rpc.command.{b}  | ...  | |
 *    |  |  +------------------+  +------------------+      | |
 *    |  +--------------------------------------------------+ |
 *    +-------------------------------------------------------+
 *
 *  WebSocket path:
 *
 *    +-------------------------------------------------------+
 *    | rpc.ws_message                                        |
 *    | ServerHandler::processSession(WSSession)              |
 *    |                                                       |
 *    |  +--------------------------------------------------+ |
 *    |  | rpc.command.{name}                               | |
 *    |  | rpc::callMethod()                                | |
 *    |  | attrs: command, version, rpc_role, rpc_status     | |
 *    |  +--------------------------------------------------+ |
 *    +-------------------------------------------------------+
 *
 *  WebSocket error paths:
 *
 *    +-------------------------------------------------------+
 *    | rpc.ws_message (error: invalid_json)                  |
 *    | ServerHandler::onWSMessage() — parse failure           |
 *    +-------------------------------------------------------+
 *
 *    +-------------------------------------------------------+
 *    | rpc.ws_upgrade                                        |
 *    | ServerHandler::onHandoff() — upgrade try/catch         |
 *    +-------------------------------------------------------+
 *
 *  Command dispatch error path:
 *
 *    +-------------------------------------------------------+
 *    | rpc.command.{name} (error: too_busy/unknown/etc)       |
 *    | rpc::doCommand() — fillHandler() rejection             |
 *    +-------------------------------------------------------+
 *
 *  gRPC path (see GrpcSpanNames.h for constants):
 *
 *    +-------------------------------------------------------+
 *    | grpc.<MethodName>  (e.g. grpc.GetLedger)              |
 *    | CallData::process(coro)                               |
 *    |   attrs: method, grpc_status                          |
 *    +-------------------------------------------------------+
 *
 *  Covered paths:
 *    - HTTP JSON-RPC (single and batch requests)
 *    - WebSocket RPC commands
 *    - WebSocket message parse errors (invalid JSON, oversized)
 *    - WebSocket upgrade failures (protocol handshake errors)
 *    - Admin CLI (connects via HTTP internally)
 *    - Command dispatch rejections (unknown cmd, too busy, no perm)
 *    - gRPC endpoints (GetLedger, GetLedgerData, GetLedgerDiff,
 *      GetLedgerEntry)
 *    - Command execution: timing, success/failure, exceptions
 *    - Per-command attributes: name, API version, rpc_role, rpc_status
 *
 *  Known gaps (not yet instrumented):
 *    - Early validation errors in processRequest() before rpc.process
 *      span (malformed JSON, auth failures, oversized requests)
 *    - Subscription push notifications (server-initiated, not RPC)
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::rpc_span {

// ===== Span prefixes =======================================================

namespace prefix {
/**
 * "rpc" — root prefix for transport-level spans.
 */
inline constexpr auto rpc = seg::rpc;
/**
 * "rpc.command" — prefix for individual RPC command spans.
 */
inline constexpr auto command = join(seg::rpc, makeStr("command"));
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto wsMessage = makeStr("ws_message");
inline constexpr auto wsUpgrade = makeStr("ws_upgrade");
inline constexpr auto httpRequest = makeStr("http_request");
inline constexpr auto process = makeStr("process");
}  // namespace op

// ===== Attribute keys ======================================================

namespace attr {
/**
 * "command" — RPC method name.
 */
inline constexpr auto command = makeStr("command");
/**
 * "version" — api_version per request.
 */
inline constexpr auto version = makeStr("version");
/**
 * "rpc_role" — admin|user. Domain-qualified: collides with grpc_role.
 */
inline constexpr auto rpcRole = makeStr("rpc_role");
/**
 * "rpc_status" — success|error. Domain-qualified: avoids OTel reserved span status.
 */
inline constexpr auto rpcStatus = makeStr("rpc_status");
/**
 * "request_payload_size" — bytes of inbound request payload.
 */
inline constexpr auto requestPayloadSize = makeStr("request_payload_size");
/**
 * "is_batch" — whether request is a JSON-RPC batch.
 */
inline constexpr auto isBatch = makeStr("is_batch");
/**
 * "batch_size" — number of sub-requests in a batch.
 */
inline constexpr auto batchSize = makeStr("batch_size");
/**
 * "load_type" — resource cost category after execution.
 */
inline constexpr auto loadType = makeStr("load_type");
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
using telemetry::attr_val::error;
using telemetry::attr_val::success;
inline constexpr auto admin = makeStr("admin");
inline constexpr auto user = makeStr("user");
inline constexpr auto unknownCommand = makeStr("unknown");
/**
 * "invalid_json" — WS message parse failure or oversize.
 */
inline constexpr auto invalidJson = makeStr("invalid_json");
}  // namespace val

}  // namespace xrpl::telemetry::rpc_span
