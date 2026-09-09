#pragma once

/**
 * Compile-time span name constants for ledger tracing.
 *
 *  Used by BuildLedger and LedgerMaster for ledger lifecycle spans.
 *  Built on StaticStr/join() from SpanNames.h.
 *
 *  Span hierarchy:
 *
 *    ledger.build   (BuildLedger — ledger construction)
 *    ledger.store   (LedgerMaster — ledger storage)
 *    ledger.validate (LedgerMaster — ledger validation acceptance)
 *    ledger.acquire (InboundLedger — fetch a missing ledger from peers)
 *    tx.apply       (BuildLedger — transaction application)
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::ledger_span {

// ===== Span operation suffixes ===============================================

namespace op {
inline constexpr auto build = makeStr("build");
inline constexpr auto store = makeStr("store");
inline constexpr auto validate = makeStr("validate");
inline constexpr auto apply = makeStr("apply");
inline constexpr auto acquire = makeStr("acquire");
}  // namespace op

// ===== Attribute keys ========================================================

namespace attr {
/**
 * Canonical shared constants (defined in SpanNames.h).
 */
using ::xrpl::telemetry::attr::closeResolutionMs;
using ::xrpl::telemetry::attr::closeTimeCorrect;
using ::xrpl::telemetry::attr::closeTimeRippleEpochS;
using ::xrpl::telemetry::attr::ledgerHash;
using ::xrpl::telemetry::attr::ledgerSeq;

/**
 * Domain-owned bare attrs.
 */
inline constexpr auto txCount = makeStr("tx_count");
inline constexpr auto txFailed = makeStr("tx_failed");
inline constexpr auto validations = makeStr("validations");

/**
 * ledger.acquire attrs (InboundLedger fetch lifecycle).
 */
inline constexpr auto acquireReason = makeStr("acquire_reason");
inline constexpr auto timeouts = makeStr("timeouts");
inline constexpr auto peerCount = makeStr("peer_count");
inline constexpr auto outcome = makeStr("outcome");
}  // namespace attr

// ===== Attribute values ======================================================

namespace val {
/**
 * ledger.acquire outcome values.
 */
inline constexpr auto complete = makeStr("complete");
inline constexpr auto failed = makeStr("failed");
/**
 * Set when the acquisition is abandoned before it finishes, i.e. the
 * InboundLedger is destroyed while !isDone(). Distinct from `failed`, which
 * means the fetch ran to its retry limit and gave up.
 */
inline constexpr auto aborted = makeStr("aborted");
/**
 * ledger.acquire reason values (mirror InboundLedger::Reason).
 */
inline constexpr auto history = makeStr("history");
inline constexpr auto consensus = makeStr("consensus");
inline constexpr auto generic = makeStr("generic");
}  // namespace val

}  // namespace xrpl::telemetry::ledger_span
