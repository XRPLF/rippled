//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVID_tED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
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

#include <cstdint>
#include <ripple/json/json_value.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple
{
/** Represents a proposed position taken during a round of consensus.

    During consensus, peers seek agreement on a set of transactions to
    apply to the prior ledger to generate the next ledger.  Each peer takes a
    position on whether to include or exclude potential transactions.
    The position on the set of transactions is proposed to its peers as an
    instance of the ConsensusProposal class.

    An instance of ConsensusProposal can be either our own proposal or one of
    our peer's.

    As consensus proceeds, peers may change their position on the
    transactions.  Each proposal has an increasing sequence number that orders
    the successive proposals.  A peer may also choose to leave consensus
    indicated by sending a ConsensuProposal with sequence equal to `seqLeave.

    @tparam NodeID_t Type used to uniquely identify nodes/peers
    @tparam LedgerID_t Type used to uniquely identify ledgers
    @tparam Position_t Type used to represent the position taken on transactions
                       under consideration during this round of consensus
    @tparam Time_t Type used to represent times related to consensus
 */
template <
    class NodeID_t,
    class LedgerID_t,
    class Position_t,
    class Time_t>
class ConsensusProposal
{
private:
    //< Sequence value when a peer initially joins consensus
    static std::uint32_t const seqJoin = 0;

    //< Sequence number when  a peer wants to bow out and leave consensus
    static std::uint32_t const seqLeave = 0xffffffff;

public:

    using NodeID = NodeID_t;

    /** (Peer) Constructor

        Constructs a peer's consensus proposal

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
        Time_t closeTime,
        Time_t now,
        NodeID_t const& nodeID)
    : previousLedger_(prevLedger)
    , position_(position)
    , closeTime_(closeTime)
    , time_(now)
    , proposeSeq_(seq)
    , nodeID_(nodeID)
    {

    }

    /** (Self) Constructor

        Constructs our initial consensus position for this ledger
        @param prevLedger The previous ledger this proposal is building on.
        @param position The position taken on transactions in this round.
        @param closeTime Position of when this ledger closed.
        @param now Time when the proposal was taken.
        @param nodeID Our ID
    */
    ConsensusProposal(
        LedgerID_t const& prevLgr,
        Position_t const& position,
        Time_t closeTime,
        Time_t now,
        NodeID_t const& nodeID)
    : previousLedger_(prevLgr)
    , position_(position)
    , closeTime_(closeTime)
    , time_(now)
    , proposeSeq_(seqJoin)
    , nodeID_(nodeID)
    {

    }

    //! @return Identifying index of which peer took this position.
    NodeID_t const&
    nodeID () const
    {
        return nodeID_;
    }

    //! @return The proposed position.
    Position_t const&
    position () const
    {
        return position_;
    }

    //! @return The prior accepted ledger this position is based on.
    LedgerID_t const&
    prevLedger () const
    {
        return previousLedger_;
    }

    /** Get the sequence number of this proposal.

        Starting with an initial sequence number of `seqJoin`, successive
        proposals from a peer will increase the sequence number.
        @return the sequence number
    */
    std::uint32_t
    proposeSeq () const
    {
        return proposeSeq_;
    }

    //! @return The current position on the consensus close time.
    Time_t
    closeTime () const
    {
        return closeTime_;
    }

    //! @return When this position was taken.
    Time_t
    seenTime () const
    {
        return time_;
    }

    /**
        @return Whether this is the first position taken during the current
        consensus round.
    */
    bool
    isInitial () const
    {
        return proposeSeq_ == seqJoin;
    }

    //! @return Whether this node left the consensus process
    bool
    isBowOut () const
    {
        return proposeSeq_ == seqLeave;
    }

    //! @return Whether this position is stale relative to the provided cutoff
    bool
    isStale (Time_t cutoff) const
    {
        return time_ <= cutoff;
    }

    /**
        Update the position during the consensus process. This will increment
        the proposal's sequence number.

        @param newPosition The new position taken.
        @param newCloseTime The new close time.
        @param now the time The new position was taken.

        @return `true` if the position was updated or `false` if this node has
        already left this consensus round.
     */
    bool
    changePosition(
        Position_t const& newPosition,
        Time_t newCloseTime,
        Time_t now)
    {
         if (proposeSeq_ == seqLeave)
            return false;

        position_    = newPosition;
        closeTime_      = newCloseTime;
        time_           = now;
        ++proposeSeq_;
        return true;
    }

    /** Leave consensus

        @param now Time when this node left consensus.
     */
    void
    bowOut(Time_t now)
    {
        time_           = now;
        proposeSeq_     = seqLeave;
    }

    //! @return JSON representation for debugging
    Json::Value
    getJson () const
    {
        using std::to_string;

        Json::Value ret = Json::objectValue;
        ret[jss::previous_ledger] = to_string (prevLedger());

        if (!isBowOut())
        {
            ret[jss::transaction_hash] = to_string (position());
            ret[jss::propose_seq] = proposeSeq();
        }

        ret[jss::close_time] = to_string(closeTime().time_since_epoch().count());

        return ret;
    }

private:

    //! Unique identifier of prior ledger this proposal is based on
    LedgerID_t previousLedger_;

    //! Unique identifier of the position this proposal is taking
    Position_t position_;

    //! The ledger close time this position is taking
    Time_t closeTime_;

    // !The time this position was last updated
    Time_t time_;

    //! The sequence number of these positions taken by this node
    std::uint32_t proposeSeq_;

    //! The identifier of the node taking this position
    NodeID_t nodeID_;

};
}
#endif
