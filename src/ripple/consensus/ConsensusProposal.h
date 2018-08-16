//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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
#ifndef RIPPLE_CONSENSUS_ConsensusProposal_H_INCLUDED
#define RIPPLE_CONSENSUS_ConsensusProposal_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/JsonFields.h>
#include <cstdint>

namespace ripple {
/** Represents a proposed position taken during a round of consensus.

    During consensus, peers seek agreement on a set of transactions to
    apply to the prior ledger to generate the next ledger.  Each peer takes a
    position on whether to include or exclude potential transactions.
    The position on the set of transactions is proposed to its peers as an
    instance of the ConsensusProposal class.

    An instance of ConsensusProposal can be either our own proposal or one of
    our peer's.

    As consensus proceeds, peers may change their position on the transaction,
    or choose to abstain. Each successive proposal includes a strictly
    monotonically increasing number (or, if a peer is choosing to abstain,
    the special value `seqLeave`).

    Refer to @ref Consensus for requirements of the template arguments.

    @tparam NodeID_t Type used to uniquely identify nodes/peers
    @tparam LedgerID_t Type used to uniquely identify ledgers
    @tparam Position_t Type used to represent the position taken on transactions
                       under consideration during this round of consensus
 */
template <class NodeID_t, class LedgerID_t, class Position_t>
class ConsensusProposal
{
public:
    using NodeID = NodeID_t;

    //< Sequence value when a peer initially joins consensus
    static std::uint32_t const seqJoin = 0;

    //< Sequence number when  a peer wants to bow out and leave consensus
    static std::uint32_t const seqLeave = 0xffffffff;

    /** Constructor

        @param prevLedger The previous ledger this proposal is building on.
        @param seq The sequence number of this proposal.
        @param position The position taken on transactions in this round.
        @param closeTime Position of when this ledger closed.
        @param now Time when the proposal was taken.
        @param nodeID ID of node/peer taking this position.
    */
    ConsensusProposal(
        LedgerID_t const& prevLedger,
        std::uint32_t seq,
        Position_t const& position,
        NetClock::time_point closeTime,
        NetClock::time_point now,
        NodeID_t const& nodeID)
        : previousLedger_(prevLedger)
        , position_(position)
        , closeTime_(closeTime)
        , time_(now)
        , proposeSeq_(seq)
        , nodeID_(nodeID)
    {
    }

    //! Identifying which peer took this position.
    NodeID_t const&
    nodeID() const
    {
        return nodeID_;
    }

    //! Get the proposed position.
    Position_t const&
    position() const
    {
        return position_;
    }

    //! Get the prior accepted ledger this position is based on.
    LedgerID_t const&
    prevLedger() const
    {
        return previousLedger_;
    }

    /** Get the sequence number of this proposal

        Starting with an initial sequence number of `seqJoin`, successive
        proposals from a peer will increase the sequence number.

        @return the sequence number
    */
    std::uint32_t
    proposeSeq() const
    {
        return proposeSeq_;
    }

    //! The current position on the consensus close time.
    NetClock::time_point const&
    closeTime() const
    {
        return closeTime_;
    }

    //! Get when this position was taken.
    NetClock::time_point const&
    seenTime() const
    {
        return time_;
    }

    /** Whether this is the first position taken during the current
        consensus round.
    */
    bool
    isInitial() const
    {
        return proposeSeq_ == seqJoin;
    }

    //! Get whether this node left the consensus process
    bool
    isBowOut() const
    {
        return proposeSeq_ == seqLeave;
    }

    //! Get whether this position is stale relative to the provided cutoff
    bool
    isStale(NetClock::time_point cutoff) const
    {
        return time_ <= cutoff;
    }

    /** Update the position during the consensus process. This will increment
        the proposal's sequence number if it has not already bowed out.

        @param newPosition The new position taken.
        @param newCloseTime The new close time.
        @param now the time The new position was taken
     */
    void
    changePosition(
        Position_t const& newPosition,
        NetClock::time_point newCloseTime,
        NetClock::time_point now)
    {
        position_ = newPosition;
        closeTime_ = newCloseTime;
        time_ = now;
        if (proposeSeq_ != seqLeave)
            ++proposeSeq_;
    }

    /** Leave consensus

        Update position to indicate the node left consensus.

        @param now Time when this node left consensus.
     */
    void
    bowOut(NetClock::time_point now)
    {
        time_ = now;
        proposeSeq_ = seqLeave;
    }

    //! Get JSON representation for debugging
    Json::Value
    getJson() const
    {
        using std::to_string;

        Json::Value ret = Json::objectValue;
        ret[jss::previous_ledger] = to_string(prevLedger());

        if (!isBowOut())
        {
            ret[jss::transaction_hash] = to_string(position());
            ret[jss::propose_seq] = proposeSeq();
        }

        ret[jss::close_time] =
            to_string(closeTime().time_since_epoch().count());

        return ret;
    }

private:
    //! Unique identifier of prior ledger this proposal is based on
    LedgerID_t previousLedger_;

    //! Unique identifier of the position this proposal is taking
    Position_t position_;

    //! The ledger close time this position is taking
    NetClock::time_point closeTime_;

    // !The time this position was last updated
    NetClock::time_point time_;

    //! The sequence number of these positions taken by this node
    std::uint32_t proposeSeq_;

    //! The identifier of the node taking this position
    NodeID_t nodeID_;
};

template <class NodeID_t, class LedgerID_t, class Position_t>
bool
operator==(
    ConsensusProposal<NodeID_t, LedgerID_t, Position_t> const& a,
    ConsensusProposal<NodeID_t, LedgerID_t, Position_t> const& b)
{
    return a.nodeID() == b.nodeID() && a.proposeSeq() == b.proposeSeq() &&
        a.prevLedger() == b.prevLedger() && a.position() == b.position() &&
        a.closeTime() == b.closeTime() && a.seenTime() == b.seenTime();
}
}
#endif
