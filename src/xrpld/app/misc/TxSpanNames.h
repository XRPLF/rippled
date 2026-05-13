#pragma once

/** Compile-time span name constants for transaction tracing.
 *
 *  Used by PeerImp (overlay) and NetworkOPs (app) for transaction
 *  lifecycle spans. Built on StaticStr/join() from SpanNames.h.
 *
 *  Span hierarchy (cross-node propagation):
 *
 *    Node A (sender)                    Node B (receiver)
 *    +---------------------+            +---------------------+
 *    | tx.process          |  protobuf  | tx.receive          |
 *    |  injectSpanContext  | ---------> |  txReceiveSpan()    |
 *    |  (PropagationHelp.) | trace_ctx  |  extracts parent    |
 *    +---------------------+            +---------------------+
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::tx_span {

// ===== Span prefixes =======================================================

namespace prefix {
/// "tx" — root prefix for transaction lifecycle spans.
inline constexpr auto tx = seg::tx;
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto receive = makeStr("receive");
inline constexpr auto process = makeStr("process");
}  // namespace op

// ===== Full span names (prefix.op) =========================================

inline constexpr auto receive = join(prefix::tx, op::receive);
inline constexpr auto process = join(prefix::tx, op::process);

// ===== Attribute keys ======================================================

namespace attr {
/// Canonical shared constants (defined in SpanNames.h).
using ::xrpl::telemetry::attr::peerId;
using ::xrpl::telemetry::attr::txHash;

/// "local" — whether tx originated locally.
inline constexpr auto local = makeStr("local");
/// "path" — sync or async processing path.
inline constexpr auto path = makeStr("path");
/// "suppressed" — whether tx was suppressed as duplicate.
inline constexpr auto suppressed = makeStr("suppressed");
/// "tx_status" — domain-qualified (collides with rpc_status, txq_status).
inline constexpr auto txStatus = makeStr("tx_status");
/// "peer_version" — version of peer that sent the tx.
inline constexpr auto peerVersion = makeStr("peer_version");
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
inline constexpr auto sync = makeStr("sync");
inline constexpr auto async = makeStr("async");
inline constexpr auto knownBad = makeStr("known_bad");
}  // namespace val

}  // namespace xrpl::telemetry::tx_span
