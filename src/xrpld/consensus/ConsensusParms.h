/** @file
 *  Single source of truth for every numeric constant governing the XRP Ledger
 *  consensus algorithm.
 *
 *  Two temporal domains are represented: validation/proposal parameters use
 *  NetClock second resolution because they are compared against ledger close
 *  timestamps shared across the network; consensus-loop timers use millisecond
 *  resolution against an internal monotonic clock for sub-second granularity.
 *  Do not mix values from these two domains.
 */

#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>

namespace xrpl {

/** Immutable bundle of tuning constants for the consensus algorithm.
 *
 *  All members are `const`, making instances effectively compile-time
 *  named-constant bundles. Simulation harnesses may construct a custom
 *  instance with alternate values injected via designated initializers;
 *  production code uses the defaults.
 *
 *  @note Two temporal domains coexist in this struct. Parameters compared
 *      against network ledger close times (validations, proposals) are in
 *      NetClock seconds. Parameters governing the internal consensus timer
 *      are in milliseconds. See member-group comments for which domain
 *      each constant belongs to.
 */
struct ConsensusParms
{
    explicit ConsensusParms() = default;

    // --- NetClock-domain parameters (second resolution) ---

    /** Maximum age of a validation relative to its ledger's close time.
     *
     *  Validations whose `signTime` is more than this duration after the
     *  ledger's close time are rejected. Guards against stale validations
     *  that reference ledger close timestamps far in the past and protects
     *  the close-time accuracy adjustment window.
     */
    std::chrono::seconds const validationVALID_WALL = std::chrono::minutes{5};

    /** Maximum age of a validation relative to when we first observed it.
     *
     *  A validation is kept current for this long after the local wall clock
     *  at first observation, independent of the ledger close time. Enables
     *  faster recovery in rare network conditions where the total number of
     *  validations produced is unusually low.
     */
    std::chrono::seconds const validationVALID_LOCAL = std::chrono::minutes{3};

    /** How far before a ledger's close time a validation is still acceptable.
     *
     *  A validation timestamped up to this duration *before* the ledger's
     *  recorded close time is accepted. Provides tolerance for extreme
     *  inter-node clock skew without rejecting honest validators.
     */
    std::chrono::seconds const validationVALID_EARLY = std::chrono::minutes{3};

    /** Maximum age of a peer proposal before it is discarded as stale.
     *
     *  Proposals older than this threshold are pruned during each call to
     *  `updateOurPositions()`. Must be greater than `proposeINTERVAL` so
     *  that actively-proposing peers are never pruned.
     */
    std::chrono::seconds const proposeFRESHNESS = std::chrono::seconds{20};

    /** How often a node re-broadcasts its position to keep it fresh.
     *
     *  Even when the node's position has not changed, it re-proposes on
     *  this interval to prevent peers from discarding it as stale. Must
     *  be less than `proposeFRESHNESS`.
     */
    std::chrono::seconds const proposeINTERVAL = std::chrono::seconds{12};

    // --- Monotonic-clock-domain parameters (millisecond resolution) ---

    /** Minimum fraction of peers that must agree before consensus is declared.
     *
     *  Used by `checkConsensus()` and `DisputedTx::stalled()`. A round
     *  reaches consensus only when at least this percentage of current
     *  proposers agree on the same tx set. Also used as the split threshold
     *  for stall detection: a vote is considered stalled when the winning
     *  side exceeds this percentage in either direction.
     */
    std::size_t const minCONSENSUS_PCT = 80;

    /** Maximum time a ledger may remain open with no activity before closing.
     *
     *  If no transactions arrive and the network is quiet, the ledger is
     *  force-closed after this interval to keep the chain advancing.
     */
    std::chrono::milliseconds const ledgerIDLE_INTERVAL = std::chrono::seconds{15};

    /** Minimum time spent in the establish phase before positions are updated.
     *
     *  Ensures all peers have had at least one opportunity to participate
     *  before the engine starts adjusting its tx-set position based on
     *  peer votes.
     */
    std::chrono::milliseconds const ledgerMIN_CONSENSUS = std::chrono::milliseconds{1950};

    /** Maximum time the engine waits for laggard validators before closing.
     *
     *  Must remain comfortably below `validationFRESHNESS` (20 s) so that
     *  a validator pausing for laggards is not mistaken for an offline node
     *  by its peers. Bounds the normal consensus window from above;
     *  `ledgerABANDON_CONSENSUS` is the hard outer limit.
     */
    std::chrono::milliseconds const ledgerMAX_CONSENSUS = std::chrono::seconds{15};

