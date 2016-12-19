
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
#include <ripple/app/ledger/LedgerProposal.h>
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
    using NetTime_t = NetClock::time_point;
    using Ledger_t = RCLCxLedger;
    using Proposal_t = LedgerProposal;
    using TxSet_t = RCLTxSet;
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
public:
    using Base = Consensus<RCLConsensus, RCLCxTraits>;

    //! Constructor
    RCLConsensus(
        Application& app,
        std::unique_ptr<FeeVote> && feeVote,
        LedgerMaster& ledgerMaster,
        LocalTxs& localTxs,
        InboundTransactions& inboundTransactions,
        typename Base::clock_type const & clock,
        beast::Journal journal);

    static char const* getCountedObjectName() { return "Consensus"; }

    /** Save the given consensus proposed by a peer with nodeID for later
        use in consensus.

        @param proposal Proposed peer position
        @param nodeID ID of peer
    */
    void
    storeProposal( LedgerProposal::ref proposal, NodeID const& nodeID);

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

    /** Get peer's proposed positions.
        @param prevLedger The base ledger which proposals are based on
        @return The set of proposals
    */
    std::vector<LedgerProposal>
    proposals (LedgerHash const& prevLedger);

    /** Relay the given proposal to all peers
        @param proposal The proposal to relay.
     */
    void
    relay(LedgerProposal const & proposal);

    /** Relay disputed transacction to peers.

        Only relay if the provided transaction hasn't been shared recently.

        @param tx The disputed transaction to relay.
    */
    void
    relay(DisputedTx <RCLCxTx, NodeID> const & dispute);

     /** Acquire the transaction set associated with a proposal.

         If the transaction set is not available locally, will attempt acquire it
         from the network.

         @param position The proposal to acquire transactions for
         @return Optional set of transactions, seated if available.
    */
    boost::optional<RCLTxSet>
    acquireTxSet(LedgerProposal const & position);

    /** @return whether the open ledger has any transactions
    */
    bool
    hasOpenTransactions() const;

    /**
        @param h The hash of the ledger of interest
        @return the number of proposers that validated a ledger
    */
    int
    numProposersValidated(LedgerHash const & h) const;

    /**
        @param h The hash of the ledger of interest.
        @return The number of validating peers that have validated a ledger
                succeeding the one provided.
    */
    int
    numProposersFinished(LedgerHash const & h) const;

    /** Propose the given position to my peers.

        @param position Our proposed position
    */
    void
    propose (LedgerProposal const& position);

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
    std::pair <RCLTxSet, LedgerProposal>
    makeInitialPosition (
        RCLCxLedger const & prevLedger,
        bool isProposing,
        bool isCorrectLCL,
        NetClock::time_point closeTime,
        NetClock::time_point now);

    //!-------------------------------------------------------------------------

    /** Notify peers of a consensus state change

        @param ne Event type for notification
        @param ledger The ledger at the time of the state change
        @param haveCorrectLCL Whether we believ we have the correct LCL.
    */
    void
    notify(protocol::NodeEvent ne, RCLCxLedger const & ledger, bool haveCorrectLCL);

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

    using Proposals = hash_map <NodeID, std::deque<LedgerProposal::pointer>>;
    Proposals proposals_;
    std::mutex proposalsLock_;
};

}

#endif
