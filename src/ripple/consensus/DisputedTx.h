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

#ifndef RIPPLE_APP_CONSENSUS_IMPL_DISPUTEDTX_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_IMPL_DISPUTEDTX_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/base_uint.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/consensus/ConsensusParms.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>
#include <memory>

namespace ripple {

/** A transaction discovered to be in dispute during conensus.

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

public:
    /** Constructor

        @param tx The transaction under dispute
        @param ourVote Our vote on whether tx should be included
        @param j Journal for debugging
    */
    DisputedTx(Tx_t const& tx, bool ourVote, beast::Journal j)
        : yays_(0), nays_(0), ourVote_(ourVote), tx_(tx), j_(j)
    {
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
    */
    void
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

    hash_map<NodeID_t, bool> votes_;  //< Votes of our peers
    beast::Journal j_;                //< Debug journal
};

// Track a peer's yes/no vote on a particular disputed tx_
template <class Tx_t, class NodeID_t>
void
DisputedTx<Tx_t, NodeID_t>::setVote(NodeID_t const& peer, bool votesYes)
{
    auto res = votes_.insert(std::make_pair(peer, votesYes));

    // new vote
    if (res.second)
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
    }
    // changes vote to yes
    else if (votesYes && !res.first->second)
    {
        JLOG(j_.debug()) << "Peer " << peer << " now votes YES on " << tx_.id();
        --nays_;
        ++yays_;
        res.first->second = true;
    }
    // changes vote to no
    else if (!votesYes && res.first->second)
    {
        JLOG(j_.debug()) << "Peer " << peer << " now votes NO on " << tx_.id();
        ++nays_;
        --yays_;
        res.first->second = false;
    }
}

// Remove a peer's vote on this disputed transasction
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

    if (proposing)  // give ourselves full weight
    {
        // This is basically the percentage of nodes voting 'yes' (including us)
        weight = (yays_ * 100 + (ourVote_ ? 100 : 0)) / (nays_ + yays_ + 1);

        // VFALCO TODO Rename these macros and turn them into language
        //             constructs.  consolidate them into a class that collects
        //             all these related values.
        //
        // To prevent avalanche stalls, we increase the needed weight slightly
        // over time.
        if (percentTime < p.avMID_CONSENSUS_TIME)
            newPosition = weight > p.avINIT_CONSENSUS_PCT;
        else if (percentTime < p.avLATE_CONSENSUS_TIME)
            newPosition = weight > p.avMID_CONSENSUS_PCT;
        else if (percentTime < p.avSTUCK_CONSENSUS_TIME)
            newPosition = weight > p.avLATE_CONSENSUS_PCT;
        else
            newPosition = weight > p.avSTUCK_CONSENSUS_PCT;
    }
    else
    {
        // don't let us outweigh a proposing node, just recognize consensus
        weight = -1;
        newPosition = yays_ > nays_;
    }

    if (newPosition == ourVote_)
    {
        JLOG(j_.info()) << "No change (" << (ourVote_ ? "YES" : "NO")
                        << ") : weight " << weight << ", percent "
                        << percentTime;
        JLOG(j_.debug()) << Json::Compact{getJson()};
        return false;
    }

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
        for (auto& vote : votes_)
            votesj[to_string(vote.first)] = vote.second;
        ret["votes"] = std::move(votesj);
    }

    return ret;
}

}  // ripple

#endif
