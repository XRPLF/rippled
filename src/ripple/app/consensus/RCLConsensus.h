
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

#ifndef RIPPLE_APP_CONSENSUS_RCLCONSENSUS_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCONSENSUS_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/app/consensus/RCLCxLedger.h>
#include <ripple/app/consensus/RCLCxTx.h>
#include <ripple/app/consensus/RCLCxPeerPos.h>
#include <ripple/core/JobQueue.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/overlay/Message.h>

namespace ripple {

class InboundTransactions;
class LocalTxs;
class LedgerMaster;

//! Types used to adapt consensus for RCL
struct RCLCxTraits
{
    //! Ledger type presented to Consensus
    using Ledger_t = RCLCxLedger;
    //! Peer identifier type used in Consensus
    using NodeID_t = NodeID;
    //! TxSet type presented to Consensus
    using TxSet_t = RCLTxSet;
    //! MissingTxException type neede by Consensus
    using MissingTxException_t = SHAMapMissingNode;
};


/** Adapts the generic Consensus algorithm for use by RCL.

    @note The enabled_shared_from_this base allows the application to properly
    create a shared instance of RCLConsensus for use in the accept logic..
*/
class RCLConsensus : public Consensus<RCLConsensus, RCLCxTraits>
                   , public std::enable_shared_from_this <RCLConsensus>
                   , public CountedObject <RCLConsensus>
{
    using Base = Consensus<RCLConsensus, RCLCxTraits>;
    using Base::accept;
public:

    //! Constructor
    RCLConsensus(
        Application& app,
        std::unique_ptr<FeeVote> && feeVote,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs,
        InboundTransactions& inboundTransactions,
        typename Base::clock_type const & clock,
        beast::Journal journal);
    RCLConsensus(RCLConsensus const&) = delete;
    RCLConsensus& operator=(RCLConsensus const&) = delete;

    static char const* getCountedObjectName() { return "Consensus"; }

    /** Save the given consensus proposed by a peer with nodeID for later
        use in consensus.

        @param peerPos Proposed peer position
        @param nodeID ID of peer
    */
    void
    storeProposal( RCLCxPeerPos::ref peerPos, NodeID const& nodeID);

    /** Returns validation public key */
    PublicKey const&
    getValidationPublicKey () const;

    /** Set validation private and public key pair. */
    void
    setValidationKeys (SecretKey const& valSecret, PublicKey const& valPublic);

private:
    friend class Consensus<RCLConsensus, RCLCxTraits>;

    //-------------------------------------------------------------------------
    // Consensus type requirements.

    /** Notification that a new consensus round has begun.

        @param ledger The ledger we are building consensus on
    */
    void
    onStartRound(RCLCxLedger const & ledger);

    //! @return Whether consensus should be (proposing, validating)
    std::pair <bool, bool>
    getMode ();

    /** Attempt to acquire a specific ledger.

        If not available, asynchronously acquires from the network.

        @param ledger The ID/hash of the ledger acquire
        @return Optional ledger, will be seated if we locally had the ledger
     */
    boost::optional<RCLCxLedger>
    acquireLedger(LedgerHash const & ledger);

    /** Get peers' proposed positions.
        @param prevLedger The base ledger which proposals are based on
        @return The set of proposals
    */
    std::vector<RCLCxPeerPos>
    proposals (LedgerHash const& prevLedger);

    /** Relay the given proposal to all peers

        @param peerPos The peer position to relay.
     */
    void
    relay(RCLCxPeerPos const & peerPos);

    /** Relay disputed transacction to peers.

        Only relay if the provided transaction hasn't been shared recently.

        @param dispute The disputed transaction to relay.
    */
    void
    relay(DisputedTx <RCLCxTx, NodeID> const & dispute);

     /** Acquire the transaction set associated with a proposal.

         If the transaction set is not available locally, will attempt acquire it
         from the network.

         @param setId The transaction set ID associated with the proposal
         @return Optional set of transactions, seated if available.
    */
    boost::optional<RCLTxSet>
    acquireTxSet(RCLTxSet::ID const & setId);

    /** Whether the open ledger has any transactions
    */
    bool
    hasOpenTransactions() const;

    /** Number of proposers that have vallidated the given ledger

        @param h The hash of the ledger of interest
        @return the number of proposers that validated a ledger
    */
    std::size_t
    proposersValidated(LedgerHash const & h) const;

    /** Number of proposers that have validated a ledger descended from requested ledger.

        @param h The hash of the ledger of interest.
        @return The number of validating peers that have validated a ledger
                succeeding the one provided.
    */
    std::size_t
    proposersFinished(LedgerHash const & h) const;

    /** Propose the given position to my peers.

        @param proposal Our proposed position
    */
    void
    propose (RCLCxPeerPos::Proposal const& proposal);

    /** Share the given tx set with peers.

        @param set The TxSet to share.
    */
    void
    share (RCLTxSet const& set);

    /** Get the last closed ledger (LCL) seen on the network

        @param currentLedger Current ledger used in consensus
        @param priorLedger Prior ledger used in consensus
        @param believedCorrect Whether consensus believes currentLedger is LCL

        @return The hash of the last closed network
     */
    uint256
    getLCL (
        uint256 const& currentLedger,
        uint256 const& priorLedger,
        bool believedCorrect);


    /** Notification that the ledger has closed.

       @param ledger the ledger we are changing to
       @param haveCorrectLCL whether we believe this is the correct LCL
    */
    void
    onClose(RCLCxLedger const & ledger, bool haveCorrectLCL);

     /** Create our initial position of transactions to accept in this round
         of consensus.

          @param prevLedger The ledger the transactions apply to
          @param isProposing Whether we are currently proposing
          @param isCorrectLCL Whether we have the correct LCL
          @param closeTime When we believe the ledger closed
          @param now The current network adjusted time

          @return Pair of (i)  transactions we believe are in the ledger
                          (ii) the corresponding proposal of those transactions
                               to send to peers
     */
    std::pair <RCLTxSet, typename RCLCxPeerPos::Proposal>
    makeInitialPosition (
        RCLCxLedger const & prevLedger,
        bool isProposing,
        bool isCorrectLCL,
        NetClock::time_point closeTime,
        NetClock::time_point now);


    /** Dispatch a call to Consensus::accept

        Accepting a ledger may be expensive, so this function can dispatch
        that call to another thread if desired and must call the accept
        method of the generic consensus algorithm.

        @param txSet The transactions to accept.
    */
    void
    dispatchAccept(RCLTxSet const & txSet);


    /** Accept a new ledger based on the given transactions.

        TODO: Too many arguments, need to group related types.

        @param set The set of accepted transactions
        @param consensusCloseTime Consensus agreed upon close time
        @param proposing_ Whether we are proposing
        @param validating_ Whether we are validating
        @param haveCorrectLCL_ Whether we had the correct last closed ledger
        @param consensusFail_ Whether consensus failed
        @param prevLedgerHash_ The hash/id of the previous ledger
        @param previousLedger_ The previous ledger
        @param closeResolution_ The close time resolution used this round
        @param now Current network adjsuted time
        @param roundTime_ Duration of this consensus round
        @param disputes_ Disputed trarnsactions from this round
        @param closeTimes_ Histogram of peers close times
        @param closeTime Our close time
        @return Whether we should continue validating
     */
    bool
    accept(
        RCLTxSet const& set,
        NetClock::time_point consensusCloseTime,
        bool proposing_,
        bool validating_,
        bool haveCorrectLCL_,
        bool consensusFail_,
        LedgerHash const &prevLedgerHash_,
        RCLCxLedger const & previousLedger_,
        NetClock::duration closeResolution_,
        NetClock::time_point const & now,
        std::chrono::milliseconds const & roundTime_,
        hash_map<RCLCxTx::ID, DisputedTx <RCLCxTx, NodeID>> const & disputes_,
        std::map <NetClock::time_point, int> closeTimes_,
        NetClock::time_point const & closeTime
    );

    /** Signal the end of consensus to the application, which will start the
        next round.

        @param correctLCL Whether we believe we have the correct LCL
    */
    void
    endConsensus(bool correctLCL);

    //!-------------------------------------------------------------------------
    // Additional members (not directly required by Consensus interface)
    /** Notify peers of a consensus state change

        @param ne Event type for notification
        @param ledger The ledger at the time of the state change
        @param haveCorrectLCL Whether we believ we have the correct LCL.
    */
    void
    notify(protocol::NodeEvent ne, RCLCxLedger const & ledger, bool haveCorrectLCL);

      /** Build the new last closed ledger.

          Accept the given the provided set of consensus transactions and build
          the last closed ledger. Since consensus just agrees on which
          transactions to apply, but not whether they make it into the closed
          ledger, this function also populates retriableTxs with those that can
          be retried in the next round.

          @param previousLedger Prior ledger building upon
          @param set The set of transactions to apply to the ledger
          @param closeTime The the ledger closed
          @param closeTimeCorrect Whether consensus agreed on close time
          @param closeResolution Resolution used to determine consensus close time
          @param now Current network adjusted time
          @param roundTime Duration of this consensus rorund
          @param retriableTxs Populate with transactions to retry in next round
          @return The newly built ledger
    */
    RCLCxLedger
    buildLCL(
        RCLCxLedger const & previousLedger,
        RCLTxSet const & set,
        NetClock::time_point closeTime,
        bool closeTimeCorrect,
        NetClock::duration closeResolution,
        NetClock::time_point now,
        std::chrono::milliseconds roundTime,
        CanonicalTXSet & retriableTxs
    );

    /** Validate the given ledger and share with peers as necessary

        @param ledger The ledger to validate
        @param now Current network adjusted time
        @param proposing Whether we were proposing transactions while generating
                         this ledger.  If we are not proposing, a validation
                         can still be sent to inform peers that we know we
                         aren't fully participating in consensus but are still
                         around and trying to catch up.
    */
    void
    validate(
        RCLCxLedger const & ledger,
        NetClock::time_point now,
        bool proposing);

    //!-------------------------------------------------------------------------
    Application& app_;
    std::unique_ptr <FeeVote> feeVote_;
    LedgerMaster & ledgerMaster_;
    LocalTxs & localTxs_;
    InboundTransactions& inboundTransactions_;
    beast::Journal j_;

    NodeID nodeID_;
    PublicKey valPublic_;
    SecretKey valSecret_;
    LedgerHash acquiringLedger_;

    // The timestamp of the last validation we used, in network time. This is
    // only used for our own validations.
    NetClock::time_point lastValidationTime_;

    using PeerPositions = hash_map <NodeID, std::deque<RCLCxPeerPos::pointer>>;
    PeerPositions peerPositions_;
    std::mutex peerPositionsLock_;
};

}

#endif
