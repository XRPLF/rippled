//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LEDGERMASTER_H
#define RIPPLE_LEDGERMASTER_H

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

// VFALCO TODO Rename to Ledgers
//        It sounds like this holds all the ledgers...
//
class LedgerMaster : LeakChecked <LedgerMaster>
{
public:
    typedef FUNCTION_TYPE <void (Ledger::ref)> callback;

public:
    LedgerMaster ()
        : mHeldTransactions (uint256 ())
        , mMissingSeq (0)
        , mMinValidations (0)
        , mLastValidateSeq (0)
        , mPubThread (false)
        , mPathFindThread (false)
        , mPathFindNewLedger (false)
        , mPathFindNewRequest (false)
    {
    }

    ~LedgerMaster ()
    {
    }

    uint32 getCurrentLedgerIndex ();

    ScopedLock getLock ()
    {
        return ScopedLock (mLock);
    }

    // The current ledger is the ledger we believe new transactions should go in
    Ledger::ref getCurrentLedger ()
    {
        return mCurrentLedger;
    }

    // An immutable snapshot of the current ledger
    Ledger::ref getCurrentSnapshot ();

    // The finalized ledger is the last closed/accepted ledger
    Ledger::ref getClosedLedger ()
    {
        return mFinalizedLedger;
    }

    // The validated ledger is the last fully validated ledger
    Ledger::ref getValidatedLedger ()
    {
        return mValidLedger;
    }

    // This is the last ledger we published to clients and can lag the validated ledger
    Ledger::ref getPublishedLedger ()
    {
        return mPubLedger;
    }

    int getPublishedLedgerAge ();
    int getValidatedLedgerAge ();
    bool isCaughtUp(std::string& reason);

    TER doTransaction (SerializedTransaction::ref txn, TransactionEngineParams params, bool& didApply);

    int getMinValidations ()
    {
        return mMinValidations;
    }
    void setMinValidations (int v)
    {
        mMinValidations = v;
    }

    void pushLedger (Ledger::pointer newLedger);
    void pushLedger (Ledger::pointer newLCL, Ledger::pointer newOL, bool fromConsensus);
    void storeLedger (Ledger::pointer);

    void setFullLedger (Ledger::pointer ledger);

    void switchLedgers (Ledger::pointer lastClosed, Ledger::pointer newCurrent);

    std::string getCompleteLedgers ()
    {
        boost::recursive_mutex::scoped_lock sl (mLock);
        return mCompleteLedgers.toString ();
    }

    Ledger::pointer closeLedger (bool recoverHeldTransactions);

    uint256 getHashBySeq (uint32 index)
    {
        uint256 hash = mLedgerHistory.getLedgerHash (index);

        if (hash.isNonZero ())
            return hash;

        return Ledger::getHashByIndex (index);
    }

    Ledger::pointer getLedgerBySeq (uint32 index)
    {
        if (mCurrentLedger && (mCurrentLedger->getLedgerSeq () == index))
            return mCurrentLedger;

        if (mFinalizedLedger && (mFinalizedLedger->getLedgerSeq () == index))
            return mFinalizedLedger;

        Ledger::pointer ret = mLedgerHistory.getLedgerBySeq (index);

        if (ret)
            return ret;

        boost::recursive_mutex::scoped_lock ml (mLock);
        mCompleteLedgers.clearValue (index);
        return ret;
    }

    Ledger::pointer getLedgerByHash (uint256 const& hash)
    {
        if (hash.isZero ())
            return boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);

        if (mCurrentLedger && (mCurrentLedger->getHash () == hash))
            return boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);

        if (mFinalizedLedger && (mFinalizedLedger->getHash () == hash))
            return mFinalizedLedger;

        return mLedgerHistory.getLedgerByHash (hash);
    }

    void setLedgerRangePresent (uint32 minV, uint32 maxV)
    {
        boost::recursive_mutex::scoped_lock sl (mLock);
        mCompleteLedgers.setRange (minV, maxV);
    }

    void addHeldTransaction (Transaction::ref trans);
    void fixMismatch (Ledger::ref ledger);

    bool haveLedgerRange (uint32 from, uint32 to);
    bool haveLedger (uint32 seq);
    bool getValidatedRange (uint32& minVal, uint32& maxVal);

    void resumeAcquiring ();

    void tune (int size, int age)
    {
        mLedgerHistory.tune (size, age);
    }
    void sweep ()
    {
        mLedgerHistory.sweep ();
    }
    float getCacheHitRate ()
    {
        return mLedgerHistory.getCacheHitRate ();
    }

    void addValidateCallback (callback& c)
    {
        mOnValidate.push_back (c);
    }

    void checkAccept (uint256 const& hash);
    void checkAccept (uint256 const& hash, uint32 seq);
    void tryPublish ();
    void newPathRequest ();

    static bool shouldAcquire (uint32 currentLedgerID, uint32 ledgerHistory, uint32 targetLedger);

private:
    void applyFutureTransactions (uint32 ledgerIndex);
    bool isValidTransaction (Transaction::ref trans);
    bool isTransactionOnFutureList (Transaction::ref trans);

    bool acquireMissingLedger (Ledger::ref from, uint256 const& ledgerHash, uint32 ledgerSeq);
    void asyncAccept (Ledger::pointer);
    void missingAcquireComplete (InboundLedger::pointer);
    void pubThread ();
    void updatePaths ();

private:
    boost::recursive_mutex mLock;

    TransactionEngine mEngine;

    Ledger::pointer mCurrentLedger;     // The ledger we are currently processiong
    Ledger::pointer mCurrentSnapshot;   // Snapshot of the current ledger
    Ledger::pointer mFinalizedLedger;   // The ledger that most recently closed
    Ledger::pointer mValidLedger;       // The highest-sequence ledger we have fully accepted
    Ledger::pointer mPubLedger;         // The last ledger we have published

    LedgerHistory mLedgerHistory;

    CanonicalTXSet mHeldTransactions;

    RangeSet mCompleteLedgers;
    InboundLedger::pointer mMissingLedger;
    uint32 mMissingSeq;

    int                         mMinValidations;    // The minimum validations to publish a ledger
    uint256                     mLastValidateHash;
    uint32                      mLastValidateSeq;
    std::list<callback>         mOnValidate;        // Called when a ledger has enough validations

    std::list<Ledger::pointer>  mPubLedgers;        // List of ledgers to publish
    bool                        mPubThread;         // Publish thread is running

    bool                        mPathFindThread;    // Pathfind thread is running
    bool                        mPathFindNewLedger;
    bool                        mPathFindNewRequest;
};

#endif
// vim:ts=4
