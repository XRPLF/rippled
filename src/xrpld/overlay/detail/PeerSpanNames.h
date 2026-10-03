#pragma once

/**
 * Compile-time span name constants for peer overlay tracing.
 *
 *  Used by PeerImp for peer message handling spans (proposals,
 *  validations) and by ConnectAttempt for the outbound dial span.
 *  Built on StaticStr/join() from SpanNames.h.
 *
 *  Span hierarchy:
 *
 *    peer.proposal.receive   (PeerImp — incoming proposal)
 *    peer.validation.receive (PeerImp — incoming validation)
 *    peer.dial               (ConnectAttempt — outbound connect attempt)
 *
 *  peer.dial is a trace root: it is the first thing a fresh node does, so
 *  nothing exists yet to parent it to.
 *
 *      +---------------+  starts   +----------------------------+
 *      | ConnectAttempt|---------->| span "peer.dial"           |
 *      |  ::run()      |           |  outcome / remote_endpoint |
 *      +---------------+           |  duration_ms               |
 *              |                   +----------------------------+
 *              | one terminal path ends it (reportOutcome)
 *              v
 *      onTimer / onConnect / onHandshake / onWrite / onRead /
 *      onShutdown / processResponse
 */

#include <xrpl/telemetry/MetricNames.h>
#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::peer_span {

// ===== Span operation suffixes ===============================================

namespace op {
inline constexpr auto proposalReceive = makeStr("proposal.receive");
inline constexpr auto validationReceive = makeStr("validation.receive");
inline constexpr auto dial = makeStr("dial");
}  // namespace op

// ===== Attribute keys ========================================================

namespace attr {
/**
 * Canonical shared constants (defined in SpanNames.h). `ledgerHash` and
 * `fullValidation` are shared with the consensus validation spans — same
 * concept, same key, told apart by span name.
 */
using ::xrpl::telemetry::attr::fullValidation;
using ::xrpl::telemetry::attr::ledgerHash;
using ::xrpl::telemetry::attr::peerId;

/**
 * Trust flag qualified by message type — whether the sending key is on this
 * node's UNL.
 *
 * The literals match consensus::span::attr::proposalTrusted and
 * ::validationTrusted, so the peer and consensus receive spans report on one
 * spanmetrics dimension instead of two. Unlike the constants above these are
 * declared here rather than re-exported from SpanNames.h, so the two spellings
 * are only kept equal by hand: change one and the dimension splits silently.
 */
inline constexpr auto proposalTrusted = makeStr("proposal_trusted");
inline constexpr auto validationTrusted = makeStr("validation_trusted");

/**
 * peer.dial attrs (outbound connect attempt).
 *
 * `outcome` is the same terminal-reason set the `overlay_connect_total`
 * counter already labels with, so the span and the counter can be read
 * against each other. `remoteEndpoint` says WHICH peer, which the counter
 * deliberately cannot carry: one series per peer address would be unbounded
 * cardinality, so it stays span-only and Tempo-searchable instead.
 * `durationMs` mirrors the `overlay_dial_latency_ms` histogram value onto the
 * individual attempt, so one slow dial is findable rather than only visible
 * in an aggregate p95.
 */
inline constexpr auto remoteEndpoint = makeStr("remote_endpoint");
inline constexpr auto durationMs = makeStr("duration_ms");
inline constexpr auto outcome = makeStr("outcome");
}  // namespace attr

// ===== Attribute values ======================================================

namespace val {
/**
 * peer.dial outcome values: the `overlay_connect_total` label values from
 * MetricNames.h, where each one is described. Four come from
 * `lval::overlay_connect`, and `self_connection` and `timeout` from the
 * shared `lval` slugs.
 *
 * Built from those constants rather than spelled again, so the span attribute
 * and the counter label cannot drift apart: ConnectAttempt::reportOutcome()
 * passes one value to both. `self_connection` is the shared slug that
 * `handshake_negotiation_fail_total` also uses, so a self-dial reads the same
 * on both counters.
 */
inline constexpr auto connected = makeStr(lval::overlay_connect::connected);
inline constexpr auto tcpFail = makeStr(lval::overlay_connect::tcpFail);
inline constexpr auto tlsFail = makeStr(lval::overlay_connect::tlsFail);
inline constexpr auto selfConnection = makeStr(lval::selfConnection);
inline constexpr auto upgradeFail = makeStr(lval::overlay_connect::upgradeFail);
inline constexpr auto timeout = makeStr(lval::timeout);
}  // namespace val

}  // namespace xrpl::telemetry::peer_span
