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

#ifndef RIPPLE_APP_LEDGER_IMPL_LEDGERCONSENSUSIMP_H_INCLUDED
#define RIPPLE_APP_LEDGER_IMPL_LEDGERCONSENSUSIMP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/impl/ConsensusImp.h>
#include <ripple/app/ledger/impl/DisputedTx.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/UintTypes.h>

namespace ripple {

/**
  Provides the implementation for LedgerConsensus.

  Achieves consensus on the next ledger.

  Two things need consensus:
    1.  The set of transactions.
    2.  The close time for the ledger.
*/
template <class Traits>
class LedgerConsensusImp
    : public LedgerConsensus<Traits>
    , public std::enable_shared_from_this <LedgerConsensusImp<Traits>>
    , public CountedObject <LedgerConsensusImp<Traits>>
{
private:
    enum class State
    {
        // We haven't closed our ledger yet, but others might have
        open,

        // Establishing consensus
        establish,

        // We have closed on a transaction set and are
        // processing the new ledger
        processing,

        // We have accepted / validated a new last closed ledger
        // and need to start a new round
        accepted,
    };

public:

    using typename Traits::Time_t;
    using typename Traits::Pos_t;
    using typename Traits::TxSet_t;
    using typename Traits::Tx_t;
    using typename Traits::LgrID_t;
    using typename Traits::TxID_t;
    using typename Traits::TxSetID_t;
    using typename Traits::NodeID_t;
    using Dispute_t = DisputedTx <Traits>;

    /**
     * The result of applying a transaction to a ledger.
    */
    enum {resultSuccess, resultFail, resultRetry};

    static char const* getCountedObjectName () { return "LedgerConsensus"; }

    LedgerConsensusImp(LedgerConsensusImp const&) = delete;
    LedgerConsensusImp& operator=(LedgerConsensusImp const&) = delete;

    ~LedgerConsensusImp () = default;


    /**
        @param localtx transactions issued by local clients
        @param inboundTransactions set of inbound transaction sets
        @param localtx A set of local transactions to apply
        @param feeVote Our desired fee levels and voting logic.
    */
    LedgerConsensusImp (
        Application& app,
        ConsensusImp& consensus,
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerMaster& ledgerMaster,
        FeeVote& feeVote);

    /**
        @param prevLCLHash The hash of the Last Closed Ledger (LCL).
        @param previousLedger Best guess of what the LCL was.
        @param closeTime Closing time point of the LCL.
        @param previousProposers the number of participants in the last round
        @param previousConvergeTime how long the last round took (ms)
    */
    void startRound (
        LgrID_t const& prevLCLHash,
        std::shared_ptr<Ledger const> const& prevLedger,
        Time_t closeTime,
        int previousProposers,
        std::chrono::milliseconds previousConvergeTime) override;

    /**
      Get the Json state of the consensus process.
      Called by the consensus_info RPC.

      @param full True if verbose response desired.
      @return     The Json state.
    */
    Json::Value getJson (bool full) override;

    /* The hash of the last closed ledger */
    LgrID_t getLCL () override;

    /**
      We have a complete transaction set, typically acquired from the network

      @param map      the transaction set.
    */
    void gotMap (TxSet_t const& map) override;

    /**
      On timer call the correct handler for each state.
    */
    void timerEntry () override;

    /**
      A server has taken a new position, adjust our tracking
      Called when a peer takes a new postion.

      @param newPosition the new position
      @return            true if we should do delayed relay of this position.
    */
    bool peerPosition (Pos_t const& newPosition) override;

    void simulate(
        boost::optional<std::chrono::milliseconds> consensusDelay) override;

    /**
      Put a transaction set where peers can find it
    */
    void shareSet (TxSet_t const&);

private:
    /**
      Handle pre-close state.
    */
    void statePreClose ();

    /** We are establishing a consensus
       Update our position only on the timer, and in this state.
       If we have consensus, move to the finish state
    */
    void stateEstablish ();

    /** Check if we've reached consensus */
    bool haveConsensus ();

    /**
      Check if our last closed ledger matches the network's.
      This tells us if we are still in sync with the network.
      This also helps us if we enter the consensus round with
      the wrong ledger, to leave it with the correct ledger so
      that we can participate in the next round.
    */
    void checkLCL ();

    /**
      Change our view of the last closed ledger

      @param lclHash Hash of the last closed ledger.
    */
    void handleLCL (LgrID_t const& lclHash);

    /**
      We have a complete transaction set, typically acquired from the network

      @param map      the transaction set.
      @param acquired true if we have acquired the transaction set.
    */
    void mapCompleteInternal (
        TxSet_t const& map,
        bool acquired);

    /** We have a new last closed ledger, process it. Final accept logic

      @param set Our consensus set
    */
    void accept (TxSet_t const& set);

    /**
      Compare two proposed transaction sets and create disputed
        transctions structures for any mismatches

      @param m1 One transaction set
      @param m2 The other transaction set
    */
    void createDisputes (TxSet_t const& m1,
                         TxSet_t const& m2);

    /**
      Add a disputed transaction (one that at least one node wants
      in the consensus set and at least one node does not) to our tracking

      @param tx   The disputed transaction
    */
    void addDisputedTransaction (Tx_t const& tx);

    /**
      Adjust the votes on all disputed transactions based
        on the set of peers taking this position

      @param map   A disputed position
      @param peers peers which are taking the position map
    */
    void adjustCount (TxSet_t const& map,
        std::vector<NodeID_t> const& peers);

    /**
      Revoke our outstanding proposal, if any, and
      cease proposing at least until this round ends
    */
    void leaveConsensus ();

    /** Make and send a proposal
    */
    void propose ();

    /** Send a node status change message to our directly connected peers

      @param event   The event which caused the status change.  This is
                     typically neACCEPTED_LEDGER or neCLOSING_LEDGER.
      @param ledger  The ledger associated with the event.
    */
    void statusChange (protocol::NodeEvent event, ReadView const& ledger);

    /** Determine our initial proposed transaction set based on
        our open ledger
    */
    std::pair <TxSet_t, Pos_t> makeInitialPosition();

    /** Take an initial position on what we think the consensus set should be
    */
    void takeInitialPosition ();

    /**
       Called while trying to avalanche towards consensus.
       Adjusts our positions to try to agree with other validators.
    */
    void updateOurPositions ();

    /** If we radically changed our consensus context for some reason,
        we need to replay recent proposals so that they're not lost.
    */
    void playbackProposals ();

    /** We have just decided to close the ledger. Start the consensus timer,
       stash the close time, inform peers, and take a position
    */
    void closeLedger ();

    /**
      If we missed a consensus round, we may be missing a validation.
      This will send an older owed validation if we previously missed it.
    */
    void checkOurValidation ();

    /** We have a new LCL and must accept it */
    void beginAccept (bool synchronous);

    void endConsensus (bool correctLCL);


    /** Add our load fee to our validation */
    void addLoad(STValidation::ref val);

    /** Convert an advertised close time to an effective close time */
    NetClock::time_point effectiveCloseTime(NetClock::time_point closeTime);

private:
    Application& app_;
    ConsensusImp& consensus_;
    InboundTransactions& inboundTransactions_;
    LocalTxs& localTX_;
    LedgerMaster& ledgerMaster_;
    FeeVote& feeVote_;
    std::recursive_mutex lock_;

    NodeID_t ourID_;
    State state_;

    // The wall time this ledger closed
    Time_t closeTime_;

    LgrID_t prevLedgerHash_;
    LgrID_t acquiringLedger_;

    std::shared_ptr<Ledger const> previousLedger_;
    boost::optional<Pos_t> ourPosition_;
    boost::optional<TxSet_t> ourSet_;
    PublicKey valPublic_;
    SecretKey valSecret_;
    bool proposing_, validating_, haveCorrectLCL_, consensusFail_;

    // How much time has elapsed since the round started
    std::chrono::milliseconds roundTime_;

    // How long the close has taken, expressed as a percentage of the time that
    // we expected it to take.
    int closePercent_;

    NetClock::duration closeResolution_;

    bool haveCloseTimeConsensus_;

    std::chrono::steady_clock::time_point consensusStartTime_;
    int previousProposers_;

    // Time it took for the last consensus round to converge
    std::chrono::milliseconds previousRoundTime_;

    // Convergence tracking, trusted peers indexed by hash of public key
    hash_map<NodeID_t, Pos_t>  peerPositions_;

    // Transaction Sets, indexed by hash of transaction tree
    hash_map<TxSetID_t, const TxSet_t> acquired_;

    // Disputed transactions
    hash_map<TxID_t, Dispute_t> disputes_;
    hash_set<TxSetID_t> compares_;

    // Close time estimates, keep ordered for predictable traverse
    std::map <Time_t, int> closeTimes_;

    // nodes that have bowed out of this consensus process
    hash_set<NodeID_t> deadNodes_;
    beast::Journal j_;

public:
    /** Returns validation public key */
    PublicKey const&
    getValidationPublicKey () const override;

    /** Set validation private and public key pair. */
    void
    setValidationKeys (
        SecretKey const& valSecret, PublicKey const& valPublic) override;
};

//------------------------------------------------------------------------------

std::shared_ptr <LedgerConsensus <RCLCxTraits>>
make_LedgerConsensus (
    Application& app,
    ConsensusImp& consensus,
    InboundTransactions& inboundTransactions,
    LocalTxs& localtx,
    LedgerMaster& ledgerMaster,
    FeeVote& feeVote);

//------------------------------------------------------------------------------
/** Apply a set of transactions to a ledger

  Typically the txFilter is used to reject transactions
  that already got in the prior ledger

  @param set            set of transactions to apply
  @param view           ledger to apply to
  @param txFilter       callback, return false to reject txn
  @return               retriable transactions
*/
CanonicalTXSet
applyTransactions (
    Application& app,
    RCLTxSet const& set,
    OpenView& view,
    std::function<bool(uint256 const&)> txFilter);

extern template class LedgerConsensusImp <RCLCxTraits>;

} // ripple

#endif
