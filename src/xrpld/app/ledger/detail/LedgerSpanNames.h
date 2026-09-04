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
 *      +-- ledger.acquire.header  (the ledger header / liBASE phase)
 *      +-- ledger.acquire.astree  (the account-state SHAMap phase)
 *      +-- ledger.acquire.txtree  (the transaction SHAMap phase)
 *    ledger.serve   (PeerImp — serve a peer's ledger-data request)
 *    tx.apply       (BuildLedger — transaction application)
 *    txset.acquire  (TransactionAcquire — fetch a proposed tx set)
 *      +-- [event] round.request  (one per consensus round that asked for
 *                                  this set; see event::roundRequest)
 *
 *  Why the acquire phases are separate child spans: a fresh sync is
 *  dominated by the account-state tree, but the flat parent span cannot
 *  separate it from the (usually tiny) transaction tree or from the header
 *  wait that gates both. Each phase carries its own missing-node count and
 *  timeout flag, so the phase that is stuck names itself.
 *
 *  Why the requesting round is an event, not a parent or a link: one round
 *  starts many fetches and one fetch is wanted by many rounds, so no single
 *  round owns a txset.acquire span. Events are repeatable and timestamped, so
 *  the span can record every requester.
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
inline constexpr auto serve = makeStr("serve");
}  // namespace op

// ===== Span prefixes =========================================================

namespace prefix {
/**
 * "txset" — root prefix for transaction-set acquisition spans. A tx set is
 * not a ledger, so it gets its own root rather than hiding under `ledger.`.
 */
inline constexpr auto txset = makeStr("txset");
}  // namespace prefix

// ===== Full span names =======================================================
//
// Joined names for the factories that take one complete span name rather than
// a prefix/suffix pair (childSpan(name) / childSpan(name, ctx)).

/**
 * The three phases of one ledger acquisition, each a child of ledger.acquire.
 *
 * `astree` and `txtree` are single words on purpose: they are the wire span
 * names a dashboard and TraceQL query match on, so they must stay stable and
 * unambiguous rather than reading as a further-nested `as.tree`.
 */
/**
 * The parent acquire span's full name. Named here rather than composed at the
 * call site because it is passed to hashSpan(), which takes one complete name,
 * and because the phase names below are built from the same two segments.
 */
inline constexpr auto acquireFull = join(seg::ledger, op::acquire);

inline constexpr auto acquireHeader = join(join(seg::ledger, op::acquire), makeStr("header"));
inline constexpr auto acquireAsTree = join(join(seg::ledger, op::acquire), makeStr("astree"));
inline constexpr auto acquireTxTree = join(join(seg::ledger, op::acquire), makeStr("txtree"));

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
 * The consensus round's identity, carried by the round.request event on
 * txset.acquire. Re-exported, not redefined: `currentLedgerHash` is already
 * documented in SpanNames.h as the current view's parent-ledger hash. Event
 * attributes only, so neither is a span attribute here nor a metric dimension.
 */
using ::xrpl::telemetry::attr::currentLedgerHash;
using ::xrpl::telemetry::attr::currentLedgerSeq;

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

/**
 * Per-phase ledger.acquire attrs (header / AS-tree / TX-tree child spans).
 *
 * `missingNodes` is the count the phase's last getMissingNodes() sweep already
 * produced, so recording it costs nothing extra; `timedOut` says whether the
 * phase ended on the retry budget rather than on completion. Both are bounded
 * only by the tree size, so `missing_nodes` stays span-only (Tempo-searchable)
 * while `timed_out` — two values — is safe as a spanmetrics dimension.
 */
inline constexpr auto missingNodes = makeStr("missing_nodes");
inline constexpr auto timedOut = makeStr("timed_out");

/**
 * txset.acquire attrs (TransactionAcquire fetch lifecycle).
 *
 * The set's own root hash identifies which proposed set stalled, so like
 * `ledgerHash` it is per-object and stays span-only. `durationMs` is recorded
 * explicitly rather than left to the span's own duration because the span is
 * ended from the acquiring thread and its wall time is the number an operator
 * reads directly off a trace.
 *
 * Which round(s) asked for the set is not an attribute here -- see
 * event::roundRequest.
 */
inline constexpr auto txSetHash = makeStr("txset_hash");
inline constexpr auto durationMs = makeStr("duration_ms");

