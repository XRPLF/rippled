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
 *    |     Attrs:   converge_percent, establish_count, proposers
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

namespace xrpl::telemetry::cons_span {

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
/// Canonical shared constants (defined in SpanNames.h).
using ::xrpl::telemetry::attr::closeResolutionMs;
using ::xrpl::telemetry::attr::closeTime;
using ::xrpl::telemetry::attr::closeTimeCorrect;
using ::xrpl::telemetry::attr::ledgerSeq;

/// Kept qualified (rule 5 — bare name ambiguous across domains).
inline constexpr auto ledgerId = join(join(seg::xrpl, seg::consensus), makeStr("ledger_id"));
inline constexpr auto mode = join(join(seg::xrpl, seg::consensus), makeStr("mode"));
inline constexpr auto round = join(join(seg::xrpl, seg::consensus), makeStr("round"));
inline constexpr auto roundId = join(join(seg::xrpl, seg::consensus), makeStr("round_id"));

/// Domain-owned bare attrs.
inline constexpr auto proposers = makeStr("proposers");
inline constexpr auto roundTimeMs = makeStr("round_time_ms");
inline constexpr auto proposing = makeStr("proposing");
/// "consensus_state" — domain-qualified (collides with other domains' state).
inline constexpr auto consensusState = makeStr("consensus_state");
inline constexpr auto parentCloseTime = makeStr("parent_close_time");
inline constexpr auto closeTimeSelf = makeStr("close_time_self");
inline constexpr auto closeTimeVoteBins = makeStr("close_time_vote_bins");
inline constexpr auto resolutionDirection = makeStr("resolution_direction");
inline constexpr auto convergePercent = makeStr("converge_percent");
inline constexpr auto establishCount = makeStr("establish_count");
inline constexpr auto avalancheThreshold = makeStr("avalanche_threshold");
inline constexpr auto closeTimeThreshold = makeStr("close_time_threshold");
inline constexpr auto haveCloseTimeConsensus = makeStr("have_close_time_consensus");
inline constexpr auto agreeCount = makeStr("agree_count");
inline constexpr auto disagreeCount = makeStr("disagree_count");
inline constexpr auto thresholdPercent = makeStr("threshold_percent");
/// "consensus_result" — domain-qualified (collides with generic result).
inline constexpr auto consensusResult = makeStr("consensus_result");
inline constexpr auto quorum = makeStr("quorum");
inline constexpr auto traceStrategy = makeStr("trace_strategy");
inline constexpr auto modeOld = makeStr("mode_old");
inline constexpr auto modeNew = makeStr("mode_new");

/// Transaction/dispute attrs used in consensus accept spans.
inline constexpr auto txId = join(join(seg::xrpl, seg::tx), makeStr("id"));
inline constexpr auto disputeOurVote = makeStr("dispute_our_vote");
inline constexpr auto disputeYays = makeStr("dispute_yays");
inline constexpr auto disputeNays = makeStr("dispute_nays");
inline constexpr auto txCount = makeStr("tx_count");
inline constexpr auto disputesCount = makeStr("disputes_count");
inline constexpr auto trusted = makeStr("trusted");
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

}  // namespace xrpl::telemetry::cons_span
