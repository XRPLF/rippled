/** @file
 *  Per-transaction dispute tracking for the XRPL consensus protocol.
 *
 *  A `DisputedTx` is created for each transaction that appears in one
 *  validator's proposed set but not another's. It records every peer's
 *  yes/no vote and drives the local node's vote toward convergence via the
 *  avalanche-style state machine defined in `ConsensusParms`.
 */

#pragma once

#include <xrpld/consensus/ConsensusParms.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_writer.h>

#include <boost/container/flat_map.hpp>

#include <utility>

namespace xrpl {

/** Tracks peer votes and drives local vote convergence for a single disputed
 *  transaction during the establish phase of XRPL consensus.
 *
 *  A `DisputedTx` is instantiated by `Consensus::createDisputes()` for every
 *  transaction that is present in one observed position but absent from
 *  another. It survives for exactly the duration of the establish phase and
 *  is destroyed when consensus is reached or the round is abandoned.
 *  Transactions that are unanimously accepted or rejected have no
 *  corresponding `DisputedTx` object.
 *
 *  Peer votes are stored in a `boost::container::flat_map` (contiguous
 *  memory, cache-friendly for the typical small-to-medium validator set).
 *  Separate `yays_` and `nays_` counters allow O(1) percentage computation
 *  in `updateVote()` without scanning the map on every tick.
 *
 *  @tparam Tx The transaction type; must expose a nested `ID` typedef and
 *      an `id()` accessor.
 *  @tparam NodeId The node-identifier type used as the map key.
 *  @see Consensus, ConsensusParms, getNeededWeight
 */

template <class Tx, class NodeId>
class DisputedTx
{
    using TxID_t = typename Tx::ID;
    using Map_t = boost::container::flat_map<NodeId, bool>;

public:
    /** Construct a dispute record for a single transaction.
     *
     *  The internal vote map is pre-reserved to `numPeers` entries to avoid
     *  rehashing during the burst of `setVote()` calls that immediately follows
     *  dispute creation.
     *
     *  @param tx The transaction under dispute.
     *  @param ourVote `true` if the local node currently includes this
     *      transaction in its proposed set.
     *  @param numPeers Number of currently-connected validators; used to
     *      size the vote map capacity up-front.
     *  @param j Journal for debug and informational logging.
     */
    DisputedTx(Tx tx, bool ourVote, std::size_t numPeers, beast::Journal j)
        : ourVote_(ourVote), tx_(std::move(tx)), j_(j)
    {
        votes_.reserve(numPeers);
    }

    /** The unique identifier (hash) of the disputed transaction. */
    [[nodiscard]] TxID_t const&
    id() const
    {
        return tx_.id();
    }

    /** Whether the local node currently votes to include this transaction. */
    [[nodiscard]] bool
    getOurVote() const
    {
        return ourVote_;
    }

    /** Determine whether this dispute has reached a stable, irresolvable state.
     *
     *  Called by `Consensus::checkConsensus()` after close-time consensus is
     *  established but disputed transactions remain. A stall indicates that the
     *  network is overwhelmingly aligned (≥ `minCONSENSUS_PCT`, i.e. 80%) yet
     *  further avalanche rounds are unlikely to change the outcome — either
     *  because the avalanche state machine has reached the terminal `Stuck`
     *  loop, or because neither peers nor the local vote have moved in
     *  `avSTALLED_ROUNDS` consecutive rounds.
     *
     *  All four conditions must hold simultaneously:
     *  1. The avalanche state machine is in a terminal loop
     *     (`nextCutoff.consensusTime <= currentCutoff.consensusTime`).
     *  2. The current state has been active for at least `avMIN_ROUNDS`.
     *  3. `peersUnchanged >= avSTALLED_ROUNDS` **or** (when proposing)
     *     `currentVoteCounter_ >= avSTALLED_ROUNDS`. The *or* rather than
     *     *and* prevents a malicious peer from resetting the peer counter via
     *     flip-flopping while the local vote remains stable.
     *  4. The yes-vote share exceeds `minCONSENSUS_PCT` in either direction.
     *
     *  A stall is logged at `error` level because stalling on even one
     *  transaction is an abnormal event warranting investigation.
     *
     *  @param p Consensus parameters (thresholds and round counts).
     *  @param proposing Whether the local node is actively proposing this round.
     *  @param peersUnchanged Number of consecutive rounds in which no peer
     *      changed its vote on any disputed transaction (maintained by the
     *      caller via `peerUnchangedCounter_`).
     *  @param j Journal for error-level stall logging.
     *  @param clog Optional diagnostic log stream; receives the stall message
     *      when non-null.
     *  @return `true` if the dispute is stalled and consensus should proceed
     *      without waiting for further vote changes.
     *  @note `at()` calls on `avalancheCutoffs` are safe because the map is
     *      constructed with all four valid `AvalancheState` keys.
     */
    [[nodiscard]] bool
    stalled(
        ConsensusParms const& p,
        bool proposing,
        int peersUnchanged,
        beast::Journal j,
        std::unique_ptr<std::stringstream> const& clog) const
    {
        auto const& currentCutoff = p.avalancheCutoffs.at(avalancheState_);
        auto const& nextCutoff = p.avalancheCutoffs.at(currentCutoff.next);

        // Not yet in the terminal stuck loop, or haven't dwelled long enough.
        // Check the time comparison in case the state machine is ever extended
        // to allow non-terminal loops.
        if (nextCutoff.consensusTime > currentCutoff.consensusTime ||
            avalancheCounter_ < p.avMIN_ROUNDS)
            return false;

        if (proposing && currentVoteCounter_ < p.avMIN_ROUNDS)
            return false;

        // Use OR: a peer that flip-flops cannot reset both counters. As long
        // as our own vote is stable, the stall guard holds.
        if (peersUnchanged < p.avSTALLED_ROUNDS &&
            (proposing && currentVoteCounter_ < p.avSTALLED_ROUNDS))
            return false;

        int const support = (yays_ + (proposing && ourVote_ ? 1 : 0)) * 100;
        int const total = nays_ + yays_ + (proposing ? 1 : 0);
        if (total == 0)
        {
            return false;
        }
        int const weight = support / total;
        bool const stalled = weight > p.minCONSENSUS_PCT || weight < (100 - p.minCONSENSUS_PCT);

        if (stalled)
        {
            // Stalling on even a single transaction is an error condition.
            std::stringstream s;
            s << "Transaction " << id() << " is stalled. We have been voting "
              << (getOurVote() ? "YES" : "NO") << " for " << currentVoteCounter_
              << " rounds. Peers have not changed their votes in " << peersUnchanged
              << " rounds. The transaction has " << weight << "% support. ";
            JLOG(j_.error()) << s.str();
            CLOG(clog) << s.str();
        }

        return stalled;
    }

