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
  This object is created when the consensus process starts, and
  is destroyed when the process is complete.

  Nearly everything herein is invoked with the master lock.

  Two things need consensus:
    1.  The set of transactions.
    2.  The close time for the ledger.
*/
class LedgerConsensusImp
    : public LedgerConsensus
    , public std::enable_shared_from_this <LedgerConsensusImp>
    , public CountedObject <LedgerConsensusImp>
{
private:
    enum class State
    {
        // We haven't closed our ledger yet, but others might have
        open,

        // Establishing consensus
        establish,

        // We have closed on a transaction set
        finished,

        // We have accepted / validated a new last closed ledger
        accepted,
    };

public:
    /**
     * The result of applying a transaction to a ledger.
    */
    enum {resultSuccess, resultFail, resultRetry};

    static char const* getCountedObjectName () { return "LedgerConsensus"; }

    LedgerConsensusImp(LedgerConsensusImp const&) = delete;
    LedgerConsensusImp& operator=(LedgerConsensusImp const&) = delete;

    ~LedgerConsensusImp () = default;

    /**
        @param previousProposers the number of participants in the last round
        @param previousConvergeTime how long the last round took (ms)
        @param inboundTransactions
        @param localtx transactions issued by local clients
        @param inboundTransactions the set of
        @param localtx A set of local transactions to apply
        @param prevLCLHash The hash of the Last Closed Ledger (LCL).
        @param previousLedger Best guess of what the LCL was.
        @param closeTime Closing time point of the LCL.
        @param feeVote Our desired fee levels and voting logic.
    */
    LedgerConsensusImp (
        Application& app,
        ConsensusImp& consensus,
        int previousProposers,
        int previousConvergeTime,
        InboundTransactions& inboundTransactions,
        LocalTxs& localtx,
        LedgerMaster& ledgerMaster,
        LedgerHash const & prevLCLHash,
        Ledger::ref previousLedger,
        std::uint32_t closeTime,
        FeeVote& feeVote);

    /**
      Get the Json state of the consensus process.
      Called by the consensus_info RPC.

      @param full True if verbose response desired.
      @return     The Json state.
    */
    Json::Value getJson (bool full) override;

    /* The hash of the last closed ledger */
    uint256 getLCL () override;

    /**
      We have a complete transaction set, typically acquired from the network

      @param hash     hash of the transaction set.
      @param map      the transaction set.
      @param acquired true if we have acquired the transaction set.
    */
    void mapComplete (
        uint256 const& hash,
        std::shared_ptr<SHAMap> const& map,
        bool acquired) override;

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
    void handleLCL (uint256 const& lclHash);

    /**
      On timer call the correct handler for each state.
    */
    void timerEntry () override;

    /**
      Handle pre-close state.
    */
    void statePreClose ();

    /** We are establishing a consensus
       Update our position only on the timer, and in this state.
       If we have consensus, move to the finish state
    */
    void stateEstablish ();

    void stateFinished ();

    void stateAccepted ();

    /** Check if we've reached consensus */
    bool haveConsensus ();

    std::shared_ptr<SHAMap> getTransactionTree (uint256 const& hash);

    /**
      A server has taken a new position, adjust our tracking
      Called when a peer takes a new postion.

      @param newPosition the new position
      @return            true if we should do delayed relay of this position.
    */
    bool peerPosition (LedgerProposal::ref newPosition) override;

    void simulate () override;

private:
    /**
      We have a complete transaction set, typically acquired from the network

      @param hash     hash of the transaction set.
      @param map      the transaction set.
      @param acquired true if we have acquired the transaction set.
    */
    void mapCompleteInternal (
        uint256 const& hash,
        std::shared_ptr<SHAMap> const& map,
        bool acquired);

    /** We have a new last closed ledger, process it. Final accept logic

      @param set Our consensus set
    */
    void accept (std::shared_ptr<SHAMap> set);

    /**
      Compare two proposed transaction sets and create disputed
        transctions structures for any mismatches

      @param m1 One transaction set
      @param m2 The other transaction set
    */
    void createDisputes (std::shared_ptr<SHAMap> const& m1,
                         std::shared_ptr<SHAMap> const& m2);

    /**
      Add a disputed transaction (one that at least one node wants
      in the consensus set and at least one node does not) to our tracking

      @param txID The ID of the disputed transaction
      @param tx   The data of the disputed transaction
    */
    void addDisputedTransaction (uint256 const& txID, Blob const& tx);

    /**
      Adjust the votes on all disputed transactions based
        on the set of peers taking this position

      @param map   A disputed position
      @param peers peers which are taking the position map
    */
    void adjustCount (std::shared_ptr<SHAMap> const& map,
                      const std::vector<NodeID>& peers);

    /**
      Revoke our outstanding proposal, if any, and
      cease proposing at least until this round ends
    */
    void leaveConsensus ();

    /** Make and send a proposal
    */
    void propose ();

    /** Let peers know that we a particular transactions set so they
       can fetch it from us.

      @param hash   The ID of the transaction.
      @param direct true if we have this transaction set locally, else a
                    directly connected peer has it.
    */
    void sendHaveTxSet (uint256 const& hash, bool direct);

    /** Send a node status change message to our directly connected peers

      @param event   The event which caused the status change.  This is
                     typically neACCEPTED_LEDGER or neCLOSING_LEDGER.
      @param ledger  The ledger associated with the event.
    */
    void statusChange (protocol::NodeEvent event, Ledger& ledger);

    /** Take an initial position on what we think the consensus should be
        based on the transactions that made it into our open ledger

      @param initialLedger The ledger that contains our initial position.
    */
    void takeInitialPosition (std::shared_ptr<ReadView const> const& initialLedger);

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

    void endConsensus ();

    /** Add our load fee to our validation */
    void addLoad(STValidation::ref val);

private:
    Application& app_;
    ConsensusImp& consensus_;
    InboundTransactions& inboundTransactions_;
    LocalTxs& m_localTX;
    LedgerMaster& ledgerMaster_;
    FeeVote& m_feeVote;

    State state_;
    std::uint32_t mCloseTime;      // The wall time this ledger closed
    uint256 mPrevLedgerHash, mNewLedgerHash, mAcquiringLedger;
    Ledger::pointer mPreviousLedger;
    LedgerProposal::pointer mOurPosition;
    RippleAddress mValPublic, mValPrivate;
    bool mProposing, mValidating, mHaveCorrectLCL, mConsensusFail;

    int mCurrentMSeconds;

    // How long the close has taken, expressed as a percentage of the time that
    // we expected it to take.
    int mClosePercent;

    int mCloseResolution;

    bool mHaveCloseTimeConsensus;

    std::chrono::steady_clock::time_point   mConsensusStartTime;
    int                             mPreviousProposers;

    // The time it took for the last consensus process to converge
    int mPreviousMSeconds;

    // Convergence tracking, trusted peers indexed by hash of public key
    hash_map<NodeID, LedgerProposal::pointer>  mPeerPositions;

    // Transaction Sets, indexed by hash of transaction tree
    hash_map<uint256, std::shared_ptr<SHAMap>> mAcquired;

    // Disputed transactions
    hash_map<uint256, std::shared_ptr <DisputedTx>> mDisputes;
    hash_set<uint256> mCompares;

    // Close time estimates
    std::map<std::uint32_t, int> mCloseTimes;

    // nodes that have bowed out of this consensus process
    hash_set<NodeID> mDeadNodes;
    beast::Journal j_;
};

//------------------------------------------------------------------------------

std::shared_ptr <LedgerConsensus>
make_LedgerConsensus (Application& app, ConsensusImp& consensus, int previousProposers,
    int previousConvergeTime, InboundTransactions& inboundTransactions,
    LocalTxs& localtx, LedgerMaster& ledgerMaster,
    LedgerHash const &prevLCLHash, Ledger::ref previousLedger,
    std::uint32_t closeTime, FeeVote& feeVote);

} // ripple

#endif
