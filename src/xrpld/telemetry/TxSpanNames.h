#pragma once

/**
 * Compile-time span name constants for transaction tracing.
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
/**
 * "tx" — root prefix for transaction lifecycle spans.
 */
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
/**
 * Canonical shared constants (defined in SpanNames.h).
 */
using ::xrpl::telemetry::attr::peerId;
using ::xrpl::telemetry::attr::txHash;

/**
 * "local" — whether tx originated locally.
 */
inline constexpr auto local = makeStr("local");
/**
 * "path" — sync or async processing path.
 */
inline constexpr auto path = makeStr("path");
/**
 * "suppressed" — whether tx was suppressed as duplicate.
 */
inline constexpr auto suppressed = makeStr("suppressed");
/**
 * "tx_status" — domain-qualified (collides with rpc_status, txq_status).
 */
inline constexpr auto txStatus = makeStr("tx_status");
/**
 * "peer_version" — version of peer that sent the tx.
 */
inline constexpr auto peerVersion = makeStr("peer_version");
/**
 * "tx_type" — transaction type name (e.g., "Payment", "OfferCreate").
 */
inline constexpr auto txType = makeStr("tx_type");
/**
 * "fee" — transaction fee in drops.
 */
inline constexpr auto fee = makeStr("fee");
/**
 * "sequence" — transaction sequence number.
 */
inline constexpr auto sequence = makeStr("sequence");
/**
 * "ter_result" — engine result code after application.
 */
inline constexpr auto terResult = makeStr("ter_result");
/**
 * "applied" — whether the transaction was applied to the ledger.
 */
inline constexpr auto applied = makeStr("applied");
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
inline constexpr auto sync = makeStr("sync");
inline constexpr auto async = makeStr("async");
inline constexpr auto knownBad = makeStr("known_bad");
/**
 * Transaction was suppressed via HashRouter (duplicate, not flagged bad).
 */
inline constexpr auto suppressed = makeStr("suppressed");
/**
 * Transaction was rejected because it carried tfInnerBatchTxn, which
 * must never appear in network-relayed traffic.
 */
inline constexpr auto rejectedInnerBatch = makeStr("rejected_inner_batch");
/**
 * Transaction was dropped because the validated ledger is too old to
 * confidently apply new transactions (server is out of sync).
 */
inline constexpr auto droppedNoSync = makeStr("dropped_no_sync");
/**
 * Transaction was dropped because the local job queue for jtTRANSACTION
 * is at MAX_TRANSACTIONS — backpressure on the receive side.
 */
inline constexpr auto droppedQueueFull = makeStr("dropped_queue_full");
}  // namespace val

}  // namespace xrpl::telemetry::tx_span
