#pragma once

#include <xrpld/consensus/ConsensusParms.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_writer.h>

#include <boost/container/flat_map.hpp>

#include <utility>

namespace xrpl {

/** A transaction discovered to be in dispute during consensus.

    During consensus, a @ref DisputedTx is created when a transaction
    is discovered to be disputed. The object persists only as long as
    the dispute.

    Undisputed transactions have no corresponding @ref DisputedTx object.

    Refer to @ref Consensus for details on the template type requirements.

    @tparam Tx The type for a transaction
    @tparam NodeId The type for a node identifier
*/

template <class Tx, class NodeId>
class DisputedTx
{
    using TxID_t = typename Tx::ID;
    using Map_t = boost::container::flat_map<NodeId, bool>;

public:
    /** Constructor

        @param tx The transaction under dispute
        @param ourVote Our vote on whether tx should be included
        @param numPeers Anticipated number of peer votes
        @param j Journal for debugging
    */
    DisputedTx(Tx tx, bool ourVote, std::size_t numPeers, beast::Journal j)
        : ourVote_(ourVote), tx_(std::move(tx)), j_(j)
    {
        votes_.reserve(numPeers);
    }

    //! The unique id/hash of the disputed transaction.
    [[nodiscard]] TxID_t const&
    id() const
    {
        return tx_.id();
    }

    //! Our vote on whether the transaction should be included.
    [[nodiscard]] bool
    getOurVote() const
    {
        return ourVote_;
    }

    //! Are we and our peers "stalled" where we probably won't change
    //! our vote?
    [[nodiscard]] bool
    stalled(
        ConsensusParms const& p,
        bool proposing,
        int peersUnchanged,
        beast::Journal j,
        std::unique_ptr<std::stringstream> const& clog) const
    {
        // at() can throw, but the map is built by hand to ensure all valid
        // values are available.
        auto const& currentCutoff = p.avalancheCutoffs.at(avalancheState_);
        auto const& nextCutoff = p.avalancheCutoffs.at(currentCutoff.next);

        // We're have not reached the final avalanche state, or been there long
        // enough, so there's room for change. Check the times in case the state
        // machine is altered to allow states to loop.
        if (nextCutoff.consensusTime > currentCutoff.consensusTime ||
            avalancheCounter_ < p.avMinRounds)
            return false;

        // We've haven't had this vote for minimum rounds yet. Things could
        // change.
        if (proposing && currentVoteCounter_ < p.avMinRounds)
            return false;

        // If we or any peers have changed a vote in several rounds, then
        // things could still change. But if _either_ has not changed in that
        // long, we're unlikely to change our vote any time soon. (This prevents
        // a malicious peer from flip-flopping a vote to prevent consensus.)
        if (peersUnchanged < p.avStalledRounds &&
            (proposing && currentVoteCounter_ < p.avStalledRounds))
            return false;

        // Does this transaction have more than 80% agreement

        // Compute the percentage of nodes voting 'yes' (possibly including us)
        int const support = (yays_ + (proposing && ourVote_ ? 1 : 0)) * 100;
        int const total = nays_ + yays_ + (proposing ? 1 : 0);
        if (total == 0)
        {
            // There are no votes, so we know nothing
            return false;
        }
        int const weight = support / total;
        // Returns true if the tx has more than minCONSENSUS_PCT (80) percent
        // agreement. Either voting for _or_ voting against the tx.
        bool const stalled = weight > p.minConsensusPct || weight < (100 - p.minConsensusPct);

        if (stalled)
        {
            // stalling is an error condition for even a single
            // transaction.
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

    //! The disputed transaction.
    [[nodiscard]] Tx const&
    tx() const
    {
        return tx_;
    }

    //! Change our vote
    void
    setOurVote(bool o)
    {
        ourVote_ = o;
    }

    /** Change a peer's vote

        @param peer Identifier of peer.
        @param votesYes Whether peer votes to include the disputed transaction.

        @return bool Whether the peer changed its vote. (A new vote counts as a
       change.)
    */
    [[nodiscard]] bool
    setVote(NodeId const& peer, bool votesYes);

    /** Remove a peer's vote

        @param peer Identifier of peer.
    */
    void
    unVote(NodeId const& peer);

    /** Update our vote given progression of consensus.

        Updates our vote on this disputed transaction based on our peers' votes
        and how far along consensus has proceeded.

        @param percentTime Percentage progress through consensus, e.g. 50%
               through or 90%.
        @param proposing Whether we are proposing to our peers in this round.
        @param p Consensus parameters controlling thresholds for voting
        @return Whether our vote changed
    */
    bool
    updateVote(int percentTime, bool proposing, ConsensusParms const& p);

    //! JSON representation of dispute, used for debugging
    [[nodiscard]] json::Value
    getJson() const;

private:
    int yays_{0};   //< Number of yes votes
    int nays_{0};   //< Number of no votes
    bool ourVote_;  //< Our vote (true is yes)
    Tx tx_;         //< Transaction under dispute
    Map_t votes_;   //< Map from NodeID to vote
    //! The number of rounds we've gone without changing our vote
    std::size_t currentVoteCounter_ = 0;
    //! Which minimum acceptance percentage phase we are currently in
    ConsensusParms::AvalancheState avalancheState_ = ConsensusParms::AvalancheState::Init;
    //! How long we have been in the current acceptance phase
    std::size_t avalancheCounter_ = 0;
    beast::Journal const j_;
};

// Track a peer's yes/no vote on a particular disputed tx_
template <class Tx, class NodeId>
bool
DisputedTx<Tx, NodeId>::setVote(NodeId const& peer, bool votesYes)
{
    auto const [it, inserted] = votes_.insert(std::make_pair(peer, votesYes));

    // new vote
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
    // changes vote to yes
    if (votesYes && !it->second)
    {
        JLOG(j_.debug()) << "Peer " << peer << " now votes YES on " << tx_.id();
        --nays_;
        ++yays_;
        it->second = true;
        return true;
    }
    // changes vote to no
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

// Remove a peer's vote on this disputed transaction
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

    // When proposing, to prevent avalanche stalls, we increase the needed
    // weight slightly over time. We also need to ensure that the consensus has
    // made a minimum number of attempts at each "state" before moving
    // to the next.
    // Proposing or not, we need to keep track of which state we've reached so
    // we can determine if the vote has stalled.
    auto const [requiredPct, newState] =
        getNeededWeight(p, avalancheState_, percentTime, ++avalancheCounter_, p.avMinRounds);
    if (newState)
    {
        avalancheState_ = *newState;
        avalancheCounter_ = 0;
    }

    if (proposing)  // give ourselves full weight
    {
        // This is basically the percentage of nodes voting 'yes' (including us)
        weight = ((yays_ * 100) + (ourVote_ ? 100 : 0)) / (nays_ + yays_ + 1);

        newPosition = weight > requiredPct;
    }
    else
    {
        // don't let us outweigh a proposing node, just recognize consensus
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
