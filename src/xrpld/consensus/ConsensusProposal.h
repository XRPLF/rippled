/** @file
 *  Defines ConsensusProposal, the fundamental unit of peer communication in
 *  the XRP Ledger BFT consensus protocol.
 *
 *  Each round, every participating validator broadcasts a ConsensusProposal
 *  identifying the transaction set it believes should be included in the next
 *  ledger and when that ledger should close.  The class is a header-only
 *  template decoupled from concrete XRPL types; the production instantiation
 *  is `ConsensusProposal<NodeID, uint256, uint256>` wrapped by `RCLCxPeerPos`
 *  with a cryptographic signature for network propagation.
 */
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
/** A proposed position broadcast by a validator during a consensus round.
 *
 *  Encapsulates a peer's view of two things: which transaction set (identified
 *  by `Position`) should be included in the next ledger, and when that ledger
 *  should close (`NetClock::time_point`).  Either field may be updated during
 *  the round via `changePosition()`; each update increments `proposeSeq_` so
 *  recipients can discard out-of-order or duplicate messages.  A peer that
 *  withdraws from consensus entirely calls `bowOut()`, which sets `proposeSeq_`
 *  to the sentinel `kSEQ_LEAVE`.
 *
 *  This class is a header-only template to keep the consensus engine decoupled
 *  from concrete XRPL types.  Production code instantiates it as
 *  `ConsensusProposal<NodeID, uint256, uint256>` inside `RCLCxPeerPos`, which
 *  adds a cryptographic signature.  Unit tests may use simple integer types.
 *
 *  @note The signing hash covers five fields (prefix, seq, close time, prev
 *      ledger, position) and is lazily cached in `signingHash_`.  Callers
 *      must not mutate the proposal without going through `changePosition()` or
 *      `bowOut()`, both of which invalidate the cache via `signingHash_.reset()`.
 *
 *  Refer to @ref Consensus for requirements of the template arguments.
 *
 *  @tparam NodeId Type used to uniquely identify nodes/peers
 *  @tparam LedgerId Type used to uniquely identify ledgers
 *  @tparam Position Type used to represent the position taken on transactions
 *      under consideration during this round of consensus (typically a tx-set
 *      hash, not the full set)
 */
template <class NodeId, class LedgerId, class Position>
class ConsensusProposal
{
public:
    using NodeID = NodeId;

    /** Sequence value for a peer's first proposal in a round.
     *
     *  `isInitial()` tests for this sentinel.  The consensus engine collects
     *  `closeTime()` from all initial proposals to measure inter-peer clock
     *  drift via `ConsensusCloseTimes`.
     */
    static std::uint32_t const kSEQ_JOIN = 0;

    /** Sequence value signalling that a peer is voluntarily leaving consensus.
     *
     *  `isBowOut()` tests for this sentinel.  Once set, `changePosition()`
     *  will not increment the sequence number further, preventing accidental
     *  re-entry.  Receiving peers remove the bowing-out node from
     *  `currPeerPositions_` and add it to `deadNodes_` for the remainder of
     *  the round.
     */
    static std::uint32_t const kSEQ_LEAVE = 0xffffffff;

    /** Construct a proposal for the given round and position.
     *
     *  @param prevLedger ID of the ledger this proposal builds on.
     *  @param seq Sequence number of this proposal; use `kSEQ_JOIN` (0) for
     *      a node's first proposal in the round.
     *  @param position Hash of the transaction set this node proposes to
     *      include in the next ledger.
     *  @param closeTime This node's estimate (in `NetClock`) of when the
     *      ledger should close.
     *  @param now Wall-clock time at which the proposal was created; stored
     *      as `seenTime()` for staleness detection.
     *  @param nodeID Identifier of the node originating this proposal.
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

    /** Identifier of the node that originated this proposal. */
    NodeId const&
    nodeID() const
    {
        return nodeID_;
    }

    /** Hash identifying the transaction set this node proposes to include. */
    Position const&
    position() const
    {
        return position_;
    }

    /** ID of the ledger this proposal is building upon.
     *
     *  The consensus engine rejects any incoming proposal whose `prevLedger()`
     *  does not match the current working ledger ID, preventing split-brain
     *  when a ledger switch is in progress.
     */
    LedgerId const&
    prevLedger() const
    {
        return previousLedger_;
    }

    /** Monotonically increasing counter ordering this peer's proposals.
     *
     *  Starts at `kSEQ_JOIN` (0) for the first proposal of the round.
     *  Incremented by `changePosition()` on each position update.
     *  Set to `kSEQ_LEAVE` (0xffffffff) when the peer bows out.
     *  The sequence number is included in the signing hash, so stale
     *  proposals cannot be replayed with a forged higher sequence.
     *
     *  @return The current proposal sequence number.
     */
    std::uint32_t
    proposeSeq() const
    {
        return proposeSeq_;
    }

    /** This node's estimate of when the ledger should close, in `NetClock`.
     *
     *  @note This is a `NetClock` value representing the proposer's preferred
     *      ledger-close time — it is unrelated to the local wall-clock
     *      observation time returned by `seenTime()`.  Conflating the two
     *      would be a temporal-domain bug.
     */
    NetClock::time_point const&
    closeTime() const
    {
        return closeTime_;
    }