    /** Minimum wait after ledger close before the next round may begin.
     *
     *  Gives all peers time to finish computing the last-closed ledger (LCL)
     *  before the engine opens the next round and starts processing proposals.
     */
    std::chrono::milliseconds const ledgerMIN_CLOSE = std::chrono::seconds{2};

    /** How often the consensus timer fires and progress is evaluated.
     *
     *  The establish phase calls `timerEntry()` on this interval. The test
     *  suite treats this value as one unit of simulated time when connecting
     *  peers at fractions of the granularity.
     */
    std::chrono::milliseconds const ledgerGRANULARITY = std::chrono::seconds{1};

    /** Multiplier used to compute the per-round abandon deadline.
     *
     *  The actual abandon timeout is `previousAgreeTime * this factor`,
     *  clamped to `[ledgerMAX_CONSENSUS, ledgerABANDON_CONSENSUS]`. The
     *  factor guards against aborting a slow-but-progressing round: a round
     *  that is merely twice as slow as normal will not be abandoned unless
     *  it also exceeds the absolute cap.
     *
     *  @see ledgerABANDON_CONSENSUS
     */
    std::size_t const ledgerABANDON_CONSENSUS_FACTOR = 10;

    /** Absolute upper bound on any consensus round's duration.
     *
     *  Does not include the time to build the LCL. After this timeout
     *  `checkConsensus()` returns `ConsensusState::Expired` regardless of
     *  agreement level. In practice the computed deadline
     *  (`previousAgreeTime * ledgerABANDON_CONSENSUS_FACTOR`) will hit this
     *  cap only for rounds many times longer than average.
     *
     *  @see ledgerABANDON_CONSENSUS_FACTOR
     */
    std::chrono::milliseconds const ledgerABANDON_CONSENSUS = std::chrono::seconds{120};

    /** Floor on the "previous round duration" used for avalanche time scaling.
     *
     *  When computing `convergePercent_` (elapsed ms / reference duration),
     *  the reference is `max(prevRoundTime_, avMIN_CONSENSUS_TIME)`. This
     *  floor ensures that every avalanche state threshold is reachable even
     *  when the prior round was unusually fast. Derived constraint: value
     *  must be at least `2 * proposeINTERVAL / (latePct - midPct)` =
     *  `2 * 0.7 s / 0.35 ≈ 4 s`.
     */
    std::chrono::milliseconds const avMIN_CONSENSUS_TIME = std::chrono::seconds{5};

    // --- Avalanche tuning ---

    /** Stages of the avalanche convergence ratchet for transaction voting.
     *
     *  States advance monotonically `Init → Mid → Late → Stuck`. Once
     *  `Stuck`, the threshold stays at 95 % until the round ends —
     *  the state never relaxes. See `avalancheCutoffs` for the time and
     *  percentage associated with each state.
     */
    enum class AvalancheState { Init, Mid, Late, Stuck };

    /** Time and percentage thresholds for one avalanche state.
     *
     *  Bundles the three facts the engine needs for each `AvalancheState`:
     *  when to enter it, what yes-vote fraction is required while in it,
     *  and which state to advance to next.
     */
    struct AvalancheCutoff
    {
        /** Percentage of the previous round's duration that must have elapsed
         *  before this state activates (0 = immediately, 200 = twice as long).
         */
        int const consensusTime;

        /** Minimum yes-vote percentage required to include a transaction while
         *  in this state.
         */
        std::size_t const consensusPct;

        /** The next `AvalancheState` to advance to once `consensusTime` and
         *  `avMIN_ROUNDS` are satisfied. `Stuck` maps to itself.
         */
        AvalancheState const next;
    };

    /** Data-driven avalanche state machine governing transaction-vote convergence.
     *
     *  Maps each `AvalancheState` to its `AvalancheCutoff`. The ratchet is
     *  asymmetric: thresholds rise over time to force convergence by attrition.
     *
     *  | State  | Time threshold | Required yes-vote | Next state |
     *  |--------|---------------|-------------------|------------|
     *  | Init   | 0 %           | 50 %              | Mid        |
     *  | Mid    | 50 %          | 65 %              | Late       |
     *  | Late   | 85 %          | 70 %              | Stuck      |
     *  | Stuck  | 200 %         | 95 %              | Stuck      |
     *
     *  Using a `std::map` rather than a `switch` keeps traversal data-driven
     *  and would support hypothetical looping state machines. All four keys
     *  are always present; `at()` calls on this map are safe.
     *
     *  @note Once in `Stuck`, the 95 % threshold makes it effectively
     *      impossible to add new transactions, forcing disputes to resolve
     *      by attrition rather than new votes.
     */
    std::map<AvalancheState, AvalancheCutoff> const avalancheCutoffs{
        {AvalancheState::Init,
         {.consensusTime = 0, .consensusPct = 50, .next = AvalancheState::Mid}},
        {AvalancheState::Mid,
         {.consensusTime = 50, .consensusPct = 65, .next = AvalancheState::Late}},
        {AvalancheState::Late,
         {.consensusTime = 85, .consensusPct = 70, .next = AvalancheState::Stuck}},
        {AvalancheState::Stuck,
         {.consensusTime = 200, .consensusPct = 95, .next = AvalancheState::Stuck}},
    };