    /** The full disputed transaction object. */
    [[nodiscard]] Tx const&
    tx() const
    {
        return tx_;
    }

    /** Override the local node's vote directly.
     *
     *  @param o `true` to vote yes (include the transaction).
     *  @note Prefer `updateVote()` for normal avalanche-driven vote changes;
     *      use this only when the engine needs to force a position (e.g.
     *      during wrong-ledger recovery).
     */
    void
    setOurVote(bool o)
    {
        ourVote_ = o;
    }

    /** Record or update a peer's vote on this transaction.
     *
     *  Maintains the `yays_` and `nays_` counters incrementally so
     *  `updateVote()` can compute percentages in O(1) without scanning the
     *  full vote map. A brand-new vote counts as a change.
     *
     *  @param peer Identifier of the voting peer.
     *  @param votesYes `true` if the peer votes to include the transaction.
     *  @return `true` if the peer's vote changed (including a first-time
     *      vote); `false` if the peer already held this position.
     */
    [[nodiscard]] bool
    setVote(NodeId const& peer, bool votesYes);

    /** Remove a peer's vote, adjusting the `yays_`/`nays_` counters.
     *
     *  Called when a peer disconnects, bows out of consensus, or its position
     *  is superseded by a new proposal. Has no effect if the peer has not
     *  previously voted.
     *
     *  @param peer Identifier of the peer whose vote is to be removed.
     */
    void
    unVote(NodeId const& peer);

    /** Update the local node's vote using the avalanche state machine.
     *
     *  Called once per consensus tick during the establish phase. Advances the
     *  avalanche state when the time threshold and minimum-rounds dwell are
     *  both met, then recomputes the local position.
     *
     *  Behaviour differs based on whether the local node is proposing:
     *  - **Proposing**: counts its own vote alongside peers:
     *    `weight = (yays_*100 + (ourVote_?100:0)) / (nays_+yays_+1)`;
     *    flips when `weight > requiredPct` (escalating avalanche threshold).
     *  - **Not proposing** (observer): simplifies to `newPosition = yays_ > nays_`
     *    with `weight = -1`. The observer never distorts the weighted vote that
     *    proposing nodes rely on.
     *
     *  `currentVoteCounter_` is incremented on every tick without a flip and
     *  reset to zero on any flip; this streak feeds both `stalled()` and the
     *  informational log output.
     *
     *  @param percentTime Elapsed consensus time as a percentage of the prior
     *      round's duration; drives avalanche state transitions.
     *  @param proposing Whether the local node is actively proposing this round.
     *  @param p Consensus parameters (avalanche cutoffs, round thresholds).
     *  @return `true` if the local vote flipped; `false` if the position is
     *      unchanged.
     *  @note Short-circuits without avalanche work if `ourVote_` is already
     *      consistent with unanimous peer agreement (all yes or all no).
     */
    bool
    updateVote(int percentTime, bool proposing, ConsensusParms const& p);

