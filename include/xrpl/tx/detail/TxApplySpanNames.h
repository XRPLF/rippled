#pragma once

/**
 * Compile-time span name constants for the transaction apply pipeline.
 *
 *  Defines the span names and attribute keys used by the three apply-pipeline
 *  stages — preflight, preclaim, and transactor (apply) — that run inside the
 *  library (`src/libxrpl/tx/`). Built on the StaticStr/join() primitives from
 *  <xrpl/telemetry/SpanNames.h>.
 *
 *  Why a separate header from TxSpanNames.h:
 *  TxSpanNames.h lives under src/xrpld/ (daemon) and serves the overlay/app
 *  lifecycle spans (tx.receive, tx.process). Library code (applySteps.cpp,
 *  Transactor.cpp) must not depend on daemon headers, so the apply-pipeline
 *  constants live here instead. The attribute strings ("tx_type",
 *  "ter_result", "applied") intentionally match TxSpanNames.h so the collector
 *  spanmetrics connector aggregates both sets under the same dimensions.
 *
 *  Span hierarchy (deterministic trace_id derived from txID[0:16]):
 *
 *  The three stages run sequentially and often on different threads, so they
 *  do not auto-parent. Each uses a hash-derived trace_id keyed on the same
 *  transaction id, placing all three under one trace without context
 *  propagation. A transaction that hard-fails preflight or preclaim never
 *  reaches the transactor span — the stage attribute identifies where it
 *  stopped.
 *
 *    +-----------------------------------------------------------+
 *    | trace_id = txID[0:16]                                     |
 *    |                                                           |
 *    |  +-------------------+   +------------------+   +-------+  |
 *    |  | tx.preflight      |   | tx.preclaim      |   | tx.   |  |
 *    |  | stage=preflight   |-->| stage=preclaim   |-->| trans |  |
 *    |  | tx_type           |   | tx_type          |   | actor |  |
 *    |  | ter_result        |   | ter_result       |   | stage=|  |
 *    |  +-------------------+   +------------------+   | apply |  |
 *    |   stateless checks       ledger-aware checks   +-------+  |
 *    |   (signature, fields)    (sequence, fee)        applies   |
 *    +-----------------------------------------------------------+
 *
 *  Usage:
 *  @code
 *      #include <xrpl/tx/detail/TxApplySpanNames.h>
 *      using namespace telemetry;
 *
 *      // preflight() / preclaim() use hashSpan with a full span name:
 *      auto span = SpanGuard::hashSpan(
 *          TraceCategory::Transactions, tx_apply_span::preflight,
 *          txID.data(), txID.kBytes);
 *      span.setAttribute(tx_apply_span::attr::stage, tx_apply_span::val::preflight);
 *      span.setAttribute(tx_apply_span::attr::terResult, transToken(ter).c_str());
 *  @endcode
 *
 *  @code
 *      // Transactor::operator() also uses hashSpan on the same txID so it
 *      // co-traces with preflight and preclaim under one trace_id:
 *      auto span = SpanGuard::hashSpan(
 *          TraceCategory::Transactions, tx_apply_span::transactor,
 *          txID.data(), txID.kBytes);
 *      span.setAttribute(tx_apply_span::attr::stage, tx_apply_span::val::apply);
 *  @endcode
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::tx_apply_span {

// ===== Span operation suffixes =============================================

namespace op {
/**
 * "preflight" — stateless transaction checks (suffix form).
 */
inline constexpr auto preflight = makeStr("preflight");
/**
 * "preclaim" — ledger-aware checks before fee claim (suffix form).
 */
inline constexpr auto preclaim = makeStr("preclaim");
/**
 * "transactor" — the apply stage (suffix form, used with span()).
 */
inline constexpr auto transactor = makeStr("transactor");
}  // namespace op

// ===== Full span names (tx.<op>) ===========================================

/**
 * "tx.preflight" — full name for hashSpan() at the preflight stage.
 */
inline constexpr auto preflight = join(seg::tx, op::preflight);
/**
 * "tx.preclaim" — full name for hashSpan() at the preclaim stage.
 */
inline constexpr auto preclaim = join(seg::tx, op::preclaim);
/**
 * "tx.transactor" — full name for hashSpan() at the apply stage. Shares the
 * txID-derived trace_id so it co-traces with tx.preflight and tx.preclaim.
 */
inline constexpr auto transactor = join(seg::tx, op::transactor);

// ===== Attribute keys ======================================================

namespace attr {
/**
 * Shared "ledger being worked on" attrs (defined in SpanNames.h). Set on
 * tx.preclaim and tx.transactor (both run against a view whose seq() is the
 * ledger being applied into). tx.preflight is stateless (no view) and is the
 * documented exception — it carries neither.
 */
using ::xrpl::telemetry::attr::currentLedgerHash;
using ::xrpl::telemetry::attr::currentLedgerSeq;

/**
 * "stage" — which apply-pipeline stage this span represents. Drives the
 * collector spanmetrics `stage` dimension for per-stage RED metrics.
 */
inline constexpr auto stage = makeStr("stage");
/**
 * "tx_type" — transaction type name (e.g., "Payment", "OfferCreate").
 * Matches tx_span::attr::txType so both share the spanmetrics dimension.
 */
inline constexpr auto txType = makeStr("tx_type");
/**
 * "ter_result" — engine result code after the stage (e.g., "tesSUCCESS").
 */
inline constexpr auto terResult = makeStr("ter_result");
/**
 * "applied" — whether the transaction was applied to the ledger (apply only).
 */
inline constexpr auto applied = makeStr("applied");
}  // namespace attr

// ===== Attribute values (stage names) ======================================

namespace val {
/**
 * "preflight" — value of the stage attribute on tx.preflight.
 */
inline constexpr auto preflight = makeStr("preflight");
/**
 * "preclaim" — value of the stage attribute on tx.preclaim.
 */
inline constexpr auto preclaim = makeStr("preclaim");
/**
 * "apply" — value of the stage attribute on tx.transactor.
 */
inline constexpr auto apply = makeStr("apply");
}  // namespace val

}  // namespace xrpl::telemetry::tx_apply_span
