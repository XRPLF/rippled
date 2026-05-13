#pragma once

#include <xrpld/consensus/ConsensusProposal.h>
#include <xrpld/consensus/DisputedTx.h>

#include <xrpl/basics/chrono.h>

#include <chrono>
#include <map>

namespace xrpl {

/** Represents how a node currently participates in consensus.
 *
 *  A node enters each round in one of two initial states — `Proposing` or
 *  `Observing` — and may transition to `WrongLedger` if it detects it is
 *  building on the wrong prior ledger.  Successfully switching to the correct
 *  ledger mid-round lands the node in `SwitchedLedger`.
 *
 *  @code
 *    proposing               observing
 *       \                       /
 *        \---> wrongLedger <---/
 *                   ^
 *                   |
 *                   v
 *              switchedLedger
 *  @endcode
 *
 *  `SwitchedLedger` is kept distinct from `Observing` because close-time
 *  calculations in `Consensus.h` guard against `WrongLedger` mode when
 *  deciding whether the previous ledger's close time is authoritative; the
 *  mode label is preserved throughout the round so diagnostics and close-time
 *  logic can account for mid-round recovery.  `MonitoredMode` inside
 *  `Consensus.h` wraps this enum so every transition calls
 *  `Adaptor::onModeChange`, making silent changes structurally impossible.
 *
 *  @see ConsensusPhase
 */
enum class ConsensusMode {
    //! Actively broadcasting our position to the network each round.
    Proposing,
    //! Listening to peer proposals without broadcasting our own position.
    Observing,
    //! Detected we are on the wrong prior ledger; attempting to acquire the
    //! correct one.  Most voting logic treats this like `Observing`, but
    //! close-time code explicitly excludes it from "correct" close-time
    //! calculations.
    WrongLedger,
    //! Successfully switched to the correct ledger mid-round.  Behaves like
    //! `Observing` in voting logic but preserves the history that a ledger
    //! switch occurred, which diagnostics and close-time code rely on.
    SwitchedLedger
};

/** Return a human-readable name for a `ConsensusMode` value.
 *
 *  Intended for logging and JSON diagnostics.  Returns `"unknown"` for any
 *  out-of-range value.
 *
 *  @param m The consensus mode to stringify.
 *  @return A lowercase string label matching the enumerator name.
 */
inline std::string
to_string(ConsensusMode m)
{
    switch (m)
    {
        case ConsensusMode::Proposing:
            return "proposing";
        case ConsensusMode::Observing:
            return "observing";
        case ConsensusMode::WrongLedger:
            return "wrongLedger";
        case ConsensusMode::SwitchedLedger:
            return "switchedLedger";
        default:
            return "unknown";
    }
}

/** Coarse phase of a single ledger consensus round.
 *
 *  Governs the high-level state machine step the node is currently in.
 *  Most `Consensus` entry points (`timerEntry`, `gotTxSet`, `peerProposal`)
 *  short-circuit immediately when the phase is `Accepted`.
 *
 *  @code
 *        "close"             "accept"
 *   open -------> establish ---------> accepted
 *     ^               |                    |
 *     |---------------|                    |
 *     ^                    "startRound"    |
 *     |------------------------------------|
 *  @endcode
 *
 *  The unusual backwards arc — `establish` → `open` — occurs inside
 *  `Consensus::handleWrongLedger` when a ledger switch is detected
 *  mid-round.  The engine re-enters `open` on the correct ledger without
 *  tearing down surrounding state.
 *
 *  @see ConsensusMode
 */
enum class ConsensusPhase {
    //! Accumulating transactions; no position declared yet.  The node waits
    //! here until `shouldCloseLedger` triggers the `close` transition.
    Open,

    //! Exchanging and converging on proposals with peers via avalanche voting.
    //! Ends when `checkConsensus` returns a non-`No` outcome.
    Establish,

