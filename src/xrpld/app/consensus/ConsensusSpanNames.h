#pragma once

/** Compile-time span name constants for consensus tracing.
 *
 *  Used by RCLConsensus (app), Consensus.h (template), and PeerImp
 *  (overlay) for consensus lifecycle spans.
 *  Built on StaticStr/join() from SpanNames.h.
 *
 *  ## Span Hierarchy
 *
 *  Root span created in Adaptor::startRoundTracing().  In "deterministic"
 *  strategy the trace-id is derived from the previous ledger hash so all
 *  nodes tracing the same round share a trace.
 *
 *    consensus.round                             [main thread, root]
 *    |  Created: Adaptor::startRoundTracing()
 *    |  Attrs:   ledger_id, ledger.seq, mode, trace_strategy, round_id
 *    |
 *    +-- consensus.phase.open                    [main thread, child]
 *    |     Created: Consensus::startRoundInternal()
 *    |     Ended:   Consensus::closeLedger()
 *    |
 *    +-- consensus.proposal.send                 [main thread]
 *    |     Created: Adaptor::propose()
 *    |     Attrs:   round (proposeSeq)
 *    |
 *    +-- consensus.ledger_close                  [main thread]
 *    |     Created: Adaptor::onClose()
 *    |     Attrs:   ledger.seq, mode
 *    |
 *    +-- consensus.establish                     [main thread, child]
 *    |     Created: Consensus::startEstablishTracing()
 *    |     Ended:   Consensus::phaseEstablish() on accept
 *    |     Attrs:   converge_percent, tx_count, disputes_count
 *    |
 *    +-- consensus.update_positions              [main thread]
 *    |     Created: Consensus::updateOurPositions()
 *    |     Attrs:   converge_percent, proposers, disputes_count
 *    |     Events:  per-dispute vote details (tx_id, our_vote, yays, nays)
 *    |
 *    +-- consensus.check                         [main thread]
 *    |     Created: Consensus::haveConsensus()
 *    |     Attrs:   agree/disagree counts, threshold_percent, result
 *    |
 *    +-- consensus.accept                        [main thread, child of round]
 *    |     Created: Adaptor::makeAcceptSpan(), shared_ptr kept alive
 *    |              until doAccept() completes on jtACCEPT thread
 *    |     Attrs:   proposers, round_time_ms, quorum
 *    |   |
 *    |   +-- consensus.accept.apply              [jtACCEPT thread, child of accept]
 *    |         Created: Adaptor::doAccept()
 *    |     Attrs:   ledger.seq, close_time, close_time_correct,
 *    |              close_resolution_ms, state, proposing, round_time_ms,
 *    |              parent_close_time, close_time_self, close_time_vote_bins,
 *    |              resolution_direction, tx_count
 *    |     Events:  tx.included (per tx)
 *    |
 *    +~~~ consensus.validation.send              [jtACCEPT thread, linked]
 *    |     Created: Adaptor::createValidationSpan() (follows-from link)
 *    |     Attrs:   ledger.seq, proposing
 *    |
 *    +-- consensus.mode_change                   [main thread]
 *          Created: Adaptor::onModeChange()
 *          Attrs:   mode.old, mode.new
 *
 *  Standalone spans (no parent, created per-message in overlay):
 *
 *    consensus.proposal.receive                  [PeerImp I/O thread]
 *      Created: PeerImp::onMessage(TMProposeSet)
 *
 *    consensus.validation.receive                [PeerImp I/O thread]
 *      Created: PeerImp::onMessage(TMValidation)
 *
 *  Legend:
 *    +--  child-of relationship (same trace)
 *    +~~~ follows-from link (separate sub-tree, causal link)
 */

#include <xrpl/telemetry/SpanNames.h>

namespace xrpl {
namespace telemetry {
namespace cons_span {

// ===== Span name segments ====================================================

namespace part {
inline constexpr auto proposal = makeStr("proposal");
inline constexpr auto validation = makeStr("validation");
inline constexpr auto accept = makeStr("accept");
inline constexpr auto phase = makeStr("phase");
}  // namespace part

namespace op {
inline constexpr auto round = makeStr("round");
inline constexpr auto proposalSend = join(part::proposal, makeStr("send"));
inline constexpr auto ledgerClose = makeStr("ledger_close");
inline constexpr auto establish = makeStr("establish");
inline constexpr auto updatePositions = makeStr("update_positions");
inline constexpr auto check = makeStr("check");
inline constexpr auto accept = makeStr("accept");
inline constexpr auto acceptApply = join(part::accept, makeStr("apply"));
inline constexpr auto validationSend = join(part::validation, makeStr("send"));
inline constexpr auto modeChange = makeStr("mode_change");
inline constexpr auto proposalReceive = join(part::proposal, makeStr("receive"));
inline constexpr auto validationReceive = join(part::validation, makeStr("receive"));
inline constexpr auto phaseOpen = join(part::phase, makeStr("open"));
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
inline constexpr auto proposalReceive = join(seg::consensus, op::proposalReceive);
inline constexpr auto validationReceive = join(seg::consensus, op::validationReceive);
inline constexpr auto phaseOpen = join(seg::consensus, op::phaseOpen);

// ===== Attribute keys ========================================================

namespace attr {
inline constexpr auto xrplConsensus = join(seg::xrpl, seg::consensus);

/// "xrpl.consensus.ledger_id"
inline constexpr auto ledgerId = join(xrplConsensus, makeStr("ledger_id"));
/// "xrpl.consensus.ledger.seq"
inline constexpr auto ledgerSeq = join(join(xrplConsensus, makeStr("ledger")), makeStr("seq"));
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
inline constexpr auto modeOld = join(join(xrplConsensus, makeStr("mode")), makeStr("old"));
/// "xrpl.consensus.mode.new"
inline constexpr auto modeNew = join(join(xrplConsensus, makeStr("mode")), makeStr("new"));

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

/// "xrpl.consensus.tx_count"
inline constexpr auto txCount = join(xrplConsensus, makeStr("tx_count"));
/// "xrpl.consensus.disputes_count"
inline constexpr auto disputesCount = join(xrplConsensus, makeStr("disputes_count"));
/// "xrpl.consensus.trusted"
inline constexpr auto trusted = join(xrplConsensus, makeStr("trusted"));
}  // namespace attr

// ===== Event names ===========================================================

namespace event {
/// "dispute.resolve"
inline constexpr auto disputeResolve = join(makeStr("dispute"), makeStr("resolve"));
/// "tx.included"
inline constexpr auto txIncluded = join(makeStr("tx"), makeStr("included"));
}  // namespace event

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