    /** Local wall-clock time when this proposal was last created or updated.
     *
     *  Used by `isStale()` to detect peers that have gone silent.  This is
     *  the time the proposal was *observed locally*, not the proposer's
     *  estimate of the ledger close time (see `closeTime()`).
     */
    NetClock::time_point const&
    seenTime() const
    {
        return time_;
    }

    /** Whether this is the first position broadcast in the current round.
     *
     *  True when `proposeSeq_ == kSEQ_JOIN` (0).  The consensus engine
     *  records the `closeTime()` of all initial proposals in
     *  `ConsensusCloseTimes` to measure inter-peer clock drift.
     */
    bool
    isInitial() const
    {
        return proposeSeq_ == kSEQ_JOIN;
    }

    /** Whether this peer has voluntarily withdrawn from the consensus round.
     *
     *  True when `proposeSeq_ == kSEQ_LEAVE` (0xffffffff).  Receiving peers
     *  erase the node from active peer positions and add it to `deadNodes_`,
     *  permanently excluding its votes for the remainder of the round.
     */
    bool
    isBowOut() const
    {
        return proposeSeq_ == kSEQ_LEAVE;
    }

    /** Whether this proposal has not been updated recently enough to act on.
     *
     *  Compares `seenTime()` (local observation time) against `cutoff`.
     *  The consensus engine computes separate cutoffs for peer proposals and
     *  the local node's own position; proposals older than their cutoff are
     *  discarded to prevent blocking on a silently-dead peer.
     *
     *  @param cutoff Threshold time; proposals seen at or before this instant
     *      are considered stale.
     *  @return `true` if `seenTime() <= cutoff`.
     */
    bool
    isStale(NetClock::time_point cutoff) const
    {
        return time_ <= cutoff;
    }

    /** Update the transaction-set position and close-time estimate.
     *
     *  Increments `proposeSeq_` unless the peer has already bowed out
     *  (`kSEQ_LEAVE`), which would otherwise allow a stale position to
     *  masquerade as a new one.  Invalidates the cached signing hash so that
     *  `signingHash()` recomputes over the updated fields.
     *
     *  @param newPosition Hash of the updated transaction set.
     *  @param newCloseTime Updated `NetClock` estimate of ledger close time.
     *  @param now Local wall-clock time of this update; stored as `seenTime()`.
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
        if (proposeSeq_ != kSEQ_LEAVE)
            ++proposeSeq_;
    }

    /** Signal that this node is voluntarily withdrawing from the round.
     *
     *  Sets `proposeSeq_` to `kSEQ_LEAVE` and updates `seenTime()`.
     *  Invalidates the cached signing hash.  After this call `isBowOut()`
     *  returns `true` and subsequent `changePosition()` calls will not
     *  increment the sequence number further.
     *
     *  @param now Local wall-clock time of the withdrawal.
     */
    void
    bowOut(NetClock::time_point now)
    {
        signingHash_.reset();
        time_ = now;
        proposeSeq_ = kSEQ_LEAVE;
    }

    /** Produce a human-readable single-line summary for logging.
     *
     *  Includes all six fields: previous ledger, sequence, position, close
     *  time, seen time, bow-out flag, and node ID.
     *
     *  @return A diagnostic string; format is not stable across versions.
     */
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

    /** JSON representation of the proposal for diagnostics.
     *
     *  Always includes `previous_ledger` and `close_time`.  When the proposal
     *  is a bow-out (`isBowOut()` is true), `transaction_hash` and
     *  `propose_seq` are omitted to avoid surfacing the meaningless
     *  `0xffffffff` sentinel in log output.
     *
     *  @return A JSON object suitable for debug logging or RPC responses.
     */
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

    /** SHA-512 half-digest covering the five signed fields of this proposal.
     *
     *  The hash is computed over `HashPrefix::Proposal`, `proposeSeq_`,
     *  `closeTime_` epoch count, `previousLedger_`, and `position_` — exactly
     *  the fields that `RCLCxPeerPos::checkSign()` verifies against the peer's
     *  signature.
     *
     *  The result is lazily cached in `signingHash_` and recomputed on first
     *  access after any mutation.  `changePosition()` and `bowOut()` must
     *  call `signingHash_.reset()` before modifying fields; reading a stale
     *  cache would silently produce an incorrect hash.
     *
     *  @return Reference to the cached signing hash (valid until the next
     *      mutation).
     */
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

    //! The time this position was last updated
    NetClock::time_point time_;

    //! The sequence number of these positions taken by this node
    std::uint32_t proposeSeq_;

    //! The identifier of the node taking this position
    NodeId nodeID_;

    //! The signing hash for this proposal
    mutable std::optional<uint256> signingHash_;
};

/** Compare two proposals for equality across all six fields.
 *
 *  Compares node ID, sequence number, previous ledger, position, close time,
 *  and `seenTime()`.  Because `seenTime()` is the local wall-clock observation
 *  time, two logically identical proposals received at different instants will
 *  *not* compare equal.  This is intentional for de-duplication within a
 *  single round; network-level suppression of duplicate wire messages is
 *  handled separately by the hash router in `RCLCxPeerPos`.
 */
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