    /** Serialise the dispute state to JSON for diagnostics.
     *
     *  Includes `yays`, `nays`, `our_vote`, and a per-peer vote map.
     *
     *  @return A JSON object suitable for logging or RPC debug output.
     */
    [[nodiscard]] json::Value
    getJson() const;

private:
    int yays_{0};   ///< Peers currently voting yes; updated by setVote/unVote.
    int nays_{0};   ///< Peers currently voting no; updated by setVote/unVote.
    bool ourVote_;  ///< Local node's current position (true = include tx).
    Tx tx_;         ///< The transaction under dispute.
    Map_t votes_;   ///< Per-peer votes; flat_map for cache locality.
    /** Consecutive rounds without a local vote flip; reset to 0 on any flip.
     *  Feeds the stall guard in `stalled()` and info-level log output. */
    std::size_t currentVoteCounter_ = 0;
    /** Current avalanche phase; advances as `percentTime` crosses thresholds. */
    ConsensusParms::AvalancheState avalancheState_ = ConsensusParms::AvalancheState::Init;
    /** Rounds spent in `avalancheState_`; reset to 0 on each state transition. */
    std::size_t avalancheCounter_ = 0;
    beast::Journal const j_;
};

template <class Tx, class NodeId>
bool
DisputedTx<Tx, NodeId>::setVote(NodeId const& peer, bool votesYes)
{
    auto const [it, inserted] = votes_.insert(std::make_pair(peer, votesYes));

    if (inserted)
    {
        if (votesYes)
        {
            JLOG(j_.debug()) << "Peer " << peer << " votes YES on " << tx_.id();
            ++yays_;
        }
        else
        {
            JLOG(j_.debug()) << "Peer " << peer << " votes NO on " << tx_.id();
            ++nays_;
        }
        return true;
    }
    if (votesYes && !it->second)
    {
        JLOG(j_.debug()) << "Peer " << peer << " now votes YES on " << tx_.id();
        --nays_;
        ++yays_;
        it->second = true;
        return true;
    }
    if (!votesYes && it->second)
    {
        JLOG(j_.debug()) << "Peer " << peer << " now votes NO on " << tx_.id();
        ++nays_;
        --yays_;
        it->second = false;
        return true;
    }
    return false;
}

template <class Tx, class NodeId>
void
DisputedTx<Tx, NodeId>::unVote(NodeId const& peer)
{
    auto it = votes_.find(peer);

    if (it != votes_.end())
    {
        if (it->second)
        {
            --yays_;
        }
        else
        {
            --nays_;
        }

        votes_.erase(it);
    }
}

template <class Tx, class NodeId>
bool
DisputedTx<Tx, NodeId>::updateVote(int percentTime, bool proposing, ConsensusParms const& p)
{
    if (ourVote_ && (nays_ == 0))
        return false;

    if (!ourVote_ && (yays_ == 0))
        return false;

    bool newPosition = false;
    int weight = 0;

    auto const [requiredPct, newState] =
        getNeededWeight(p, avalancheState_, percentTime, ++avalancheCounter_, p.avMIN_ROUNDS);
    if (newState)
    {
        avalancheState_ = *newState;
        avalancheCounter_ = 0;
    }

    if (proposing)
    {
        weight = (yays_ * 100 + (ourVote_ ? 100 : 0)) / (nays_ + yays_ + 1);
        newPosition = weight > requiredPct;
    }
    else
    {
        // Observer: follow majority without distorting the weighted vote
        // that proposing nodes use for threshold calculations.
        weight = -1;
        newPosition = yays_ > nays_;
    }

    if (newPosition == ourVote_)
    {
        ++currentVoteCounter_;
        JLOG(j_.info()) << "No change (" << (ourVote_ ? "YES" : "NO") << ") on " << tx_.id()
                        << " : weight " << weight << ", percent " << percentTime
                        << ", round(s) with this vote: " << currentVoteCounter_;
        JLOG(j_.debug()) << json::Compact{getJson()};
        return false;
    }

    currentVoteCounter_ = 0;
    ourVote_ = newPosition;
    JLOG(j_.debug()) << "We now vote " << (ourVote_ ? "YES" : "NO") << " on " << tx_.id();
    JLOG(j_.debug()) << json::Compact{getJson()};
    return true;
}

template <class Tx, class NodeId>
json::Value
DisputedTx<Tx, NodeId>::getJson() const
{
    using std::to_string;

    json::Value ret(json::ValueType::Object);

    ret["yays"] = yays_;
    ret["nays"] = nays_;
    ret["our_vote"] = ourVote_;

    if (!votes_.empty())
    {
        json::Value votes(json::ValueType::Object);
        for (auto const& [nodeId, vote] : votes_)
            votes[to_string(nodeId)] = vote;
        ret["votes"] = std::move(votes);
    }

    return ret;
}

}  // namespace xrpl
