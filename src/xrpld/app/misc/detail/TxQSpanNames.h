#pragma once

/** Compile-time span name constants for Transaction Queue tracing.
 *
 *  Covers the TxQ lifecycle: enqueue decisions, direct apply, batch
 *  clear, ledger-close accept loop, per-tx apply, and cleanup.
 *
 *  Span hierarchy:
 *
 *    Transaction submission:
 *
 *    +-------------------------------------------------------+
 *    | tx.process (existing, from TxSpanNames.h)             |
 *    |                                                       |
 *    |  +--------------------------------------------------+ |
 *    |  | txq.enqueue                                      | |
 *    |  | TxQ::apply()                                     | |
 *    |  | attrs: tx_hash, status, fee_level                | |
 *    |  |                                                  | |
 *    |  |  +-------------------+ +----------------------+  | |
 *    |  |  | txq.apply_direct  | | txq.batch_clear      |  | |
 *    |  |  | tryDirectApply()  | | tryClearAccount...() |  | |
 *    |  |  +-------------------+ +----------------------+  | |
 *    |  +--------------------------------------------------+ |
 *    +-------------------------------------------------------+
 *
 *    Ledger close (consensus thread):
 *
 *    +-------------------------------------------------------+
 *    | txq.accept                                            |
 *    | TxQ::accept()                                         |
 *    |   attrs: queue_size, ledger_changed                   |
 *    |                                                       |
 *    |  +--------------------------------------------------+ |
 *    |  | txq.accept.tx  (per queued transaction)           | |
 *    |  | attrs: tx_hash, ter_code, retries_remaining       | |
 *    |  +--------------------------------------------------+ |
 *    +-------------------------------------------------------+
 *
 *    Post-close cleanup:
 *
 *    +-------------------------------------------------------+
 *    | txq.cleanup                                           |
 *    | TxQ::processClosedLedger()                            |
 *    |   attrs: ledger_seq, expired_count                    |
 *    +-------------------------------------------------------+
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::txq_span {

// ===== Span prefixes =======================================================

namespace prefix {
/// "txq" — root prefix for transaction queue spans.
inline constexpr auto txq = makeStr("txq");
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto enqueue = makeStr("enqueue");
inline constexpr auto applyDirect = makeStr("apply_direct");
inline constexpr auto batchClear = makeStr("batch_clear");
inline constexpr auto accept = makeStr("accept");
inline constexpr auto acceptTx = makeStr("accept_tx");
inline constexpr auto cleanup = makeStr("cleanup");
}  // namespace op

// ===== Attribute keys ======================================================

namespace attr {
inline constexpr auto xrplTxq = join(seg::xrpl, makeStr("txq"));

/// "xrpl.txq.tx_hash"
inline constexpr auto txHash = join(xrplTxq, makeStr("tx_hash"));
/// "xrpl.txq.status"
inline constexpr auto status = join(xrplTxq, makeStr("status"));
/// "xrpl.txq.fee_level_paid"
inline constexpr auto feeLevelPaid = join(xrplTxq, makeStr("fee_level_paid"));
/// "xrpl.txq.required_fee_level"
inline constexpr auto requiredFeeLevel = join(xrplTxq, makeStr("required_fee_level"));
/// "xrpl.txq.queue_size"
inline constexpr auto queueSize = join(xrplTxq, makeStr("queue_size"));
/// "xrpl.txq.ledger_changed"
inline constexpr auto ledgerChanged = join(xrplTxq, makeStr("ledger_changed"));
/// "xrpl.txq.ledger_seq"
inline constexpr auto ledgerSeq = join(xrplTxq, makeStr("ledger_seq"));
/// "xrpl.txq.expired_count"
inline constexpr auto expiredCount = join(xrplTxq, makeStr("expired_count"));
/// "xrpl.txq.ter_code"
inline constexpr auto terCode = join(xrplTxq, makeStr("ter_code"));
/// "xrpl.txq.retries_remaining"
inline constexpr auto retriesRemaining = join(xrplTxq, makeStr("retries_remaining"));
/// "xrpl.txq.num_cleared"
inline constexpr auto numCleared = join(xrplTxq, makeStr("num_cleared"));
}  // namespace attr

// ===== Attribute values ====================================================

namespace val {
inline constexpr auto queued = makeStr("queued");
inline constexpr auto appliedDirect = makeStr("applied_direct");
inline constexpr auto rejected = makeStr("rejected");
inline constexpr auto applied = makeStr("applied");
inline constexpr auto failed = makeStr("failed");
inline constexpr auto retried = makeStr("retried");
}  // namespace val

}  // namespace xrpl::telemetry::txq_span