/**
 * ledger.serve attrs (the JtLedgerReq worker answering a peer).
 *
 * `objectType` names which of the four request kinds was served, and
 * `servedNodes` how many SHAMap nodes the reply carried — accumulated by the
 * reply-assembly loop and written once after it, never per node.
 */
inline constexpr auto objectType = makeStr("object_type");
inline constexpr auto servedNodes = makeStr("served_nodes");
}  // namespace attr

// ===== Span events ===========================================================

namespace event {
/**
 * "round.request" -- one consensus round asked for this transaction set.
 *
 * Fired on txset.acquire, once per requesting round, carrying that round's
 * `currentLedgerHash` / `currentLedgerSeq`. A fetch is wanted by many rounds, so
 * the span accumulates one event per requester; a single attribute could only
 * ever name the first. Requesters are told apart by parent-ledger hash -- see
 * `InboundTransactionSet::lastRound`.
 *
 * The event count is a LOWER BOUND on the rounds that waited, valid only while
 * the span is open. Once the span closes with an outcome -- `timeout` in
 * particular -- the entry survives a few more rounds and their requests add
 * nothing, so a fetch that kept three rounds waiting can show one event.
 *
 * `currentLedgerSeq` is a decimal STRING here, not the int64 every span
 * attribute uses, because event attributes are string pairs. TraceQL numeric
 * comparisons such as `event.current_ledger_seq > N` therefore do not work.
 */
inline constexpr auto roundRequest = join(makeStr("round"), makeStr("request"));
}  // namespace event

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
 * Extra terminal value for the per-phase acquire and txset.acquire spans.
 *
 * `ledger.acquire` itself never uses this: an acquire that exhausts its retry
 * budget sets failed_, so its outcome is `failed`. A phase and a tx set can
 * instead end while the parent fetch is still alive, and `timeout` is what
 * names that -- the phase ran out of time, but nothing failed permanently.
 */
inline constexpr auto timeout = makeStr("timeout");

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

/**
 * ledger.serve object_type values (mirror the protobuf `itype` request kinds).
 *
 * Four bounded values, one per TMGetLedger info type: the ledger header
 * (`liBASE`), the transaction tree (`liTX_NODE`), the account-state tree
 * (`liAS_NODE`), and a proposed transaction set (`liTS_CANDIDATE`). Serving
 * the state tree is what a syncing peer needs most, so telling it apart from
 * the cheap header replies is the point of the split.
 */
inline constexpr auto header = makeStr("header");
inline constexpr auto txTree = makeStr("tx");
inline constexpr auto asTree = makeStr("as");
inline constexpr auto txSet = makeStr("txset");

/**
 * ledger.serve outcome values.
 *
 * - complete: a reply with at least one node was sent.
 * - partial:  nodes were sent but the reply hit a size cap, so the requester
 *             must ask again for the rest.
 * - refused:  nothing was sent. The paired `serve_refused_total` counter
 *             carries the specific cause; the span records only that this
 *             request went unanswered, so a trace shows the gap.
 */
inline constexpr auto partial = makeStr("partial");
inline constexpr auto refused = makeStr("refused");
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

/**
 * Pick the terminal `outcome` for ONE acquire phase (header / AS-tree /
 * TX-tree) or for a tx-set fetch.
 *
 * The sibling of acquireOutcome() for the units whose lifetime is shorter than
 * the whole fetch. It takes one more input, `timedOut`, because these units
 * have a fourth end state the parent does not: a unit can stop because the
 * retry budget expired. Reporting that as `failed` would say "bad data" and as
 * `abandoned` would say "we stopped caring", so `timeout` names it directly --
 * and it is the value a stuck fresh sync shows, which is why these spans
 * exist.
 *
 * Precedence is `timeout` > `failed` > `complete` > `abandoned`, and the
 * timeout-first order is the load-bearing part. The exhausted-budget path in
 * both emitters sets the terminal `failed_` flag as well, because that flag is
 * how the TimeoutCounter base stops its timer loop -- so `timedOut` implies
 * `failed`, and checking `failed` first would relabel every timeout as a data
 * fault and erase the distinction. A genuine data fault never sets `timedOut`,
 * so nothing is lost the other way round.
 *
 * A pure constexpr function with no dependency on InboundLedger or
 * TransactionAcquire, so the whole rule is assertable from the lib-only test
 * binary, which cannot link xrpld.
 *
 * @param failed   The unit's `failed_` flag (terminal error).
 * @param complete The unit's `complete_` flag (all data assembled).
 * @param timedOut Whether the unit's retry budget expired before it ended.
 * @return `timeout`, `failed`, `complete` or `abandoned`, in that precedence.
 *
 * Example -- the healthy and the stuck case:
 * @code
 * phaseOutcome(false, true, false);  // "complete" -- assembled in time
 * phaseOutcome(true, false, true);   // "timeout"  -- budget gone; the failed_
 *                                    //  flag the same path sets is subsumed
 * @endcode
 *
 * Example -- edge case: a unit torn down mid-fetch with no flag set at all
 * still reports a value, so it stays in the outcome rate:
 * @code
 * phaseOutcome(false, false, false);  // "abandoned"
 * @endcode
 *
 * @note Pure and side-effect free; safe to call from any thread, including a
 *       destructor (it allocates nothing and cannot throw).
 */
