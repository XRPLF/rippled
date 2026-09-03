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
 *    |              early_close_triggered (at start); open_duration_ms,
 *    |              peer_positions_at_close, tx_sets_acquired, close_reason,
 *    |              proposers_validated (at close; absent if the round is
 *    |              recovered or simulated, neither of which reaches
 *    |              closeLedger())
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
 *    |     Attrs:   converge_percent, establish_count, proposers,
 *    |              disputes_count (overwritten each iteration);
 *    |              close_time_avalanche_state (terminal, at end)
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
 *    |     Attrs:   ledger_seq, close_time_ripple_epoch_s, close_time_correct,
 *    |              close_resolution_ms, consensus_state, proposing, round_time_ms,
 *    |              parent_close_time_ripple_epoch_s, close_time_self_ripple_epoch_s,
 *    |              close_time_vote_bins, resolution_direction, tx_count
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

#include <xrpl/telemetry/SpanNames.h>

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
using ::xrpl::telemetry::attr::closeTimeCorrect;
using ::xrpl::telemetry::attr::closeTimeRippleEpochS;
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
 * A handleWrongLedger recovery emits a SECOND phase.open span under the same
 * round, so `start_reason` is what tells the two apart.
 */
inline constexpr auto startReason = makeStr("start_reason");
inline constexpr auto previousCloseAgree = makeStr("previous_close_agree");
inline constexpr auto peerPositionsAtOpen = makeStr("peer_positions_at_open");
inline constexpr auto earlyCloseTriggered = makeStr("early_close_triggered");
/**
 * Open-phase end metadata (set on consensus.phase.open before reset).
 *
 * `proposers_validated` counts validators of the previous ledger, unlike
 * `proposers_finished` below, which counts those already past it.
 */
inline constexpr auto openDurationMs = makeStr("open_duration_ms");
inline constexpr auto peerPositionsAtClose = makeStr("peer_positions_at_close");
inline constexpr auto txSetsAcquired = makeStr("tx_sets_acquired");
inline constexpr auto closeReason = makeStr("close_reason");
inline constexpr auto proposersValidated = makeStr("proposers_validated");
/**
 * Ledger-close inputs.
 */
inline constexpr auto txCountOpen = makeStr("tx_count_open");
/**
 * Establish/check additional state.
 */
inline constexpr auto proposersFinished = makeStr("proposers_finished");
/**
 * Establish-phase end metadata.
 *
 * The terminal close-time regime, set once. Qualified because DisputedTx
 * tracks a SECOND, per-transaction avalanche; this is not that one. The
 * derived `avalanche_threshold` cannot be inverted back to it.
 */
inline constexpr auto closeTimeAvalancheState = makeStr("close_time_avalanche_state");
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
/**
 * Close-time instants, both NetClock readings in whole seconds since the XRP
 * Ledger epoch (2000-01-01T00:00:00Z) — see `closeTimeRippleEpochS` in
 * SpanNames.h for why the epoch is spelled into the key.
 *
 * `parentCloseTimeRippleEpochS` is the previous ledger's close time;
 * `closeTimeSelfRippleEpochS` is this node's own close-time vote for the round,
 * so the pair shows how far the node's position sat from the ledger it built on.
 *
 * `closeTimeVoteBins` is not a time: it holds the number of distinct close-time
 * positions seen from peers this round.
 */
inline constexpr auto parentCloseTimeRippleEpochS = makeStr("parent_close_time_ripple_epoch_s");
inline constexpr auto closeTimeSelfRippleEpochS = makeStr("close_time_self_ripple_epoch_s");
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

/**
 * "validation_status" — which exit the inbound validation took. Set once per
 * exit, so a dropped validation (microseconds) is separable from a queued one
 * (job wait plus checkValidation). Without it the span name reports two
 * unrelated latency distributions and every quantile over it is meaningless.
 */
inline constexpr auto validationStatus = makeStr("validation_status");
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
// close_time_avalanche_state values, one per AvalancheState enumerator.
inline constexpr auto avalancheInit = makeStr("init");
inline constexpr auto avalancheMid = makeStr("mid");
inline constexpr auto avalancheLate = makeStr("late");
inline constexpr auto avalancheStuck = makeStr("stuck");
// Sentinel for an unmapped enumerator, matching to_string(ConsensusPhase).
inline constexpr auto unknown = makeStr("unknown");
// close_reason values, one per LedgerCloseReason enumerator. keep_open is
// never emitted: the attribute is only set on the path that closes.
inline constexpr auto closeKeepOpen = makeStr("keep_open");
inline constexpr auto closeAnomaly = makeStr("anomaly");
inline constexpr auto closeOthersClosed = makeStr("others_closed");
inline constexpr auto closeIdle = makeStr("idle");
inline constexpr auto closeNormal = makeStr("normal");
// validation_status values, one per exit of the inbound validation path.
inline constexpr auto validationQueued = makeStr("queued");
inline constexpr auto validationDroppedDiverged = makeStr("dropped_diverged");
inline constexpr auto validationDroppedLoad = makeStr("dropped_load");
}  // namespace val

}  // namespace xrpl::telemetry::consensus::span
