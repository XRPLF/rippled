#pragma once

/** Compile-time span name and attribute constants for consensus tracing.
 *
 *  Used by PeerImp (overlay) and RCLConsensus (consensus) for proposal
 *  and validation lifecycle spans. Built on StaticStr/join() from
 *  SpanNames.h and follows the rule-5 underscore form for shared
 *  cross-span attributes (e.g. `consensus_round`, `ledger_seq`).
 *
 *  Phase 3 introduces the receive-side surface used by PeerImp.
 *  Phase 4 will extend this with the proposer/validator-side spans
 *  (`consensus.proposal.send`, `consensus.validation.send`, round
 *  bookkeeping, etc.).
 *
 *  Span hierarchy (cross-node propagation):
 *
 *    Node A (sender)                       Node B (receiver)
 *    +----------------------------+        +-------------------------------+
 *    | consensus.proposal/...send |  proto | consensus.proposal/...receive |
 *    |  inject trace context      | -----> |  proposalReceiveSpan() /      |
 *    |  (RCLConsensus broadcast)  | t_ctx  |  validationReceiveSpan()      |
 *    +----------------------------+        +-------------------------------+
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl::telemetry::cons_span {

// ===== Span prefixes =======================================================

namespace prefix {
/// "consensus" — root prefix for consensus lifecycle spans.
inline constexpr auto consensus = seg::consensus;
/// "consensus.proposal" — proposal sub-tree.
inline constexpr auto proposal = join(consensus, makeStr("proposal"));
/// "consensus.validation" — validation sub-tree.
inline constexpr auto validation = join(consensus, makeStr("validation"));
}  // namespace prefix

// ===== Span operation suffixes =============================================

namespace op {
inline constexpr auto receive = makeStr("receive");
inline constexpr auto send = makeStr("send");
}  // namespace op

// ===== Full span names =====================================================

inline constexpr auto proposalReceive = join(prefix::proposal, op::receive);
inline constexpr auto validationReceive = join(prefix::validation, op::receive);

// ===== Attribute keys ======================================================

namespace attr {
/// Canonical shared constants (defined in SpanNames.h).
using ::xrpl::telemetry::attr::ledgerSeq;

/// "trusted" — bare field; whether the proposing/validating node is
/// in our UNL. Used only on consensus spans, no cross-domain collision.
inline constexpr auto trusted = makeStr("trusted");

/// "consensus_round" — propose-sequence within a consensus round
/// (rule-5 underscore form, shared across consensus spans).
inline constexpr auto round = makeStr("consensus_round");
}  // namespace attr

}  // namespace xrpl::telemetry::cons_span
