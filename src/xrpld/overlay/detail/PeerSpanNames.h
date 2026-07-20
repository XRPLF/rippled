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
 * Trust flag qualified by message type, shared with consensus.*.receive.
 */
inline constexpr auto proposalTrusted = makeStr("proposal_trusted");
inline constexpr auto validationTrusted = makeStr("validation_trusted");
}  // namespace attr

}  // namespace xrpl::telemetry::peer_span