    //! Round concluded; ledger committed.  The node stays here until the
    //! application calls `startRound` to kick off the next cycle.  No
    //! consensus state changes occur in this phase.
    Accepted,
};

/** Return a human-readable name for a `ConsensusPhase` value.
 *
 *  Intended for logging and JSON diagnostics.  Returns `"unknown"` for any
 *  out-of-range value.
 *
 *  @param p The consensus phase to stringify.
 *  @return A lowercase string label matching the enumerator name.
 */
inline std::string
to_string(ConsensusPhase p)
{
    switch (p)
    {
        case ConsensusPhase::Open:
            return "open";
        case ConsensusPhase::Establish:
            return "establish";
        case ConsensusPhase::Accepted:
            return "accepted";
        default:
            return "unknown";
    }
}

/** Elapsed-time stopwatch with dual real-clock and simulation tick modes.
 *
 *  Provides a single `read()` value — milliseconds since the last `reset()` —
 *  via two distinct `tick()` overloads:
 *
 *  - **Wall-clock tick**: computes `duration_cast<milliseconds>(tp - start_)`,
 *    giving real elapsed time from a `steady_clock::time_point`.
 *  - **Fixed-increment tick**: accumulates a caller-supplied `milliseconds`
 *    delta, enabling deterministic simulation in unit tests without touching
 *    the system clock.
 *
 *  Both paths update the same `dur_` field, so `read()` is always valid
 *  regardless of which variant was used.  This is the timing substrate for
 *  `ConsensusResult::roundTime`, which `Consensus.h` uses to record how long
 *  the `Establish` phase lasted and feed into `prevRoundTime_` heuristics for
 *  the next round's timeout calculations.
 *
 *  @note The wall-clock overload measures elapsed time from `start_`; it does
 *      not accumulate across calls.  The fixed-increment overload does
 *      accumulate.  Do not mix both overloads on the same timer instance.
 */
class ConsensusTimer
{
    using time_point = std::chrono::steady_clock::time_point;
    time_point start_;
    std::chrono::milliseconds dur_{};

public:
    /** Return elapsed time since the last `reset()`.
     *
     *  @return Milliseconds elapsed, as recorded by the most recent `tick()`.
     */
    [[nodiscard]] std::chrono::milliseconds
    read() const
    {
        return dur_;
    }

    /** Advance the timer by a fixed increment (simulation mode).
     *
     *  Adds @p fixed to the accumulated duration.  Use this overload in
     *  deterministic unit tests to drive the timer without a real clock.
     *
     *  @param fixed Duration to add to the current elapsed time.
     */
    void
    tick(std::chrono::milliseconds fixed)
    {
        dur_ += fixed;
    }

    /** Reset the timer origin to @p tp and clear the elapsed duration.
     *
     *  Must be called before the first wall-clock `tick(time_point)` to
     *  establish the reference point.  `Consensus::closeLedger` calls this
     *  when transitioning to the `Establish` phase.
     *
     *  @param tp The new start time for elapsed-time measurement.
     */
    void
    reset(time_point tp)
    {
        start_ = tp;
        dur_ = std::chrono::milliseconds{0};
    }

    /** Update elapsed time from a wall-clock sample (production mode).
     *
     *  Sets `dur_` to `duration_cast<milliseconds>(tp - start_)`.  The
     *  measurement is absolute from the last `reset()`, not incremental —
     *  calling this multiple times with the same @p tp is idempotent.
     *
     *  @param tp Current `steady_clock` time point.
     */
    void
    tick(time_point tp)
    {
        using namespace std::chrono;
        dur_ = duration_cast<milliseconds>(tp - start_);
    }
};

/** Histogram of peer-reported close times for clock-drift analysis.
 *
 *  Each peer's initial consensus proposal (`seqJoin`) includes that peer's
 *  estimate of when the ledger closed.  This struct collects those estimates
 *  so the engine can find the most-agreed-upon close-time bucket during
 *  close-time consensus resolution.
 *
 *  `peers` is a `std::map` rather than an unordered container so that
 *  iteration is in ascending time order, giving deterministic traversal
 *  and reproducible bucket selection across nodes with different hash seeds.
 *  `rawCloseTimes_` in `Consensus.h` is the live instance populated during
 *  the `Establish` phase and passed to `Adaptor::onAccept` at round end.
 */
struct ConsensusCloseTimes
{
    explicit ConsensusCloseTimes() = default;

    //! Histogram of peer close-time estimates (time → vote count); ordered
    //! ascending for deterministic traversal during close-time resolution.
    std::map<NetClock::time_point, int> peers;

