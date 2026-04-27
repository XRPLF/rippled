#pragma once

/** Compile-time span name constants for consensus tracing.
 *
 *  Used by RCLConsensus (app) and Consensus.h (template) for
 *  consensus lifecycle spans. Built on StaticStr/join() from SpanNames.h.
 *
 *  Span hierarchy:
 *
 *    consensus.round (deterministic trace_id from ledger hash)
 *    |
 *    +-- consensus.proposal.send
 *    +-- consensus.ledger_close
 *    +-- consensus.establish
 *    +-- consensus.update_positions
 *    +-- consensus.check
 *    +-- consensus.accept
 *    +-- consensus.accept.apply     (jtACCEPT thread)
 *    +-- consensus.validation.send  (jtACCEPT thread, linked)
 *    +-- consensus.mode_change
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl {
namespace telemetry {
namespace cons_span {

// ===== Span name segments ====================================================

namespace op {
inline constexpr auto round = makeStr("round");
inline constexpr auto proposalSend = makeStr("proposal.send");
inline constexpr auto ledgerClose = makeStr("ledger_close");
inline constexpr auto establish = makeStr("establish");
inline constexpr auto updatePositions = makeStr("update_positions");
inline constexpr auto check = makeStr("check");
inline constexpr auto accept = makeStr("accept");
inline constexpr auto acceptApply = makeStr("accept.apply");
inline constexpr auto validationSend = makeStr("validation.send");
inline constexpr auto modeChange = makeStr("mode_change");
}  // namespace op

// ===== Full span names (prefix.op) ===========================================

inline constexpr auto round = join(seg::consensus, op::round);
inline constexpr auto proposalSend = join(seg::consensus, op::proposalSend);
inline constexpr auto ledgerClose = join(seg::consensus, op::ledgerClose);
inline constexpr auto establish = join(seg::consensus, op::establish);
inline constexpr auto updatePositions = join(seg::consensus, op::updatePositions);
inline constexpr auto check = join(seg::consensus, op::check);
inline constexpr auto accept = join(seg::consensus, op::accept);
inline constexpr auto acceptApply = join(seg::consensus, op::acceptApply);
inline constexpr auto validationSend = join(seg::consensus, op::validationSend);
inline constexpr auto modeChange = join(seg::consensus, op::modeChange);

// ===== Attribute keys ========================================================

namespace attr {
inline constexpr auto xrplConsensus = join(seg::xrpl, seg::consensus);

/// "xrpl.consensus.ledger_id"
inline constexpr auto ledgerId = join(xrplConsensus, makeStr("ledger_id"));
/// "xrpl.consensus.ledger.seq"
inline constexpr auto ledgerSeq = join(xrplConsensus, makeStr("ledger.seq"));
/// "xrpl.consensus.mode"
inline constexpr auto mode = join(xrplConsensus, makeStr("mode"));
/// "xrpl.consensus.round"
inline constexpr auto round = join(xrplConsensus, makeStr("round"));
/// "xrpl.consensus.proposers"
inline constexpr auto proposers = join(xrplConsensus, makeStr("proposers"));
/// "xrpl.consensus.round_time_ms"
inline constexpr auto roundTimeMs = join(xrplConsensus, makeStr("round_time_ms"));
/// "xrpl.consensus.proposing"
inline constexpr auto proposing = join(xrplConsensus, makeStr("proposing"));
/// "xrpl.consensus.state"
inline constexpr auto state = join(xrplConsensus, makeStr("state"));

// Close time attributes
/// "xrpl.consensus.close_time"
inline constexpr auto closeTime = join(xrplConsensus, makeStr("close_time"));
/// "xrpl.consensus.close_time_correct"
inline constexpr auto closeTimeCorrect = join(xrplConsensus, makeStr("close_time_correct"));
/// "xrpl.consensus.close_resolution_ms"
inline constexpr auto closeResolutionMs = join(xrplConsensus, makeStr("close_resolution_ms"));
/// "xrpl.consensus.parent_close_time"
inline constexpr auto parentCloseTime = join(xrplConsensus, makeStr("parent_close_time"));
/// "xrpl.consensus.close_time_self"
inline constexpr auto closeTimeSelf = join(xrplConsensus, makeStr("close_time_self"));
/// "xrpl.consensus.close_time_vote_bins"
inline constexpr auto closeTimeVoteBins = join(xrplConsensus, makeStr("close_time_vote_bins"));
/// "xrpl.consensus.resolution_direction"
inline constexpr auto resolutionDirection = join(xrplConsensus, makeStr("resolution_direction"));

// Establish/convergence attributes
/// "xrpl.consensus.converge_percent"
inline constexpr auto convergePercent = join(xrplConsensus, makeStr("converge_percent"));
/// "xrpl.consensus.establish_count"
inline constexpr auto establishCount = join(xrplConsensus, makeStr("establish_count"));
/// "xrpl.consensus.proposers_agreed"
inline constexpr auto proposersAgreed = join(xrplConsensus, makeStr("proposers_agreed"));

// Avalanche threshold attributes
/// "xrpl.consensus.avalanche_threshold"
inline constexpr auto avalancheThreshold = join(xrplConsensus, makeStr("avalanche_threshold"));
/// "xrpl.consensus.close_time_threshold"
inline constexpr auto closeTimeThreshold = join(xrplConsensus, makeStr("close_time_threshold"));
/// "xrpl.consensus.have_close_time_consensus"
inline constexpr auto haveCloseTimeConsensus =
    join(xrplConsensus, makeStr("have_close_time_consensus"));

// Consensus check attributes
/// "xrpl.consensus.agree_count"
inline constexpr auto agreeCount = join(xrplConsensus, makeStr("agree_count"));
/// "xrpl.consensus.disagree_count"
inline constexpr auto disagreeCount = join(xrplConsensus, makeStr("disagree_count"));
/// "xrpl.consensus.threshold_percent"
inline constexpr auto thresholdPercent = join(xrplConsensus, makeStr("threshold_percent"));
/// "xrpl.consensus.result"
inline constexpr auto result = join(xrplConsensus, makeStr("result"));
/// "xrpl.consensus.quorum"
inline constexpr auto quorum = join(xrplConsensus, makeStr("quorum"));
/// "xrpl.consensus.validation_count"
inline constexpr auto validationCount = join(xrplConsensus, makeStr("validation_count"));

// Trace strategy attribute
/// "xrpl.consensus.trace_strategy"
inline constexpr auto traceStrategy = join(xrplConsensus, makeStr("trace_strategy"));
/// "xrpl.consensus.round_id"
inline constexpr auto roundId = join(xrplConsensus, makeStr("round_id"));

// Mode change attributes
/// "xrpl.consensus.mode.old"
inline constexpr auto modeOld = join(xrplConsensus, makeStr("mode.old"));
/// "xrpl.consensus.mode.new"
inline constexpr auto modeNew = join(xrplConsensus, makeStr("mode.new"));

// Dispute event attributes
/// "xrpl.tx.id"
inline constexpr auto txId = join(join(seg::xrpl, seg::tx), makeStr("id"));
/// "xrpl.dispute.our_vote"
inline constexpr auto disputeOurVote =
    join(join(seg::xrpl, makeStr("dispute")), makeStr("our_vote"));
/// "xrpl.dispute.yays"
inline constexpr auto disputeYays = join(join(seg::xrpl, makeStr("dispute")), makeStr("yays"));
/// "xrpl.dispute.nays"
inline constexpr auto disputeNays = join(join(seg::xrpl, makeStr("dispute")), makeStr("nays"));
}  // namespace attr

// ===== Attribute values ======================================================

namespace val {
inline constexpr auto finished = makeStr("finished");
inline constexpr auto movedOn = makeStr("moved_on");
inline constexpr auto yes = makeStr("yes");
inline constexpr auto no = makeStr("no");
inline constexpr auto expired = makeStr("expired");
inline constexpr auto increased = makeStr("increased");
inline constexpr auto decreased = makeStr("decreased");
inline constexpr auto unchanged = makeStr("unchanged");
}  // namespace val

}  // namespace cons_span
}  // namespace telemetry
}  // namespace xrpl
