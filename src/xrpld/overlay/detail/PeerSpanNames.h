#pragma once

/**
 * Compile-time span name constants for peer overlay tracing.
 *
 *  Used by PeerImp for peer message handling spans (proposals,
 *  validations). Built on StaticStr/join() from SpanNames.h.
 *
 *  Span hierarchy:
 *
 *    peer.proposal.receive   (PeerImp — incoming proposal)
 *    peer.validation.receive (PeerImp — incoming validation)
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::peer_span {

// ===== Span operation suffixes ===============================================

namespace op {
inline constexpr auto proposalReceive = makeStr("proposal.receive");
inline constexpr auto validationReceive = makeStr("validation.receive");
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
}  // namespace attr

}  // namespace xrpl::telemetry::peer_span
