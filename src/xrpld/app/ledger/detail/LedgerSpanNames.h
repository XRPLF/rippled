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
}  // namespace attr

}  // namespace xrpl::telemetry::ledger_span
