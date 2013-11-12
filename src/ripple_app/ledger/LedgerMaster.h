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

#ifndef RIPPLE_LEDGERMASTER_H
#define RIPPLE_LEDGERMASTER_H

// Tracks the current ledger and any ledgers in the process of closing
// Tracks ledger history
// Tracks held transactions

// VFALCO TODO Rename to Ledgers
//        It sounds like this holds all the ledgers...
//
class LedgerMaster
    : public Stoppable
    , public LeakChecked <LedgerMaster>
{
public:
    typedef FUNCTION_TYPE <void (Ledger::ref)> callback;

public:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    typedef LockType::ScopedUnlockType ScopedUnlockType;

    explicit LedgerMaster (Stoppable& parent)
        : Stoppable ("LedgerMaster", parent)
        , mLock (this, "LedgerMaster", __FILE__, __LINE__)
        , mPubLedgerClose (0)
        , mPubLedgerSeq (0)
        , mValidLedgerClose (0)
        , mValidLedgerSeq (0)
        , mHeldTransactions (uint256 ())
        , mMinValidations (0)
        , mLastValidateSeq (0)
        , mAdvanceThread (false)
        , mAdvanceWork (false)
        , mFillInProgress (0)
        , mPathFindThread (false)
        , mPathFindNewRequest (false)
    {
    }

    ~LedgerMaster ()
    {
    }

    uint32 getCurrentLedgerIndex ();

    LockType& peekMutex ()
    {
        return mLock;
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
        return mClosedLedger;
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
    void pushLedger (Ledger::pointer newLCL, Ledger::pointer newOL);
    void storeLedger (Ledger::pointer);
    void forceValid (Ledger::pointer);

    void setFullLedger (Ledger::pointer ledger, bool isSynchronous, bool isCurrent);

    void switchLedgers (Ledger::pointer lastClosed, Ledger::pointer newCurrent);

    void failedSave(uint32 seq, uint256 const& hash);

    std::string getCompleteLedgers ()
    {
        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
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
        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);
            if (mCurrentLedger && (mCurrentLedger->getLedgerSeq () == index))
                return mCurrentLedger;

                if (mClosedLedger && (mClosedLedger->getLedgerSeq () == index))
                return mClosedLedger;
        }

        Ledger::pointer ret = mLedgerHistory.getLedgerBySeq (index);

        if (ret)
            return ret;

        clearLedger (index);
        return ret;
    }

    Ledger::pointer getLedgerByHash (uint256 const& hash)
    {
        if (hash.isZero ())
            return boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);

        if (mCurrentLedger && (mCurrentLedger->getHash () == hash))
            return boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);

        if (mClosedLedger && (mClosedLedger->getHash () == hash))
            return mClosedLedger;

        return mLedgerHistory.getLedgerByHash (hash);
    }

    void setLedgerRangePresent (uint32 minV, uint32 maxV)
    {
        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
        mCompleteLedgers.setRange (minV, maxV);
    }

    uint256 getLedgerHash(uint32 desiredSeq, Ledger::ref knownGoodLedger);

    void addHeldTransaction (Transaction::ref trans);
    void fixMismatch (Ledger::ref ledger);

    bool haveLedgerRange (uint32 from, uint32 to);
    bool haveLedger (uint32 seq);
    void clearLedger (uint32 seq);
    bool getValidatedRange (uint32& minVal, uint32& maxVal);
    bool getFullValidatedRange (uint32& minVal, uint32& maxVal);

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

    void checkAccept (Ledger::ref ledger);
    void checkAccept (uint256 const& hash);

    void tryAdvance ();
    void newPathRequest ();

    static bool shouldAcquire (uint32 currentLedgerID, uint32 ledgerHistory, uint32 targetLedger);

private:
    std::list<Ledger::pointer> findNewLedgersToPublish ();

    void applyFutureTransactions (uint32 ledgerIndex);
    bool isValidTransaction (Transaction::ref trans);
    bool isTransactionOnFutureList (Transaction::ref trans);

    void getFetchPack (Ledger::ref have);
    void tryFill (Job&, Ledger::pointer);
    void advanceThread ();
    void doAdvance ();
    void updatePaths (Job&);

    void setValidLedger(Ledger::ref);
    void setPubLedger(Ledger::ref);

private:
    LockType mLock;

    TransactionEngine mEngine;

    Ledger::pointer mCurrentLedger;     // The ledger we are currently processiong
    Ledger::pointer mCurrentSnapshot;   // Snapshot of the current ledger
    Ledger::pointer mClosedLedger;      // The ledger that most recently closed
    Ledger::pointer mValidLedger;       // The highest-sequence ledger we have fully accepted
    Ledger::pointer mPubLedger;         // The last ledger we have published
    Ledger::pointer mPathLedger;        // The last ledger we did pathfinding against

    beast::Atomic<uint32> mPubLedgerClose;
    beast::Atomic<uint32> mPubLedgerSeq;
    beast::Atomic<uint32> mValidLedgerClose;
    beast::Atomic<uint32> mValidLedgerSeq;

    LedgerHistory mLedgerHistory;

    CanonicalTXSet mHeldTransactions;

    LockType mCompleteLock;
    RangeSet mCompleteLedgers;

    int                         mMinValidations;    // The minimum validations to publish a ledger
    uint256                     mLastValidateHash;
    uint32                      mLastValidateSeq;
    std::list<callback>         mOnValidate;        // Called when a ledger has enough validations

    std::list<Ledger::pointer>  mPubLedgers;        // List of ledgers to publish
    bool                        mAdvanceThread;     // Publish thread is running
    bool                        mAdvanceWork;       // Publish thread has work to do
    int                         mFillInProgress;

    bool                        mPathFindThread;    // Pathfind thread is running
    bool                        mPathFindNewLedger;
    bool                        mPathFindNewRequest;
};

#endif
// vim:ts=4
