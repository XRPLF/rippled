#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>
#include <sstream>

namespace xrpl {
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
    the special value `kSeqLeave`).

    Refer to @ref Consensus for requirements of the template arguments.

    @tparam NodeId Type used to uniquely identify nodes/peers
    @tparam LedgerId Type used to uniquely identify ledgers
    @tparam Position Type used to represent the position taken on transactions
                       under consideration during this round of consensus
 */
template <class NodeId, class LedgerId, class Position>
class ConsensusProposal
{
public:
    using NodeID = NodeId;

    //< Sequence value when a peer initially joins consensus
    static std::uint32_t const kSeqJoin = 0;

    //< Sequence number when  a peer wants to bow out and leave consensus
    static std::uint32_t const kSeqLeave = 0xffffffff;

    /** Constructor

        @param prevLedger The previous ledger this proposal is building on.
        @param seq The sequence number of this proposal.
        @param position The position taken on transactions in this round.
        @param closeTime Position of when this ledger closed.
        @param now Time when the proposal was taken.
        @param nodeID ID of node/peer taking this position.
    */
    ConsensusProposal(
        LedgerId const& prevLedger,
        std::uint32_t seq,
        Position const& position,
        NetClock::time_point closeTime,
        NetClock::time_point now,
        NodeId const& nodeID)
        : previousLedger_(prevLedger)
        , position_(position)
        , closeTime_(closeTime)
        , time_(now)
        , proposeSeq_(seq)
        , nodeID_(nodeID)
    {
    }

    //! Identifying which peer took this position.
    NodeId const&
    nodeID() const
    {
        return nodeID_;
    }

    //! Get the proposed position.
    Position const&
    position() const
    {
        return position_;
    }

    //! Get the prior accepted ledger this position is based on.
    LedgerId const&
    prevLedger() const
    {
        return previousLedger_;
    }

    /** Get the sequence number of this proposal

        Starting with an initial sequence number of `kSeqJoin`, successive
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
        return proposeSeq_ == kSeqJoin;
    }

    //! Get whether this node left the consensus process
    bool
    isBowOut() const
    {
        return proposeSeq_ == kSeqLeave;
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
        Position const& newPosition,
        NetClock::time_point newCloseTime,
        NetClock::time_point now)
    {
        signingHash_.reset();
        position_ = newPosition;
        closeTime_ = newCloseTime;
        time_ = now;
        if (proposeSeq_ != kSeqLeave)
            ++proposeSeq_;
    }

    /** Leave consensus

        Update position to indicate the node left consensus.

        @param now Time when this node left consensus.
     */
    void
    bowOut(NetClock::time_point now)
    {
        signingHash_.reset();
        time_ = now;
        proposeSeq_ = kSeqLeave;
    }

    std::string
    render() const
    {
        std::stringstream ss;
        ss << "proposal: previous_ledger: " << previousLedger_ << " proposal_seq: " << proposeSeq_
           << " position: " << position_ << " close_time: " << to_string(closeTime_)
           << " now: " << to_string(time_) << " is_bow_out:" << isBowOut()
           << " node_id: " << nodeID_;
        return ss.str();
    }

    //! Get JSON representation for debugging
    json::Value
    getJson() const
    {
        using std::to_string;

        json::Value ret = json::ValueType::Object;
        ret[jss::previous_ledger] = to_string(prevLedger());

        if (!isBowOut())
        {
            ret[jss::transaction_hash] = to_string(position());
            ret[jss::propose_seq] = proposeSeq();
        }

        ret[jss::close_time] = to_string(closeTime().time_since_epoch().count());

        return ret;
    }

    //! The digest for this proposal, used for signing purposes.
    uint256 const&
    signingHash() const
    {
        if (!signingHash_)
        {
            signingHash_ = sha512Half(
                HashPrefix::Proposal,
                std::uint32_t(proposeSeq()),
                closeTime().time_since_epoch().count(),
                prevLedger(),
                position());
        }

        return signingHash_.value();
    }

private:
    //! Unique identifier of prior ledger this proposal is based on
    LedgerId previousLedger_;

    //! Unique identifier of the position this proposal is taking
    Position position_;

    //! The ledger close time this position is taking
    NetClock::time_point closeTime_;

    // !The time this position was last updated
    NetClock::time_point time_;

    //! The sequence number of these positions taken by this node
    std::uint32_t proposeSeq_;

    //! The identifier of the node taking this position
    NodeId nodeID_;

    //! The signing hash for this proposal
    mutable std::optional<uint256> signingHash_;
};

template <class NodeId, class LedgerId, class Position>
bool
operator==(
    ConsensusProposal<NodeId, LedgerId, Position> const& a,
    ConsensusProposal<NodeId, LedgerId, Position> const& b)
{
    return a.nodeID() == b.nodeID() && a.proposeSeq() == b.proposeSeq() &&
        a.prevLedger() == b.prevLedger() && a.position() == b.position() &&
        a.closeTime() == b.closeTime() && a.seenTime() == b.seenTime();
}
}  // namespace xrpl
