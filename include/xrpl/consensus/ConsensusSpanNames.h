#pragma once

/**
 * Compile-time span name constants for consensus tracing.
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
 *    |  Attrs:   consensus_ledger_id, ledger_seq, consensus_mode,
 *    |           trace_strategy, consensus_round_id
 *    |
 *    +-- consensus.phase.open                    [main thread, child]
 *    |     Created: Consensus::startRoundInternal()
 *    |     Ended:   Consensus::closeLedger()
 *    |     Attrs:   start_reason, previous_close_agree, peer_positions_at_open,
 *    |              early_close_triggered (all at start); open_duration_ms,
 *    |              peer_positions_at_close, tx_sets_acquired (all at end)
 *    |
 *    +-- consensus.proposal.send                 [main thread]
 *    |     Created: Adaptor::propose()
 *    |     Attrs:   consensus_round (proposeSeq)
 *    |
 *    +-- consensus.ledger_close                  [main thread]
 *    |     Created: Adaptor::onClose()
 *    |     Attrs:   ledger_seq, consensus_mode
 *    |
 *    +-- consensus.establish                     [main thread, child]
 *    |     Created: Consensus::startEstablishTracing()
 *    |     Ended:   Consensus::phaseEstablish() on accept
 *    |     Attrs:   disputes_count_initial (at start); converge_percent,
 *    |              establish_count, proposers, disputes_count (overwritten
 *    |              each iteration); avalanche_state (terminal, at end)
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
 *    |     Attrs:   ledger_seq, close_time, close_time_correct,
 *    |              close_resolution_ms, consensus_state, proposing, round_time_ms,
 *    |              parent_close_time, close_time_self, close_time_vote_bins,
 *    |              resolution_direction, tx_count
 *    |     Events:  tx.included (per tx, attrs: tx_id)
 *    |
 *    +~~~ consensus.validation.send              [jtACCEPT thread, linked]
 *    |     Created: Adaptor::createValidationSpan() (follows-from link)
 *    |     Attrs:   ledger_seq, proposing
 *    |
 *    +-- consensus.mode_change                   [main thread]
 *          Created: Adaptor::onModeChange()
 *          Attrs:   mode_old, mode_new
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

#include <xrpl/consensus/ConsensusParms.h>
#include <xrpl/telemetry/SpanNames.h>

#include <string_view>

namespace xrpl::telemetry::consensus::span {

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
/**
 * Canonical shared constants (defined in SpanNames.h). `ledgerHash` and
 * `fullValidation` are shared with the peer.validation.receive span — same
 * concept, same key, distinguished by span name (not an emitter prefix).
 */
using ::xrpl::telemetry::attr::closeResolutionMs;
using ::xrpl::telemetry::attr::closeTime;
using ::xrpl::telemetry::attr::closeTimeCorrect;
using ::xrpl::telemetry::attr::fullValidation;
using ::xrpl::telemetry::attr::ledgerHash;
using ::xrpl::telemetry::attr::ledgerSeq;

/**
 * Domain-qualified attrs (rule 5 — bare name ambiguous across domains).
 * Use `<domain>_<field>` underscore form for TraceQL ergonomics.
 */
inline constexpr auto ledgerId = makeStr("consensus_ledger_id");
inline constexpr auto mode = makeStr("consensus_mode");
inline constexpr auto round = makeStr("consensus_round");
inline constexpr auto roundId = makeStr("consensus_round_id");
/**
 * Current phase name attached to consensus.round; updated on each
 * phase transition event (open/establish/accepted).
 */
inline constexpr auto consensusPhase = makeStr("consensus_phase");
/**
 * Boolean flag set on consensus.check when checkConsensus reports stalled.
 */
inline constexpr auto consensusStalled = makeStr("consensus_stalled");

/**
 * Domain-owned bare attrs.
 */
inline constexpr auto proposers = makeStr("proposers");
inline constexpr auto roundTimeMs = makeStr("round_time_ms");
inline constexpr auto proposing = makeStr("proposing");
/**
 * Round continuity / context attrs (set on consensus.round at round start).
 */