    /** Minimum yes-vote percentage required to agree on the ledger close time.
     *
     *  Used exclusively for close-time consensus, which is a simpler majority
     *  question and does not go through the multi-round avalanche ratchet.
     *  Deliberately lower than `minCONSENSUS_PCT` (80 %) because close-time
     *  disagreement is less critical than tx-set disagreement.
     */
    std::size_t const avCT_CONSENSUS_PCT = 75;

    /** Minimum rounds a node must spend in an avalanche state before advancing.
     *
     *  Guards against premature state escalation caused by clock jitter:
     *  even when the elapsed-time percentage is high enough to warrant
     *  the next state, the node stays put until it has completed at least
     *  this many voting rounds. Also used in `haveConsensus()` to set the
     *  minimum round counter before declaring consensus.
     */
    std::size_t const avMIN_ROUNDS = 2;

    /** Rounds of unchanged peer votes before a dispute is declared stalled.
     *
     *  `DisputedTx::stalled()` returns `true` when either
     *  `peerUnchangedCounter_` or `currentVoteCounter_` reaches this value,
     *  signalling that further voting is unlikely to produce a different
     *  outcome and the engine should treat the dispute as deadlocked.
     */
    std::size_t const avSTALLED_ROUNDS = 4;
};

/** Query the avalanche state machine for the current vote threshold.
 *
 *  Returns the yes-vote percentage required right now and, if the engine
 *  should advance to the next `AvalancheState`, the new state. The caller
 *  is responsible for persisting the state transition:
 *  @code
 *  auto const [pct, newState] = getNeededWeight(p, state_, pct_, rounds_, p.avMIN_ROUNDS);
 *  if (newState)
 *      state_ = *newState;
 *  @endcode
 *
 *  The `minimumRounds` guard prevents premature escalation on clock jitter:
 *  a state transition only occurs when both the time percentage *and* the
 *  round count conditions are satisfied.
 *
 *  Called per-transaction by `DisputedTx::updateVote()` and once per tick
 *  for close-time consensus in `Consensus::updateOurPositions()`. The
 *  close-time path always passes `currentRounds = 0` and `minimumRounds = 0`
 *  because close-time convergence does not track discrete voting rounds.
 *
 *  @param p             The consensus parameter bundle.
 *  @param currentState  The avalanche state the caller is currently in.
 *  @param percentTime   Elapsed time expressed as a percentage of the previous
 *      round's duration (i.e. `convergePercent_`).
 *  @param currentRounds Number of voting rounds spent in `currentState`.
 *  @param minimumRounds Minimum rounds required before a state transition is
 *      allowed. Pass `p.avMIN_ROUNDS` for transaction voting; pass `0` for
 *      close-time consensus.
 *  @return A pair of: (1) the `consensusPct` in effect for the current tick,
 *      and (2) an optional next `AvalancheState` — `nullopt` when no
 *      transition occurs, otherwise the state the caller should advance to.
 *  @note `at()` calls on `avalancheCutoffs` are safe because the map is
 *      constructed with all four valid keys.
 */
inline std::pair<std::size_t, std::optional<ConsensusParms::AvalancheState>>
getNeededWeight(
    ConsensusParms const& p,
    ConsensusParms::AvalancheState currentState,
    int percentTime,
    std::size_t currentRounds,
    std::size_t minimumRounds)
{
    auto const& currentCutoff = p.avalancheCutoffs.at(currentState);
    if (currentCutoff.next != currentState && currentRounds >= minimumRounds)
    {
        auto const& nextCutoff = p.avalancheCutoffs.at(currentCutoff.next);
        XRPL_ASSERT(
            nextCutoff.consensusTime >= currentCutoff.consensusTime,
            "xrpl::getNeededWeight : next state valid");
        if (percentTime >= nextCutoff.consensusTime)
        {
            return {nextCutoff.consensusPct, currentCutoff.next};
        }
    }
    return {currentCutoff.consensusPct, {}};
}

}  // namespace xrpl
