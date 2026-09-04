#pragma once

/**
 * Compile-time span name constants for Transaction Queue tracing.
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
 *    |  | attrs: tx_hash, tx_type, txq_status,             | |
 *    |  |        fee_level_paid, required_fee_level         | |
 *    |  |                                                  | |
 *    |  |  +-------------------+ +----------------------+  | |
 *    |  |  | txq.apply_direct  | | txq.batch_clear      |  | |
 *    |  |  | tryDirectApply()  | | tryClearAccount...() |  | |
 *    |  |  +-------------------+ | attrs: num_cleared   |  | |
 *    |  |                        +----------------------+  | |
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
 *    |  | txq.accept_tx  (per queued transaction)           | |
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
/**
 * "txq" — root prefix for transaction queue spans.
 */
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

// ===== Full span names (prefix.op) ===========================================
//
// Joined "txq.<op>" names for the explicit-context factories
// (SpanGuard::childSpan(name, ctx)) that take one full span name rather than a
// prefix/suffix pair.

inline constexpr auto enqueue = join(prefix::txq, op::enqueue);

// ===== Attribute keys ======================================================

namespace attr {
/**
 * Canonical shared constants (defined in SpanNames.h).
 */
using ::xrpl::telemetry::attr::currentLedgerHash;
using ::xrpl::telemetry::attr::currentLedgerSeq;
using ::xrpl::telemetry::attr::ledgerSeq;
using ::xrpl::telemetry::attr::txHash;

/**
 * "txq_status" — domain-qualified (collides with tx_status, rpc_status).
 */
inline constexpr auto txqStatus = makeStr("txq_status");
/**
 * "fee_level_paid" — fee level paid by queued tx.
 */
inline constexpr auto feeLevelPaid = makeStr("fee_level_paid");
/**
 * "required_fee_level" — minimum fee level for inclusion.
 */
inline constexpr auto requiredFeeLevel = makeStr("required_fee_level");
/**
 * "queue_size" — current TxQ depth.
 */
inline constexpr auto queueSize = makeStr("queue_size");
/**
 * "ledger_changed" — whether ledger changed since last attempt.
 */
inline constexpr auto ledgerChanged = makeStr("ledger_changed");
/**
 * "expired_count" — number of expired entries cleared.
 */
inline constexpr auto expiredCount = makeStr("expired_count");
/**
 * "ter_code" — transaction engine result code.
 */
inline constexpr auto terCode = makeStr("ter_code");
/**
 * "retries_remaining" — retries left before discard.
 */
inline constexpr auto retriesRemaining = makeStr("retries_remaining");
/**
 * "num_cleared" — entries cleared in batch.
 */
inline constexpr auto numCleared = makeStr("num_cleared");
/**
 * "tx_type" — transaction type name (e.g., "Payment", "OfferCreate").
 */
inline constexpr auto txType = makeStr("tx_type");
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