    //! This node's own close-time estimate.
    NetClock::time_point self;
};

/** Outcome classification returned by `checkConsensus`.
 *
 *  Every call to `checkConsensus` resolves to exactly one of these values,
 *  in priority order: `No` first, then `Expired`, then `MovedOn`, then `Yes`.
 *  Both `MovedOn` and `Expired` cause `Consensus` to transition to the
 *  `Accepted` phase, but they are logged and recorded differently in
 *  `ConsensusResult::state` for diagnostics.
 */
enum class ConsensusState {
    //! Insufficient time has elapsed or agreement is below threshold; the
    //! establish phase continues.
    No,
    //! Enough of the network already validated a later ledger without us.
    //! The local node accepts whatever the network decided.  This is normal
    //! network dynamics — a slow node catching up.
    MovedOn,
    //! Round time exceeded the abandon threshold (`prevAgreeTime *
    //! ledgerABANDON_CONSENSUS_FACTOR`, clamped to
    //! `[ledgerMAX_CONSENSUS, ledgerABANDON_CONSENSUS]`).  Forces acceptance
    //! to prevent indefinite stalls; rare and diagnostic-worthy.
    Expired,
    //! Local node and the network agree on the transaction set (normally
    //! ≥80% of proposers, counting self when proposing).  Also triggered
    //! immediately when all disputes have a clear supermajority (`stalled`
    //! path in `checkConsensus`).
    Yes
};

/** Aggregated outcome of a single consensus round.
 *
 *  Instantiated once per round inside `Consensus::closeLedger` and stored
 *  as `Consensus::result_` (`std::optional<Result>`).  Bundles the agreed
 *  transaction set, the node's signed position, live dispute state, and
 *  round-timing data needed to hand the round off to `Adaptor::onAccept`.
 *
 *  @invariant `txns.id() == position.position()` — the node's declared
 *      position is always a commitment to a specific transaction set.
 *      Enforced by assertion in the constructor.
 *
 *  @tparam Traits Traits class supplying `Ledger_t`, `TxSet_t`, and
 *      `NodeID_t`; the same traits type used by the enclosing `Consensus`
 *      template.
 */
template <class Traits>
struct ConsensusResult
{
    using Ledger_t = typename Traits::Ledger_t;
    using TxSet_t = typename Traits::TxSet_t;
    using NodeID_t = typename Traits::NodeID_t;

    using Tx_t = typename TxSet_t::Tx;
    using Proposal_t = ConsensusProposal<NodeID_t, typename Ledger_t::ID, typename TxSet_t::ID>;
    using Dispute_t = DisputedTx<Tx_t, NodeID_t>;

    /** Construct a round result from a transaction set and matching proposal.
     *
     *  @param s The transaction set produced by `Adaptor::onClose`; ownership
     *      is transferred.
     *  @param p This node's initial consensus proposal; ownership is
     *      transferred.
     *  @pre `s.id() == p.position()` — asserted internally.  Violating this
     *      precondition terminates the process in debug builds.
     */
    ConsensusResult(TxSet_t&& s, Proposal_t&& p) : txns{std::move(s)}, position{std::move(p)}
    {
        XRPL_ASSERT(txns.id() == position.position(), "xrpl::ConsensusResult : valid inputs");
    }

    //! The transaction set that consensus agrees should go into the ledger.
    TxSet_t txns;

    //! This node's signed proposal committing to `txns.id()` and a close time.
    Proposal_t position;

    //! Live avalanche-voting state for each transaction where peers disagree
    //! with our current position.  Only genuinely-differing transactions are
    //! tracked here — transactions already agreed upon are absent.
    hash_map<typename Tx_t::ID, Dispute_t> disputes;

    //! Work-avoidance cache of transaction-set IDs already compared against
    //! `txns`.  Checked before `createDisputes` to prevent O(n²) recomputation
    //! when multiple peers share the same transaction-set ID.
    hash_set<typename TxSet_t::ID> compares;

    //! Elapsed time in the `Establish` phase.  Reset when `closeLedger`
    //! transitions to `Establish`; read at round end to update `prevRoundTime_`
    //! for the next round's timeout heuristics.
    ConsensusTimer roundTime;

    //! Outcome of `checkConsensus` for this round.  Starts as `No`; set to
    //! `Yes`, `MovedOn`, or `Expired` when the `Accepted` phase is entered.
    ConsensusState state = ConsensusState::No;

    //! Snapshot of the number of peers that proposed during this round.
    //! Passed to `Adaptor::onAccept` and used to seed `prevProposers_` for the
    //! next round's consensus-threshold calculations.
    std::size_t proposers = 0;
};
}  // namespace xrpl
