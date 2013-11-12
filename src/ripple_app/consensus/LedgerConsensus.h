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

#ifndef RIPPLE_LEDGERCONSENSUS_H
#define RIPPLE_LEDGERCONSENSUS_H

/** Manager for achieving consensus on the next ledger.

    This object is created when the consensus process starts, and
    is destroyed when the process is complete.
*/
class LedgerConsensus
    : public boost::enable_shared_from_this <LedgerConsensus>
    , public CountedObject <LedgerConsensus>
{
public:
    static char const* getCountedObjectName () { return "LedgerConsensus"; }

    LedgerConsensus (LedgerHash const & prevLCLHash, Ledger::ref previousLedger, uint32 closeTime);

    int startup ();

    Json::Value getJson (bool full);

    Ledger::ref peekPreviousLedger ()
    {
        return mPreviousLedger;
    }

    uint256 getLCL ()
    {
        return mPrevLedgerHash;
    }

    SHAMap::pointer getTransactionTree (uint256 const & hash, bool doAcquire);

    TransactionAcquire::pointer getAcquiring (uint256 const & hash);

    void mapComplete (uint256 const & hash, SHAMap::ref map, bool acquired);

    bool stillNeedTXSet (uint256 const & hash);

    void checkLCL ();

    void handleLCL (uint256 const & lclHash);

    void timerEntry ();

    // state handlers
    void statePreClose ();
    void stateEstablish ();
    void stateCutoff ();
    void stateFinished ();
    void stateAccepted ();

    bool haveConsensus (bool forReal);

    bool peerPosition (LedgerProposal::ref);

    bool peerHasSet (Peer::ref peer, uint256 const & set, protocol::TxSetStatus status);

    SHAMapAddNode peerGaveNodes (Peer::ref peer, uint256 const & setHash,
                                 const std::list<SHAMapNode>& nodeIDs, const std::list< Blob >& nodeData);

    bool isOurPubKey (const RippleAddress & k)
    {
        return k == mValPublic;
    }

    // test/debug
    void simulate ();

private:
    // final accept logic
    void accept (SHAMap::ref txSet, LoadEvent::pointer);

    void weHave (uint256 const & id, Peer::ref avoidPeer);
    void startAcquiring (TransactionAcquire::pointer);
    SHAMap::pointer find (uint256 const & hash);

    void createDisputes (SHAMap::ref, SHAMap::ref);
    void addDisputedTransaction (uint256 const& , Blob const & transaction);
    void adjustCount (SHAMap::ref map, const std::vector<uint160>& peers);
    void propose ();

    void addPosition (LedgerProposal&, bool ours);
    void removePosition (LedgerProposal&, bool ours);
    void sendHaveTxSet (uint256 const & set, bool direct);
    void applyTransactions (SHAMap::ref transactionSet, Ledger::ref targetLedger,
                            Ledger::ref checkLedger, CanonicalTXSet & failedTransactions, bool openLgr);
    int applyTransaction (TransactionEngine & engine, SerializedTransaction::ref txn, Ledger::ref targetLedger,
                          bool openLgr, bool retryAssured);

    uint32 roundCloseTime (uint32 closeTime);

    // manipulating our own position
    void statusChange (protocol::NodeEvent, Ledger & ledger);
    void takeInitialPosition (Ledger & initialLedger);
    void updateOurPositions ();
    void playbackProposals ();
    int getThreshold ();
    void closeLedger ();
    void checkOurValidation ();

    void beginAccept (bool synchronous);
    void endConsensus ();

    void addLoad (SerializedValidation::ref val);

private:
    // VFALCO TODO Rename these to look pretty
    enum LCState
    {
        lcsPRE_CLOSE,       // We haven't closed our ledger yet, but others might have
        lcsESTABLISH,       // Establishing consensus
        lcsFINISHED,        // We have closed on a transaction set
        lcsACCEPTED,        // We have accepted/validated a new last closed ledger
    };

    LCState mState;
    uint32 mCloseTime;                      // The wall time this ledger closed
    uint256 mPrevLedgerHash, mNewLedgerHash;
    Ledger::pointer mPreviousLedger;
    InboundLedger::pointer mAcquiringLedger;
    LedgerProposal::pointer mOurPosition;
    RippleAddress mValPublic, mValPrivate;
    bool mProposing, mValidating, mHaveCorrectLCL, mConsensusFail;

    int mCurrentMSeconds, mClosePercent, mCloseResolution;
    bool mHaveCloseTimeConsensus;

    boost::posix_time::ptime        mConsensusStartTime;
    int                             mPreviousProposers;
    int                             mPreviousMSeconds;

    // Convergence tracking, trusted peers indexed by hash of public key
    boost::unordered_map<uint160, LedgerProposal::pointer> mPeerPositions;

    // Transaction Sets, indexed by hash of transaction tree
    boost::unordered_map<uint256, SHAMap::pointer> mAcquired;
    boost::unordered_map<uint256, TransactionAcquire::pointer> mAcquiring;

    // Peer sets
    boost::unordered_map<uint256, std::vector< boost::weak_ptr<Peer> > > mPeerData;

    // Disputed transactions
    boost::unordered_map<uint256, DisputedTx::pointer> mDisputes;
    boost::unordered_set<uint256> mCompares;

    // Close time estimates
    std::map<uint32, int> mCloseTimes;

    // nodes that have bowed out of this consensus process
    boost::unordered_set<uint160> mDeadNodes;
};


#endif
