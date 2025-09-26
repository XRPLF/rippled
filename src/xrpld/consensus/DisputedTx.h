//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef XRPL_APP_CONSENSUS_IMPL_DISPUTEDTX_H_INCLUDED
#define XRPL_APP_CONSENSUS_IMPL_DISPUTEDTX_H_INCLUDED

#include <xrpld/consensus/ConsensusParms.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_writer.h>

#include <boost/container/flat_map.hpp>

namespace ripple {

/** A transaction discovered to be in dispute during consensus.

    During consensus, a @ref DisputedTx is created when a transaction
    is discovered to be disputed. The object persists only as long as
    the dispute.

    Undisputed transactions have no corresponding @ref DisputedTx object.

    Refer to @ref Consensus for details on the template type requirements.

    @tparam Tx_t The type for a transaction
    @tparam NodeID_t The type for a node identifier
*/

template <class Tx_t, class NodeID_t>
class DisputedTx
{
    using TxID_t = typename Tx_t::ID;
    using Map_t = boost::container::flat_map<NodeID_t, bool>;

public:
    /** Constructor

        @param tx The transaction under dispute
        @param ourVote Our vote on whether tx should be included
        @param numPeers Anticipated number of peer votes
        @param j Journal for debugging
    */
    DisputedTx(
        Tx_t const& tx,
        bool ourVote,
        std::size_t numPeers,
        beast::Journal j)
        : yays_(0), nays_(0), ourVote_(ourVote), tx_(tx), j_(j)
    {
        votes_.reserve(numPeers);
    }

    //! The unique id/hash of the disputed transaction.
    TxID_t const&
    ID() const
    {
        return tx_.id();
    }

    //! Our vote on whether the transaction should be included.
    bool
    getOurVote() const
    {
        return ourVote_;
    }

    //! Are we and our peers "stalled" where we probably won't change
    //! our vote?
    bool
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
            avalancheCounter_ < p.avMIN_ROUNDS)
            return false;

        // We've haven't had this vote for minimum rounds yet. Things could
        // change.
        if (proposing && currentVoteCounter_ < p.avMIN_ROUNDS)
            return false;

        // If we or any peers have changed a vote in several rounds, then
        // things could still change. But if _either_ has not changed in that
        // long, we're unlikely to change our vote any time soon. (This prevents
        // a malicious peer from flip-flopping a vote to prevent consensus.)
        if (peersUnchanged < p.avSTALLED_ROUNDS &&
            (proposing && currentVoteCounter_ < p.avSTALLED_ROUNDS))
            return false;

        // Does this transaction have more than 80% agreement

        // Compute the percentage of nodes voting 'yes' (possibly including us)
        int const support = (yays_ + (proposing && ourVote_ ? 1 : 0)) * 100;
        int total = nays_ + yays_ + (proposing ? 1 : 0);
        if (!total)
            // There are no votes, so we know nothing
            return false;
        int const weight = support / total;
        // Returns true if the tx has more than minCONSENSUS_PCT (80) percent
        // agreement. Either voting for _or_ voting against the tx.
        bool const stalled =
            weight > p.minCONSENSUS_PCT || weight < (100 - p.minCONSENSUS_PCT);

        if (stalled)
        {
            // stalling is an error condition for even a single
            // transaction.
            std::stringstream s;
            s << "Transaction " << ID() << " is stalled. We have been voting "
              << (getOurVote() ? "YES" : "NO") << " for " << currentVoteCounter_
              << " rounds. Peers have not changed their votes in "
              << peersUnchanged << " rounds. The transaction has " << weight
              << "% support. ";
            JLOG(j_.error()) << s.str();
            CLOG(clog) << s.str();
        }

        return stalled;
    }

    //! The disputed transaction.
    Tx_t const&
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
    setVote(NodeID_t const& peer, bool votesYes);

    /** Remove a peer's vote

        @param peer Identifier of peer.
    */
    void
    unVote(NodeID_t const& peer);

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
    Json::Value
    getJson() const;

