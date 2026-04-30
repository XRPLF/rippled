#pragma once

/** Compile-time span name constants for ledger tracing.
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
inline constexpr auto xrplLedger = join(seg::xrpl, seg::ledger);

/// "xrpl.ledger.seq"
inline constexpr auto seq = join(xrplLedger, makeStr("seq"));
/// "xrpl.ledger.close_time"
inline constexpr auto closeTime = join(xrplLedger, makeStr("close_time"));
/// "xrpl.ledger.close_time_correct"
inline constexpr auto closeTimeCorrect = join(xrplLedger, makeStr("close_time_correct"));
/// "xrpl.ledger.close_resolution_ms"
inline constexpr auto closeResolutionMs = join(xrplLedger, makeStr("close_resolution_ms"));
/// "xrpl.ledger.tx_count"
inline constexpr auto txCount = join(xrplLedger, makeStr("tx_count"));
/// "xrpl.ledger.tx_failed"
inline constexpr auto txFailed = join(xrplLedger, makeStr("tx_failed"));
/// "xrpl.ledger.validations"
inline constexpr auto validations = join(xrplLedger, makeStr("validations"));
}  // namespace attr

}  // namespace xrpl::telemetry::ledger_span
