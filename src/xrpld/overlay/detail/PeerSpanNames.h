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
 * Trust flag qualified by message type, shared with consensus.*.receive.
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
 * peer.dial outcome values.
 *
 * The identical slugs `ConnectAttempt::reportOutcome` passes to the
 * `overlay_connect_total` counter, defined here so the span and the counter
 * cannot drift apart: the dial state machine names its outcome once and both
 * signals receive that same value.
 *
 * - connected:    the peer was activated and added to the overlay.
 * - tcp_fail:     the TCP connect or local-endpoint read failed.
 * - tls_fail:     the TLS handshake or the shared-value exchange failed.
 * - duplicate:    TLS succeeded but PeerFinder already holds a slot for this
 *                 address, so the attempt was redundant rather than faulty.
 * - upgrade_fail: TLS succeeded but the HTTP upgrade, protocol negotiation
 *                 or activation was rejected.
 * - timeout:      the attempt never reached any terminal state in time.
 *
 * `duplicate` is separate from `tls_fail` on purpose. Dialling an address we
 * are already connected to is normal churn on a healthy node, while a TLS
 * failure means the peer could not be spoken to at all. Reporting both as
 * `tls_fail` made a rising TLS-failure count unreadable: it could equally mean
 * broken peers or merely a busy PeerFinder, and the two need opposite
 * responses.
 */
inline constexpr auto connected = makeStr("connected");
inline constexpr auto tcpFail = makeStr("tcp_fail");
inline constexpr auto tlsFail = makeStr("tls_fail");
inline constexpr auto duplicate = makeStr("duplicate");
inline constexpr auto upgradeFail = makeStr("upgrade_fail");
inline constexpr auto timeout = makeStr("timeout");
}  // namespace val

}  // namespace xrpl::telemetry::peer_span
