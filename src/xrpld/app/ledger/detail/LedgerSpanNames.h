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

#include <string_view>

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
using ::xrpl::telemetry::attr::closeTime;
using ::xrpl::telemetry::attr::closeTimeCorrect;
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
 *
 * The target ledger's identity uses the shared `ledgerHash` / `ledgerSeq`
 * keys aliased above, so a stuck acquire is findable in TraceQL by the exact
 * ledger it was fetching. Both are per-ledger values and therefore stay
 * trace-only: they are Tempo-searchable but are never promoted to spanmetrics
 * dimensions, which would mint one metric series per ledger.
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
 *
 * Every acquire records exactly one of these before its span closes, so a
 * span-derived outcome rate never silently drops an acquire that was still
 * in flight.
 *
 * - complete:  all header, transaction and state data was assembled.
 * - failed:    the fetch reached a terminal error (bad data, or the retry
 *              budget ran out).
 * - abandoned: the InboundLedger was destroyed while still fetching, which
 *              happens when the sweeper drops an acquire that made no
 *              progress, or on shutdown. This is the outcome for a fetch
 *              that never reached a result at all -- the case that used to
 *              leave the span with no outcome and a duration stretched to
 *              the sweep interval instead of the real fetch time.
 */
inline constexpr auto complete = makeStr("complete");
inline constexpr auto failed = makeStr("failed");
inline constexpr auto abandoned = makeStr("abandoned");
/**
 * ledger.acquire reason values (mirror InboundLedger::Reason).
 */
inline constexpr auto history = makeStr("history");
inline constexpr auto consensus = makeStr("consensus");
inline constexpr auto generic = makeStr("generic");
}  // namespace val

// ===== Outcome rule ==========================================================

/**
 * Pick the terminal `outcome` value for an acquire from its own state flags.
 *
 * This is the whole rule behind "every ledger.acquire span carries an
 * outcome". InboundLedger has four exit paths (done(), the local-store
 * shortcut, the "can never be acquired" exit, and the destructor when a fetch
 * is swept), and all of them derive the value here instead of naming one, so
 * no exit can label itself wrongly and no exit can be added without getting an
 * outcome.
 *
 * The "neither flag" case is the one that matters: an acquire destroyed while
 * still fetching reached no result, and reporting it as `abandoned` is what
 * keeps a stuck-then-swept fetch in the outcome rate instead of vanishing
 * from it.
 *
 * Kept here, next to the values it returns, as a pure constexpr function: it
 * has no dependency on InboundLedger and can therefore be asserted directly
 * from the lib-only test binary, which cannot link xrpld.
 *
 * @param failed   The acquire's `failed_` flag (terminal error).
 * @param complete The acquire's `complete_` flag (all data assembled).
 * @return `failed` when failed is set, `complete` when only complete is set,
 *         otherwise `abandoned`.
 *
 * Example -- the three live cases:
 * @code
 * acquireOutcome(false, true);   // "complete"  -- normal success
 * acquireOutcome(true, false);   // "failed"    -- terminal error
 * acquireOutcome(false, false);  // "abandoned" -- swept mid-fetch
 * @endcode
 *
 * Example -- edge case: a failure recorded on an otherwise complete acquire
 * still reports `failed`, because a fetch that hit a terminal error is not a
 * success no matter what else was assembled:
 * @code
 * acquireOutcome(true, true);    // "failed"
 * @endcode
 *
 * @note Pure and side-effect free; safe to call from any thread, including a
 *       destructor (it allocates nothing and cannot throw).
 */
[[nodiscard]] constexpr std::string_view
acquireOutcome(bool failed, bool complete) noexcept
{
    if (failed)
        return val::failed;
    if (complete)
        return val::complete;
    return val::abandoned;
}

}  // namespace xrpl::telemetry::ledger_span