inline constexpr auto previousProposers = makeStr("previous_proposers");
inline constexpr auto previousRoundTimeMs = makeStr("previous_round_time_ms");
inline constexpr auto previousLedgerSeq = makeStr("previous_ledger_seq");
inline constexpr auto closeTimeResolutionMs = makeStr("close_time_resolution_ms");
/**
 * Open-phase start metadata (set on consensus.phase.open at creation).
 *
 * `start_reason` distinguishes a fresh round from a handleWrongLedger
 * recovery, which emits a SECOND consensus.phase.open span under the same
 * round span; without it the two are indistinguishable in a trace.
 * `early_close_triggered` records that startRoundInternal itself forced an
 * immediate timerEntry because peers had already closed.
 */
inline constexpr auto startReason = makeStr("start_reason");
inline constexpr auto previousCloseAgree = makeStr("previous_close_agree");
inline constexpr auto peerPositionsAtOpen = makeStr("peer_positions_at_open");
inline constexpr auto earlyCloseTriggered = makeStr("early_close_triggered");
/**
 * Open-phase end metadata (set on consensus.phase.open before reset).
 *
 * `tx_sets_acquired` counts the candidate transaction sets held when the
 * ledger closed; a low count next to a high peer_positions_at_close points
 * at missing tx-set fetches rather than at disagreement.
 */
inline constexpr auto openDurationMs = makeStr("open_duration_ms");
inline constexpr auto peerPositionsAtClose = makeStr("peer_positions_at_close");
inline constexpr auto txSetsAcquired = makeStr("tx_sets_acquired");
/**
 * Ledger-close inputs.
 */
inline constexpr auto txCountOpen = makeStr("tx_count_open");
/**
 * Establish/check additional state.
 */
inline constexpr auto proposersFinished = makeStr("proposers_finished");
/**
 * Establish-phase start/end metadata.
 *
 * `disputes_count_initial` is the dispute count carried into the establish
 * phase from the positions held at close. It is set once, unlike
 * `disputes_count`, which updateEstablishTracing() overwrites every
 * iteration and so only ever reports the final value.
 * `avalanche_state` is the terminal close-time convergence regime, set once
 * when the establish span ends; the derived `avalanche_threshold` on
 * consensus.update_positions cannot be inverted back to it.
 */
inline constexpr auto disputesCountInitial = makeStr("disputes_count_initial");
inline constexpr auto avalancheState = makeStr("avalanche_state");
/**
 * Accept/apply enrichment.
 */
inline constexpr auto disputesResolvedCount = makeStr("disputes_resolved_count");
/**
 * Validation send/receive enrichment. (`full_validation` is shared — see the
 * `using` re-export above.)
 */
inline constexpr auto validationSignTime = makeStr("validation_sign_time");
/**
 * Receive-side hash prefixes for cross-peer correlation.
 */
inline constexpr auto prevLedgerPrefix = makeStr("prev_ledger_prefix");
inline constexpr auto positionHashPrefix = makeStr("position_hash_prefix");
/**
 * "consensus_state" — domain-qualified (collides with other domains' state).
 */
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
/**
 * "consensus_result" — domain-qualified (collides with generic result).
 */
inline constexpr auto consensusResult = makeStr("consensus_result");
inline constexpr auto quorum = makeStr("quorum");
inline constexpr auto traceStrategy = makeStr("trace_strategy");
inline constexpr auto modeOld = makeStr("mode_old");
inline constexpr auto modeNew = makeStr("mode_new");

/**
 * "is_bow_out" — whether this proposal is a bow-out (resigning from round).
 */
inline constexpr auto isBowOut = makeStr("is_bow_out");

/**
 * Transaction/dispute attrs used in consensus accept spans.
 */