[[nodiscard]] constexpr std::string_view
phaseOutcome(bool failed, bool complete, bool timedOut) noexcept
{
    if (timedOut)
        return val::timeout;
    if (failed)
        return val::failed;
    if (complete)
        return val::complete;
    return val::abandoned;
}

/**
 * Pick the `object_type` value for a served ledger-data request from the
 * protobuf `itype` the peer asked with.
 *
 * Kept as a rule rather than a per-branch literal because
 * `processLedgerRequest` reaches its exits from four different places, and a
 * per-branch value would let two of them disagree about the same request. It
 * takes a plain int so this header stays free of the protobuf headers and the
 * rule remains assertable from the lib-only test binary; the caller passes
 * `m->itype()`, whose enum values are fixed by the wire protocol.
 *
 * @param itype The protobuf TMLedgerInfoType: 0 `liBASE`, 1 `liTX_NODE`,
 *        2 `liAS_NODE`, 3 `liTS_CANDIDATE`.
 * @return `header`, `tx`, `as` or `txset`; `header` for an unrecognised value,
 *         which cannot occur because onMessage() rejects the request first.
 *
 * Example -- the two request kinds a syncing peer sends most:
 * @code
 * serveObjectType(2);  // "as"  -- account-state tree, the bulk of a sync
 * serveObjectType(0);  // "header" -- the cheap liBASE reply
 * @endcode
 *
 * @note Pure and side-effect free.
 */
[[nodiscard]] constexpr std::string_view
serveObjectType(int itype) noexcept
{
    switch (itype)
    {
        case 1:
            return val::txTree;
        case 2:
            return val::asTree;
        case 3:
            return val::txSet;
        default:
            return val::header;
    }
}

/**
 * Pick the `outcome` value for a served ledger-data request from the size of
 * the reply it produced.
 *
 * `processLedgerRequest` has eight exits and only one of them sends anything,
 * so a per-branch value would be seven chances to mislabel. Deriving it from
 * the reply itself removes that: the reply's node count is the one piece of
 * state that already exists at every exit, and is zero on all seven refusals.
 *
 * A reply that reached the soft cap is `partial`, not `complete`: the assembly
 * loop stopped early and the requester must ask again for the rest, so counting
 * it as a success would hide the round trips a large tree really costs.
 *
 * @param servedNodes Nodes in the reply, i.e. `ledgerData.nodes_size()`.
 * @param softCap     The reply-size cap the assembly loop stops at
 *        (`Tuning::kSoftMaxReplyNodes`). Passed in so this header needs no
 *        overlay dependency and the rule stays assertable from the lib-only
 *        test binary.
 * @return `refused` for an empty reply, `partial` at or above the cap,
 *         otherwise `complete`.
 *
 * Example -- the served and the refused case:
 * @code
 * serveOutcome(12, 128);   // "complete" -- whole request answered
 * serveOutcome(0, 128);    // "refused"  -- nothing sent; the paired
 *                          //  serve_refused_total counter says why
 * @endcode
 *
 * Example -- edge case: a reply that filled the cap is not a success, because
 * the peer must come back for the remainder:
 * @code
 * serveOutcome(128, 128);  // "partial"
 * @endcode
 *
 * @note Pure and side-effect free; safe to call while unwinding.
 */
[[nodiscard]] constexpr std::string_view
serveOutcome(int servedNodes, int softCap) noexcept
{
    if (servedNodes <= 0)
        return val::refused;
    if (servedNodes >= softCap)
        return val::partial;
    return val::complete;
}

}  // namespace xrpl::telemetry::ledger_span