private:
    int yays_;      //< Number of yes votes
    int nays_;      //< Number of no votes
    bool ourVote_;  //< Our vote (true is yes)
    Tx_t tx_;       //< Transaction under dispute
    Map_t votes_;   //< Map from NodeID to vote
    //! The number of rounds we've gone without changing our vote
    std::size_t currentVoteCounter_ = 0;
    //! Which minimum acceptance percentage phase we are currently in
    ConsensusParms::AvalancheState avalancheState_ = ConsensusParms::init;
    //! How long we have been in the current acceptance phase
    std::size_t avalancheCounter_ = 0;
    beast::Journal const j_;
};

// Track a peer's yes/no vote on a particular disputed tx_
template <class Tx_t, class NodeID_t>
bool
DisputedTx<Tx_t, NodeID_t>::setVote(NodeID_t const& peer, bool votesYes)
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
    else if (votesYes && !it->second)
    {
        JLOG(j_.debug()) << "Peer " << peer << " now votes YES on " << tx_.id();
        --nays_;
        ++yays_;
        it->second = true;
        return true;
    }
    // changes vote to no
    else if (!votesYes && it->second)
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
template <class Tx_t, class NodeID_t>
void
DisputedTx<Tx_t, NodeID_t>::unVote(NodeID_t const& peer)
{
    auto it = votes_.find(peer);

    if (it != votes_.end())
    {
        if (it->second)
            --yays_;
        else
            --nays_;

        votes_.erase(it);
    }
}

template <class Tx_t, class NodeID_t>
bool
DisputedTx<Tx_t, NodeID_t>::updateVote(
    int percentTime,
    bool proposing,
    ConsensusParms const& p)
{
    if (ourVote_ && (nays_ == 0))
        return false;

    if (!ourVote_ && (yays_ == 0))
        return false;

    bool newPosition;
    int weight;

    // When proposing, to prevent avalanche stalls, we increase the needed
    // weight slightly over time. We also need to ensure that the consensus has
    // made a minimum number of attempts at each "state" before moving
    // to the next.
    // Proposing or not, we need to keep track of which state we've reached so
    // we can determine if the vote has stalled.
    auto const [requiredPct, newState] = getNeededWeight(
        p, avalancheState_, percentTime, ++avalancheCounter_, p.avMIN_ROUNDS);
    if (newState)
    {
        avalancheState_ = *newState;
        avalancheCounter_ = 0;
    }

    if (proposing)  // give ourselves full weight
    {
        // This is basically the percentage of nodes voting 'yes' (including us)
        weight = (yays_ * 100 + (ourVote_ ? 100 : 0)) / (nays_ + yays_ + 1);

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
        JLOG(j_.info()) << "No change (" << (ourVote_ ? "YES" : "NO") << ") on "
                        << tx_.id() << " : weight " << weight << ", percent "
                        << percentTime
                        << ", round(s) with this vote: " << currentVoteCounter_;
        JLOG(j_.debug()) << Json::Compact{getJson()};
        return false;
    }

    currentVoteCounter_ = 0;
    ourVote_ = newPosition;
    JLOG(j_.debug()) << "We now vote " << (ourVote_ ? "YES" : "NO") << " on "
                     << tx_.id();
    JLOG(j_.debug()) << Json::Compact{getJson()};
    return true;
}

template <class Tx_t, class NodeID_t>
Json::Value
DisputedTx<Tx_t, NodeID_t>::getJson() const
{
    using std::to_string;

    Json::Value ret(Json::objectValue);

    ret["yays"] = yays_;
    ret["nays"] = nays_;
    ret["our_vote"] = ourVote_;

    if (!votes_.empty())
    {
        Json::Value votesj(Json::objectValue);
        for (auto const& [nodeId, vote] : votes_)
            votesj[to_string(nodeId)] = vote;
        ret["votes"] = std::move(votesj);
    }

    return ret;
}

}  // namespace ripple

#endif