inline constexpr auto txId = makeStr("tx_id");
inline constexpr auto disputeOurVote = makeStr("dispute_our_vote");
inline constexpr auto disputeYays = makeStr("dispute_yays");
inline constexpr auto disputeNays = makeStr("dispute_nays");
inline constexpr auto txCount = makeStr("tx_count");
inline constexpr auto disputesCount = makeStr("disputes_count");
/**
 * Trust flag (is the message origin a trusted UNL validator). Qualified by
 * message type, shared with the peer.{proposal,validation}.receive spans:
 * consensus.proposal.receive uses `proposal_trusted`, consensus.validation.
 * receive uses `validation_trusted`. Same concept on both emitters → same key.
 */
inline constexpr auto proposalTrusted = makeStr("proposal_trusted");
inline constexpr auto validationTrusted = makeStr("validation_trusted");
}  // namespace attr

// ===== Event names ===========================================================

namespace event {
/**
 * "dispute.resolve"
 */
inline constexpr auto disputeResolve = join(makeStr("dispute"), makeStr("resolve"));
/**
 * "tx.included"
 */
inline constexpr auto txIncluded = join(makeStr("tx"), makeStr("included"));

/**
 * Phase transition events — fired on consensus.round at each transition
 * so the round-level span carries a complete timeline of phase changes,
 * including the handleWrongLedger recovery edge that re-enters Open.
 */
inline constexpr auto phaseOpen = join(makeStr("phase"), makeStr("open"));
inline constexpr auto phaseEstablish = join(makeStr("phase"), makeStr("establish"));
inline constexpr auto phaseAccepted = join(makeStr("phase"), makeStr("accepted"));
inline constexpr auto phaseRecovery = join(makeStr("phase"), makeStr("recovery"));

/**
 * Outcome events — fired on consensus.round at the establish→accepted
 * transition so the path that drove acceptance is queryable.
 */
inline constexpr auto outcomeYes = join(makeStr("outcome"), makeStr("yes"));
inline constexpr auto outcomeMovedOn = join(makeStr("outcome"), makeStr("moved_on"));
inline constexpr auto outcomeExpired = join(makeStr("outcome"), makeStr("expired"));
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
// consensus_phase attribute values (the phase the round is entering).
inline constexpr auto phaseOpen = makeStr("open");
inline constexpr auto phaseEstablish = makeStr("establish");
inline constexpr auto phaseAccepted = makeStr("accepted");
// start_reason values (how startRoundInternal was entered).
inline constexpr auto startInitial = makeStr("initial");
inline constexpr auto startRecovered = makeStr("recovered");
// avalanche_state values, one per ConsensusParms::AvalancheState enumerator.
inline constexpr auto avalancheInit = makeStr("init");
inline constexpr auto avalancheMid = makeStr("mid");
inline constexpr auto avalancheLate = makeStr("late");
inline constexpr auto avalancheStuck = makeStr("stuck");
}  // namespace val

/**
 * Map a close-time avalanche state to its `avalanche_state` label.
 *
 * The regime escalates Init -> Mid -> Late -> Stuck as a round takes longer
 * to converge, raising the close-time agreement threshold at each step. The
 * label is recorded once, when the establish span ends, so the terminal
 * regime of the round is queryable.
 *
 * @param state The state held by Consensus::closeTimeAvalancheState_.
 * @return The wire label; one of val::avalanche*.
 *
 * @note constexpr so the call site costs nothing. Every enumerator is
 * handled explicitly; there is no default arm, so adding an enumerator to
 * ConsensusParms::AvalancheState makes the switch fall through to the
 * unreachable return rather than silently mislabelling a new regime.
 */
[[nodiscard]] constexpr std::string_view
avalancheStateLabel(ConsensusParms::AvalancheState const state)
{
    switch (state)
    {
        case ConsensusParms::AvalancheState::Init:
            return val::avalancheInit;
        case ConsensusParms::AvalancheState::Mid:
            return val::avalancheMid;
        case ConsensusParms::AvalancheState::Late:
            return val::avalancheLate;
        case ConsensusParms::AvalancheState::Stuck:
            return val::avalancheStuck;
    }
    return val::avalancheInit;
}

}  // namespace xrpl::telemetry::consensus::span
