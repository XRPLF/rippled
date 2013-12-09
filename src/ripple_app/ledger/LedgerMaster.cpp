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

#define MIN_VALIDATION_RATIO    150     // 150/256ths of validations of previous ledger
#define MAX_LEDGER_GAP          100     // Don't catch up more than 100 ledgers  (cannot exceed 256)

SETUP_LOG (LedgerMaster)

class LedgerCleanerLog;
template <> char const* LogPartition::getPartitionName <LedgerCleanerLog> () { return "LedgerCleaner"; }

class LedgerMasterImp
    : public LedgerMaster
    , public LeakChecked <LedgerMasterImp>
{
public:
    typedef FUNCTION_TYPE <void (Ledger::ref)> callback;

    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;
    typedef LockType::ScopedUnlockType ScopedUnlockType;

    Journal m_journal;

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

    ScopedPointer<LedgerCleaner> mLedgerCleaner;

    int                         mMinValidations;    // The minimum validations to publish a ledger
    uint256                     mLastValidateHash;
    uint32                      mLastValidateSeq;
    std::list<callback>         mOnValidate;        // Called when a ledger has enough validations

    std::list<Ledger::pointer>  mPubLedgers;        // List of ledgers to publish
    bool                        mAdvanceThread;     // Publish thread is running
    bool                        mAdvanceWork;       // Publish thread has work to do
    int                         mFillInProgress;

    int                         mPathFindThread;    // Pathfinder jobs dispatched
    bool                        mPathFindNewLedger;
    bool                        mPathFindNewRequest;

    //--------------------------------------------------------------------------

    explicit LedgerMasterImp (Stoppable& parent, Journal journal)
        : LedgerMaster (parent)
        , m_journal (journal)
        , mLock (this, "LedgerMaster", __FILE__, __LINE__)
        , mPubLedgerClose (0)
        , mPubLedgerSeq (0)
        , mValidLedgerClose (0)
        , mValidLedgerSeq (0)
        , mHeldTransactions (uint256 ())
        , mLedgerCleaner (LedgerCleaner::New(*this, LogPartition::getJournal<LedgerCleanerLog>()))
        , mMinValidations (0)
        , mLastValidateSeq (0)
        , mAdvanceThread (false)
        , mAdvanceWork (false)
        , mFillInProgress (0)
        , mPathFindThread (0)
        , mPathFindNewRequest (false)
    {
    }

    ~LedgerMasterImp ()
    {
    }

    uint32 getCurrentLedgerIndex ()
    {
        return mCurrentLedger->getLedgerSeq ();
    }

    // An immutable snapshot of the current ledger
    Ledger::ref getCurrentSnapshot ()
    {
        if (!mCurrentSnapshot || (mCurrentSnapshot->getHash () != mCurrentLedger->getHash ()))
            mCurrentSnapshot = boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);

        assert (mCurrentSnapshot->isImmutable ());
        return mCurrentSnapshot;
    }

    int getPublishedLedgerAge ()
    {
        uint32 pubClose = mPubLedgerClose.get();
        if (!pubClose)
        {
            WriteLog (lsDEBUG, LedgerMaster) << "No published ledger";
            return 999999;
        }

        int64 ret = getApp().getOPs ().getCloseTimeNC ();
        ret -= static_cast<int64> (pubClose);
        ret = std::max (0LL, ret);

        WriteLog (lsTRACE, LedgerMaster) << "Published ledger age is " << ret;
        return static_cast<int> (ret);
    }

    int getValidatedLedgerAge ()
    {
        uint32 valClose = mValidLedgerClose.get();
        if (!valClose)
        {
            WriteLog (lsDEBUG, LedgerMaster) << "No validated ledger";
            return 999999;
        }

        int64 ret = getApp().getOPs ().getCloseTimeNC ();
        ret -= static_cast<int64> (valClose);
        ret = std::max (0LL, ret);

        WriteLog (lsTRACE, LedgerMaster) << "Validated ledger age is " << ret;
        return static_cast<int> (ret);
    }

    bool isCaughtUp(std::string& reason)
    {
        if (getPublishedLedgerAge() > 180)
        {
            reason = "No recently-published ledger";
            return false;
        }
        uint32 validClose = mValidLedgerClose.get();
        uint32 pubClose = mPubLedgerClose.get();
        if (!validClose || !pubClose)
        {
            reason = "No published ledger";
            return false;
        }
        if (validClose  > (pubClose + 90))
        {
            reason = "Published ledger lags validated ledger";
            return false;
        }
        return true;
    }

    void setValidLedger(Ledger::ref l)
    {
        mValidLedger =  l;
        mValidLedgerClose = l->getCloseTimeNC();
        mValidLedgerSeq = l->getLedgerSeq();
    }

    void setPubLedger(Ledger::ref l)
    {
        mPubLedger = l;
        mPubLedgerClose = l->getCloseTimeNC();
        mPubLedgerSeq = l->getLedgerSeq();
    }

    void addHeldTransaction (Transaction::ref transaction)
    {
        // returns true if transaction was added
        ScopedLockType ml (mLock, __FILE__, __LINE__);
        mHeldTransactions.push_back (transaction->getSTransaction ());
    }

    void pushLedger (Ledger::pointer newLedger)
    {
        // Caller should already have properly assembled this ledger into "ready-to-close" form --
        // all candidate transactions must already be applied
        WriteLog (lsINFO, LedgerMaster) << "PushLedger: " << newLedger->getHash ();

        {
            ScopedLockType ml (mLock, __FILE__, __LINE__);

            if (mClosedLedger)
            {
                mClosedLedger->setClosed ();
                WriteLog (lsTRACE, LedgerMaster) << "Finalizes: " << mClosedLedger->getHash ();
            }

            mClosedLedger = mCurrentLedger;
            mCurrentLedger = newLedger;
            mEngine.setLedger (newLedger);
        }

        if (getConfig().RUN_STANDALONE)
        {
            setFullLedger(newLedger, true, false);
            tryAdvance();
        }
        else
            checkAccept(newLedger);
    }

    void pushLedger (Ledger::pointer newLCL, Ledger::pointer newOL)
    {
        assert (newLCL->isClosed () && newLCL->isAccepted ());
        assert (!newOL->isClosed () && !newOL->isAccepted ());


        {
            ScopedLockType ml (mLock, __FILE__, __LINE__);
            mClosedLedger = newLCL;
            mCurrentLedger = newOL;
            mEngine.setLedger (newOL);
        }

        if (getConfig().RUN_STANDALONE)
        {
            setFullLedger(newLCL, true, false);
            tryAdvance();
        }
        else
        {
            mLedgerHistory.builtLedger (newLCL);
            checkAccept (newLCL);
        }
    }

    void switchLedgers (Ledger::pointer lastClosed, Ledger::pointer current)
    {
        assert (lastClosed && current);

        {
            ScopedLockType ml (mLock, __FILE__, __LINE__);
            mClosedLedger = lastClosed;
            mClosedLedger->setClosed ();
            mClosedLedger->setAccepted ();
            mCurrentLedger = current;

            assert (!mCurrentLedger->isClosed ());
            mEngine.setLedger (mCurrentLedger);
        }
        checkAccept (lastClosed);
    }

    bool fixIndex (LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
    {
        return mLedgerHistory.fixIndex (ledgerIndex, ledgerHash);
    }

    void storeLedger (Ledger::pointer ledger)
    {
        mLedgerHistory.addLedger (ledger, false);
    }

    void forceValid (Ledger::pointer ledger)
    {
        ledger->setValidated();
        setFullLedger(ledger, true, false);
    }

    Ledger::pointer closeLedger (bool recover)
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        Ledger::pointer closingLedger = mCurrentLedger;

        if (recover)
        {
            int recovers = 0;

            for (CanonicalTXSet::iterator it = mHeldTransactions.begin (), end = mHeldTransactions.end (); it != end; ++it)
            {
                try
                {
                    TransactionEngineParams tepFlags = tapOPEN_LEDGER;

                    if (getApp().getHashRouter ().addSuppressionPeer (it->first.getTXID (), SF_SIGGOOD))
                        tepFlags = static_cast<TransactionEngineParams> (tepFlags | tapNO_CHECK_SIGN);

                    bool didApply;
                    mEngine.applyTransaction (*it->second, tepFlags, didApply);

                    if (didApply)
                        ++recovers;
                }
                catch (...)
                {
                    // CHECKME: We got a few of these
                    WriteLog (lsWARNING, LedgerMaster) << "Held transaction throws";
                }
            }

            CondLog (recovers != 0, lsINFO, LedgerMaster) << "Recovered " << recovers << " held transactions";

            // VFALCO TODO recreate the CanonicalTxSet object instead of resetting it
            mHeldTransactions.reset (closingLedger->getHash ());
        }

        mCurrentLedger = boost::make_shared<Ledger> (boost::ref (*closingLedger), true);
        mEngine.setLedger (mCurrentLedger);

        return Ledger::pointer (new Ledger (*closingLedger, true));
    }

    TER doTransaction (SerializedTransaction::ref txn, TransactionEngineParams params, bool& didApply)
    {
        Ledger::pointer ledger;
        TER result;

        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);
            result = mEngine.applyTransaction (*txn, params, didApply);
            ledger = mEngine.getLedger ();
        }
        if (didApply)
           getApp().getOPs ().pubProposedTransaction (ledger, txn, result);
        return result;
    }

    bool haveLedgerRange (uint32 from, uint32 to)
    {
        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
        uint32 prevMissing = mCompleteLedgers.prevMissing (to + 1);
        return (prevMissing == RangeSet::absent) || (prevMissing < from);
    }

    bool haveLedger (uint32 seq)
    {
        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
        return mCompleteLedgers.hasValue (seq);
    }

    void clearLedger (uint32 seq)
    {
        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
        return mCompleteLedgers.clearValue (seq);
    }

    // returns Ledgers we have all the nodes for
    bool getFullValidatedRange (uint32& minVal, uint32& maxVal)
    {
        maxVal = mPubLedgerSeq.get();

        if (!maxVal)
            return false;

        {
            ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
            minVal = mCompleteLedgers.prevMissing (maxVal);
        }

        if (minVal == RangeSet::absent)
            minVal = maxVal;
        else
            ++minVal;

        return true;
    }

    // Returns Ledgers we have all the nodes for and are indexed
    bool getValidatedRange (uint32& minVal, uint32& maxVal)
    { 
        maxVal = mPubLedgerSeq.get();

        if (!maxVal)
            return false;

        {
            ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
            minVal = mCompleteLedgers.prevMissing (maxVal);
        }

        if (minVal == RangeSet::absent)
            minVal = maxVal;
        else
            ++minVal;

        // Remove from the validated range any ledger sequences that may not be
        // fully updated in the database yet

        std::set<uint32> sPendingSaves = Ledger::getPendingSaves();

        if (!sPendingSaves.empty() && ((minVal != 0) || (maxVal != 0)))
        {
            // Ensure we shrink the tips as much as possible
            // If we have 7-9 and 8,9 are invalid, we don't want to see the 8 and shrink to just 9
            // because then we'll have nothing when we could have 7.
            while (sPendingSaves.count(maxVal) > 0)
                --maxVal;
            while (sPendingSaves.count(minVal) > 0)
                ++minVal;

            // Best effort for remaining exclusions
            BOOST_FOREACH(uint32 v, sPendingSaves)
            {
                if ((v >= minVal) && (v <= maxVal))
                {
                    if (v > ((minVal + maxVal) / 2))
                        maxVal = v - 1;
                    else
                        minVal = v + 1;
                }
            }

            if (minVal > maxVal)
                minVal = maxVal = 0;
        }

        return true;
    }

    void tryFill (Job& job, Ledger::pointer ledger)
    {
        uint32 seq = ledger->getLedgerSeq ();
        uint256 prevHash = ledger->getParentHash ();

        std::map< uint32, std::pair<uint256, uint256> > ledgerHashes;

        uint32 minHas = ledger->getLedgerSeq ();
        uint32 maxHas = ledger->getLedgerSeq ();

        while (! job.shouldCancel() && seq > 0)
        {
            {
                ScopedLockType ml (mLock, __FILE__, __LINE__);
                minHas = seq;
                --seq;

                if (haveLedger (seq))
                    break;
            }

            std::map< uint32, std::pair<uint256, uint256> >::iterator it = ledgerHashes.find (seq);

            if (it == ledgerHashes.end ())
            {
                if (getApp().isShutdown ())
                    return;

                {
                    ScopedLockType ml (mCompleteLock, __FILE__, __LINE__);
                    mCompleteLedgers.setRange (minHas, maxHas);
                }
                maxHas = minHas;
                ledgerHashes = Ledger::getHashesByIndex ((seq < 500) ? 0 : (seq - 499), seq);
                it = ledgerHashes.find (seq);

                if (it == ledgerHashes.end ())
                    break;
            }

            if (it->second.first != prevHash)
                break;

            prevHash = it->second.second;
        }

        {
            ScopedLockType ml (mCompleteLock, __FILE__, __LINE__);
            mCompleteLedgers.setRange (minHas, maxHas);
        }
        {
            ScopedLockType ml (mLock, __FILE__, __LINE__);
            mFillInProgress = 0;
            tryAdvance();
        }
    }

    /** Request a fetch pack to get the ledger prior to 'nextLedger'
    */
    void getFetchPack (Ledger::ref nextLedger)
    {
        Peer::pointer target;
        int count = 0;

        std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();
        BOOST_FOREACH (const Peer::pointer & peer, peerList)
        {
            if (peer->hasRange (nextLedger->getLedgerSeq() - 1, nextLedger->getLedgerSeq()))
            {
                if (count++ == 0)
                    target = peer;
                else if ((rand () % ++count) == 0)
                    target = peer;
            }
        }

        if (target)
        {
            protocol::TMGetObjectByHash tmBH;
            tmBH.set_query (true);
            tmBH.set_type (protocol::TMGetObjectByHash::otFETCH_PACK);
            tmBH.set_ledgerhash (nextLedger->getHash().begin (), 32);
            PackedMessage::pointer packet = boost::make_shared<PackedMessage> (tmBH, protocol::mtGET_OBJECTS);

            target->sendPacket (packet, false);
            WriteLog (lsTRACE, LedgerMaster) << "Requested fetch pack for " << nextLedger->getLedgerSeq() - 1;
        }
        else
            WriteLog (lsDEBUG, LedgerMaster) << "No peer for fetch pack";
    }

    void fixMismatch (Ledger::ref ledger)
    {
        int invalidate = 0;
        uint256 hash;

        for (uint32 lSeq = ledger->getLedgerSeq () - 1; lSeq > 0; --lSeq)
            if (haveLedger (lSeq))
            {
                try
                {
                    hash = ledger->getLedgerHash (lSeq);
	        }
	        catch (...)
	        {
	            WriteLog (lsWARNING, LedgerMaster) << "fixMismatch encounters partial ledger";
	            clearLedger(lSeq);
	            return;
	        }

                if (hash.isNonZero ())
                {
                    // try to close the seam
                    Ledger::pointer otherLedger = getLedgerBySeq (lSeq);

                    if (otherLedger && (otherLedger->getHash () == hash))
                    {
                        // we closed the seam
                        CondLog (invalidate != 0, lsWARNING, LedgerMaster) << "Match at " << lSeq << ", " <<
                                invalidate << " prior ledgers invalidated";
                        return;
                    }
                }

                clearLedger (lSeq);
                ++invalidate;
            }

        // all prior ledgers invalidated
        CondLog (invalidate != 0, lsWARNING, LedgerMaster) << "All " << invalidate << " prior ledgers invalidated";
    }

    void setFullLedger (Ledger::pointer ledger, bool isSynchronous, bool isCurrent)
    {
        // A new ledger has been accepted as part of the trusted chain
        WriteLog (lsDEBUG, LedgerMaster) << "Ledger " << ledger->getLedgerSeq () << " accepted :" << ledger->getHash ();
        assert (ledger->peekAccountStateMap ()->getHash ().isNonZero ());

        ledger->setValidated();
        mLedgerHistory.addLedger(ledger, true);
        ledger->setFull();
        ledger->pendSaveValidated (isSynchronous, isCurrent);

        {

            {
                ScopedLockType ml (mCompleteLock, __FILE__, __LINE__);
                mCompleteLedgers.setValue (ledger->getLedgerSeq ());
            }

            ScopedLockType ml (mLock, __FILE__, __LINE__);

            if (!mValidLedger || (ledger->getLedgerSeq() > mValidLedger->getLedgerSeq()))
                setValidLedger(ledger);
            if (!mPubLedger)
            {
                setPubLedger(ledger);
                getApp().getOrderBookDB().setup(ledger);
            }

            if ((ledger->getLedgerSeq () != 0) && haveLedger (ledger->getLedgerSeq () - 1))
            {
                // we think we have the previous ledger, double check
                Ledger::pointer prevLedger = getLedgerBySeq (ledger->getLedgerSeq () - 1);

                if (!prevLedger || (prevLedger->getHash () != ledger->getParentHash ()))
                {
                    WriteLog (lsWARNING, LedgerMaster) << "Acquired ledger invalidates previous ledger: " <<
                                                       (prevLedger ? "hashMismatch" : "missingLedger");
                    fixMismatch (ledger);
                }
            }
        }

        //--------------------------------------------------------------------------
        //
        {
            if (isCurrent)
                getApp ().getValidators ().ledgerClosed (ledger->getHash());
        }
        //
        //--------------------------------------------------------------------------
    }

    void failedSave(uint32 seq, uint256 const& hash)
    {
        clearLedger(seq);
        getApp().getInboundLedgers().findCreate(hash, seq, true);
    }

    void checkAccept (uint256 const& hash)
    {
        Ledger::pointer ledger = mLedgerHistory.getLedgerByHash (hash);

        if (!ledger)
        {
            InboundLedger::pointer l = getApp().getInboundLedgers().findCreate(hash, 0, false);
            if (l && l->isComplete() && !l->isFailed())
                ledger = l->getLedger();
            else
            {
                WriteLog (lsDEBUG, LedgerMaster) << "checkAccept triggers acquire " << hash.GetHex();
            }
        }

        if (ledger)
            checkAccept (ledger);
    }

    void checkAccept (Ledger::ref ledger)
    {
        if (ledger->getLedgerSeq() <= mValidLedgerSeq.get())
            return;

        // Can we advance the last fully-validated ledger? If so, can we publish?
        ScopedLockType ml (mLock, __FILE__, __LINE__);

        if (mValidLedger && (ledger->getLedgerSeq() <= mValidLedger->getLedgerSeq ()))
            return;

        int minVal = mMinValidations;

        if (mLastValidateHash.isNonZero ())
        {
            int val = getApp().getValidations ().getTrustedValidationCount (mLastValidateHash);
            val *= MIN_VALIDATION_RATIO;
            val /= 256;

            if (val > minVal)
                minVal = val;
        }

        if (getConfig ().RUN_STANDALONE)
            minVal = 0;

        int tvc = getApp().getValidations().getTrustedValidationCount(ledger->getHash());
        if (tvc < minVal) // nothing we can do
        {
            WriteLog (lsTRACE, LedgerMaster) << "Only " << tvc << " validations for " << ledger->getHash();
            return;
        }

        WriteLog (lsINFO, LedgerMaster) << "Advancing accepted ledger to " << ledger->getLedgerSeq() << " with >= " << minVal << " validations";

        mLastValidateHash = ledger->getHash();
        mLastValidateSeq = ledger->getLedgerSeq();

        ledger->setValidated();
        ledger->setFull();
        setValidLedger(ledger);
        if (!mPubLedger)
        {
            ledger->pendSaveValidated(true, true);
            setPubLedger(ledger);
            getApp().getOrderBookDB().setup(ledger);
        }

        uint64 fee, fee2, ref;
        ref = getApp().getFeeTrack().getLoadBase();
        int count = getApp().getValidations().getFeeAverage(ledger->getHash(), ref, fee);
        int count2 = getApp().getValidations().getFeeAverage(ledger->getParentHash(), ref, fee2);

        if ((count + count2) == 0)
            getApp().getFeeTrack().setRemoteFee(ref);
        else
            getApp().getFeeTrack().setRemoteFee(((fee * count) + (fee2 * count2)) / (count + count2));

        tryAdvance ();
    }

    void advanceThread()
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        assert (mValidLedger && mAdvanceThread);

        WriteLog (lsTRACE, LedgerMaster) << "advanceThread<";

        try
        {
            doAdvance();
        }
        catch (...)
        {
            WriteLog (lsFATAL, LedgerMaster) << "doAdvance throws an exception";
        }

        mAdvanceThread = false;
        WriteLog (lsTRACE, LedgerMaster) << "advanceThread>";
    }

    // Try to publish ledgers, acquire missing ledgers
    void doAdvance ()
    {
        do
        {
            mAdvanceWork = false; // If there's work to do, we'll make progress
            bool progress = false;

            std::list<Ledger::pointer> pubLedgers = findNewLedgersToPublish ();
            if (pubLedgers.empty())
            {
                if (!getConfig().RUN_STANDALONE && !getApp().getFeeTrack().isLoadedLocal() &&
                    (getApp().getJobQueue().getJobCount(jtPUBOLDLEDGER) < 10) &&
                    (mValidLedger->getLedgerSeq() == mPubLedger->getLedgerSeq()))
                { // We are in sync, so can acquire
                    uint32 missing;
                    {
                        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
                        missing = mCompleteLedgers.prevMissing(mPubLedger->getLedgerSeq());
                    }
                    WriteLog (lsTRACE, LedgerMaster) << "tryAdvance discovered missing " << missing;
                    if ((missing != RangeSet::absent) && (missing > 0) &&
                        shouldAcquire(mValidLedger->getLedgerSeq(), getConfig().LEDGER_HISTORY, missing) &&
                        ((mFillInProgress == 0) || (missing > mFillInProgress)))
                    {
                        WriteLog (lsTRACE, LedgerMaster) << "advanceThread should acquire";
                        {
                            ScopedUnlockType sl(mLock, __FILE__, __LINE__);
                            Ledger::pointer nextLedger = mLedgerHistory.getLedgerBySeq(missing + 1);
                            if (nextLedger)
                            {
                                assert (nextLedger->getLedgerSeq() == (missing + 1));
                                Ledger::pointer ledger = getLedgerByHash(nextLedger->getParentHash());
                                if (!ledger)
                                {
                                    if (!getApp().getInboundLedgers().isFailure(nextLedger->getParentHash()))
                                    {
                                        InboundLedger::pointer acq =
                                            getApp().getInboundLedgers().findCreate(nextLedger->getParentHash(),
                                                                                    nextLedger->getLedgerSeq() - 1, false);
                                        if (acq->isComplete() && !acq->isFailed())
                                            ledger = acq->getLedger();
                                        else if ((missing > 40000) && getApp().getOPs().shouldFetchPack(missing))
                                        {
                                            WriteLog (lsTRACE, LedgerMaster) << "tryAdvance want fetch pack " << missing;
                                            getFetchPack(nextLedger);
                                        }
                                        else
                                            WriteLog (lsTRACE, LedgerMaster) << "tryAdvance no fetch pack for " << missing;
                                    }
                                    else
                                        WriteLog (lsDEBUG, LedgerMaster) << "tryAdvance found failed acquire";
                                }
                                if (ledger)
                                {
                                    assert(ledger->getLedgerSeq() == missing);
                                    WriteLog (lsTRACE, LedgerMaster) << "tryAdvance acquired " << ledger->getLedgerSeq();
                                    setFullLedger(ledger, false, false);
                                    if ((mFillInProgress == 0) && (Ledger::getHashByIndex(ledger->getLedgerSeq() - 1) == ledger->getParentHash()))
                                    { // Previous ledger is in DB
                                        ScopedLockType sl(mLock, __FILE__, __LINE__);
                                        mFillInProgress = ledger->getLedgerSeq();
                                        getApp().getJobQueue().addJob(jtADVANCE, "tryFill", BIND_TYPE (
                                            &LedgerMasterImp::tryFill, this, P_1, ledger));
                                    }
                                    progress = true;
                                }
                                else
                                {
                                    try
                                    {
                                        for (int i = 0; i < getConfig().getSize(siLedgerFetch); ++i)
                                        {
                                            uint32 seq = missing - i;
                                            uint256 hash = nextLedger->getLedgerHash(seq);
                                            if (hash.isNonZero())
                                                getApp().getInboundLedgers().findCreate(hash, seq, false);
                                        }
                                    }
                                    catch (...)
                                    {
                                        WriteLog (lsWARNING, LedgerMaster) << "Threw while prefecthing";
                                    }
                                }
                            }
                            else
                            {
                                WriteLog (lsFATAL, LedgerMaster) << "Unable to find ledger following prevMissing " << missing;
                                WriteLog (lsFATAL, LedgerMaster) << "Pub:" << mPubLedger->getLedgerSeq() << " Val:" << mValidLedger->getLedgerSeq();
                                WriteLog (lsFATAL, LedgerMaster) << "Ledgers: " << getApp().getLedgerMaster().getCompleteLedgers();
                                clearLedger (missing + 1);
                                progress = true;
                            }
                        }
                        if (mValidLedger->getLedgerSeq() != mPubLedger->getLedgerSeq())
                        {
                            WriteLog (lsDEBUG, LedgerMaster) << "tryAdvance found last valid changed";
                            progress = true;
                        }
                    }
                }
                else
                    WriteLog (lsTRACE, LedgerMaster) << "tryAdvance not fetching history";
            }
            else
            {
                WriteLog (lsTRACE, LedgerMaster) << "tryAdvance found " << pubLedgers.size() << " ledgers to publish";
                BOOST_FOREACH(Ledger::ref ledger, pubLedgers)
                {
                    {
                        ScopedUnlockType sul (mLock, __FILE__, __LINE__);
                        WriteLog(lsDEBUG, LedgerMaster) << "tryAdvance publishing seq " << ledger->getLedgerSeq();

                        setFullLedger(ledger, true, true);
                        getApp().getOPs().pubLedger(ledger);
                    }

                    setPubLedger(ledger);
                    progress = true;
                }

                getApp().getOPs().clearNeedNetworkLedger();
                newPFWork ("pf:newLedger");
            }
            if (progress)
                mAdvanceWork = true;
        } while (mAdvanceWork);
    }

    std::list<Ledger::pointer> findNewLedgersToPublish ()
    {
        std::list<Ledger::pointer> ret;

        WriteLog (lsTRACE, LedgerMaster) << "findNewLedgersToPublish<";
        if (!mPubLedger)
        {
            WriteLog (lsINFO, LedgerMaster) << "First published ledger will be " << mValidLedger->getLedgerSeq();
            ret.push_back (mValidLedger);
        }
        else if (mValidLedger->getLedgerSeq () > (mPubLedger->getLedgerSeq () + MAX_LEDGER_GAP))
        {
            WriteLog (lsWARNING, LedgerMaster) << "Gap in validated ledger stream " << mPubLedger->getLedgerSeq () << " - " <<
                                               mValidLedger->getLedgerSeq () - 1;
            ret.push_back (mValidLedger);
            setPubLedger (mValidLedger);
            getApp().getOrderBookDB().setup(mValidLedger);
        }
        else if (mValidLedger->getLedgerSeq () > mPubLedger->getLedgerSeq ())
        {
            int acqCount = 0;

            uint32 pubSeq = mPubLedger->getLedgerSeq() + 1; // Next sequence to publish
            uint32 valSeq = mValidLedger->getLedgerSeq();
            Ledger::pointer valLedger = mValidLedger;

            ScopedUnlockType sul(mLock, __FILE__, __LINE__);
            try
            {
                for (uint32 seq = pubSeq; seq <= valSeq; ++seq)
                {
                    WriteLog (lsTRACE, LedgerMaster) << "Trying to fetch/publish valid ledger " << seq;

                    Ledger::pointer ledger;
                    uint256 hash = valLedger->getLedgerHash (seq); // This can throw

                    if (seq == valSeq)
                    { // We need to publish the ledger we just fully validated
                        ledger = valLedger;
                    }
                    else
                    {
                        if (hash.isZero ())
                        {
                            WriteLog (lsFATAL, LedgerMaster) << "Ledger: " << valSeq << " does not have hash for " << seq;
                            assert (false);
                        }

                        ledger = mLedgerHistory.getLedgerByHash (hash);
                    }

                    if (!ledger && (++acqCount < 4))
                    { // We can try to acquire the ledger we need
                        InboundLedger::pointer acq = getApp().getInboundLedgers ().findCreate (hash, seq, false);

                        if (!acq->isDone ())
                        {
                            nothing ();
                        }
                        else if (acq->isComplete () && !acq->isFailed ())
                        {
                            ledger = acq->getLedger();
                        }
                        else
                        {
                            WriteLog (lsWARNING, LedgerMaster) << "Failed to acquire a published ledger";
                            getApp().getInboundLedgers().dropLedger(hash);
                            acq = getApp().getInboundLedgers().findCreate(hash, seq, false);
                            if (acq->isComplete())
                            {
                                if (acq->isFailed())
                                    getApp().getInboundLedgers().dropLedger(hash);
                                else
                                    ledger = acq->getLedger();
                            }
                        }
                    }

                    if (ledger && (ledger->getLedgerSeq() == pubSeq))
                    { // We acquired the next ledger we need to publish
                        ledger->setValidated();
                        ret.push_back (ledger);
                        ++pubSeq;
                    }

                }
            }
            catch (...)
            {
                WriteLog (lsERROR, LedgerMaster) << "findNewLedgersToPublish catches an exception";
            }
        }

        WriteLog (lsTRACE, LedgerMaster) << "findNewLedgersToPublish> " << ret.size();
        return ret;
    }

    void tryAdvance()
    {
        ScopedLockType ml (mLock, __FILE__, __LINE__);

        // Can't advance without at least one fully-valid ledger
        mAdvanceWork = true;
        if (!mAdvanceThread && mValidLedger)
        {
            mAdvanceThread = true;
            getApp().getJobQueue ().addJob (jtADVANCE, "advanceLedger",
                                            BIND_TYPE (&LedgerMasterImp::advanceThread, this));
        }
    }

    // Return the hash of the valid ledger with a particular sequence, given a subsequent ledger known valid
    uint256 getLedgerHash(uint32 desiredSeq, Ledger::ref knownGoodLedger)
    { 
        assert(desiredSeq < knownGoodLedger->getLedgerSeq());

        uint256 hash = knownGoodLedger->getLedgerHash(desiredSeq);

        // Not directly in the given ledger
        if (hash.isZero ())
        { 
            uint32 seq = (desiredSeq + 255) % 256;
            assert(seq < desiredSeq);

            uint256 i = knownGoodLedger->getLedgerHash(seq);
            if (i.isNonZero())
            {
                Ledger::pointer l = getLedgerByHash(i);
                if (l)
                {
                    hash = l->getLedgerHash(desiredSeq);
                    assert (hash.isNonZero());
                }
            }
            else
                assert(false);
        }

        return hash;
    }

    void updatePaths (Job& job)
    {
        {
            ScopedLockType ml (mLock, __FILE__, __LINE__);
            if (getApp().getOPs().isNeedNetworkLedger () || !mCurrentLedger)
            {
                --mPathFindThread;
                return;
            }
        }


        while (! job.shouldCancel())
        {
            Ledger::pointer lastLedger;
            {
                ScopedLockType ml (mLock, __FILE__, __LINE__);

                if (mValidLedger &&
                    (!mPathLedger || (mPathLedger->getLedgerSeq() != mValidLedger->getLedgerSeq())))
                { // We have a new valid ledger since the last full pathfinding
                    mPathLedger = mValidLedger;
                    lastLedger = mPathLedger;
                }
                else if (mPathFindNewRequest)
                { // We have a new request but no new ledger
                    lastLedger = boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);
                }
                else
                { // Nothing to do
                    --mPathFindThread;
                    return;
                }
            }

            if (!getConfig().RUN_STANDALONE)
            { // don't pathfind with a ledger that's more than 60 seconds old
                int64 age = getApp().getOPs().getCloseTimeNC();
                age -= static_cast<int64> (lastLedger->getCloseTimeNC());
                if (age > 60)
                {
                    WriteLog (lsDEBUG, LedgerMaster) << "Published ledger too old for updating paths";
                    --mPathFindThread;
                    return;
                }
            }

            try
            {
                // VFALCO TODO Fix this global variable
                PathRequest::updateAll (lastLedger, job.getCancelCallback ());
            }
            catch (SHAMapMissingNode&)
            {
                WriteLog (lsINFO, LedgerMaster) << "Missing node detected during pathfinding";
                getApp().getInboundLedgers().findCreate(lastLedger->getHash (), lastLedger->getLedgerSeq (), false);
            }
        }
    }

    void newPathRequest ()
    {
        ScopedLockType ml (mLock, __FILE__, __LINE__);
        mPathFindNewRequest = true;

        newPFWork("pf:newRequest");
    }

    bool isNewPathRequest ()
    {
        ScopedLockType ml (mLock, __FILE__, __LINE__);
        if (!mPathFindNewRequest)
            return false;
        mPathFindNewRequest = false;
        return true;
    }

    // If the order book is radically updated, we need to reprocess all pathfinding requests
    void newOrderBookDB ()
    {
        ScopedLockType ml (mLock, __FILE__, __LINE__);
        mPathLedger.reset();

        newPFWork("pf:newOBDB");
    }

    /** A thread needs to be dispatched to handle pathfinding work of some kind
    */
    void newPFWork (const char *name)
    {
        if (mPathFindThread < 2)
        {
            ++mPathFindThread;
            getApp().getJobQueue().addJob (jtUPDATE_PF, name,
                BIND_TYPE (&LedgerMasterImp::updatePaths, this, P_1));
        }
    }

    LockType& peekMutex ()
    {
        return mLock;
    }

    // The current ledger is the ledger we believe new transactions should go in
    Ledger::ref getCurrentLedger ()
    {
        return mCurrentLedger;
    }

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

    int getMinValidations ()
    {
        return mMinValidations;
    }

    void setMinValidations (int v)
    {
        mMinValidations = v;
    }

    std::string getCompleteLedgers ()
    {
        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
        return mCompleteLedgers.toString ();
    }

    /** Find or acquire the ledger with the specified index and the specified hash
        Return a pointer to that ledger if it is immediately available
    */
    Ledger::pointer findAcquireLedger (uint32 index, uint256 const& hash)
    {
        Ledger::pointer ledger (getLedgerByHash (hash));
        if (!ledger)
        {
            InboundLedger::pointer inboundLedger = getApp().getInboundLedgers().findCreate (hash, index, false);
            if (inboundLedger && inboundLedger->isComplete() && !inboundLedger->isFailed())
                ledger = inboundLedger->getLedger();
        }
        return ledger;
    }

    uint256 getHashBySeq (uint32 index)
    {
        uint256 hash = mLedgerHistory.getLedgerHash (index);

        if (hash.isNonZero ())
            return hash;

        return Ledger::getHashByIndex (index);
    }

    uint256 walkHashBySeq (uint32 index)
    {
        uint256 ledgerHash;
        Ledger::pointer referenceLedger;

        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);
            referenceLedger = mValidLedger;
        }
        if (referenceLedger)
            ledgerHash = walkHashBySeq (index, referenceLedger);
        return ledgerHash;
    }

    /** Walk the chain of ledger hashed to determine the hash of the
        ledger with the specified index. The referenceLedger is used as
        the base of the chain and should be fully validated and must not
        precede the target index. This function may throw if nodes
        from the reference ledger or any prior ledger are not present
        in the node store.
    */
    uint256 walkHashBySeq (uint32 index, Ledger::ref referenceLedger)
    {
        uint256 ledgerHash;
        if (!referenceLedger || (referenceLedger->getLedgerSeq() < index))
            return ledgerHash; // Nothing we can do. No validated ledger.

        // See if the hash for the ledger we need is in the reference ledger
        ledgerHash = referenceLedger->getLedgerHash (index);
        if (ledgerHash.isZero())
        { 
            // No, Try to get another ledger that might have the hash we need
            // Compute the index and hash of a ledger that will have the hash we need
            LedgerIndex refIndex = (index + 255) & (~255);
            LedgerHash refHash = referenceLedger->getLedgerHash (refIndex);

            if (meets_precondition (refHash.isNonZero ()))
            {
                // We found the hash and sequence of a better reference ledger
                Ledger::pointer ledger = findAcquireLedger (refIndex, refHash);
                if (ledger)
                {
                    ledgerHash = ledger->getLedgerHash (index);
                    assert (ledgerHash.isNonZero());
                }
            }
        }
        return ledgerHash;
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

    void doLedgerCleaner(const Json::Value& parameters)
    {
        mLedgerCleaner->doClean (parameters);
    }

    void setLedgerRangePresent (uint32 minV, uint32 maxV)
    {
        ScopedLockType sl (mCompleteLock, __FILE__, __LINE__);
        mCompleteLedgers.setRange (minV, maxV);
    }
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

    PropertyStream::Source& getPropertySource ()
    {
        return *mLedgerCleaner;
    }
};

//------------------------------------------------------------------------------

LedgerMaster::LedgerMaster (Stoppable& parent)
    : Stoppable ("LedgerMaster", parent)
{
}

LedgerMaster::~LedgerMaster ()
{
}

bool LedgerMaster::shouldAcquire (
    uint32 currentLedger, uint32 ledgerHistory, uint32 candidateLedger)
{
    bool ret;

    if (candidateLedger >= currentLedger)
        ret = true;
    else
        ret = (currentLedger - candidateLedger) <= ledgerHistory;

    WriteLog (lsTRACE, LedgerMaster) << "Missing ledger " << candidateLedger << (ret ? " should" : " should NOT") << " be acquired";
    return ret;
}


LedgerMaster* LedgerMaster::New (Stoppable& parent, Journal journal)
{
    return new LedgerMasterImp (parent, journal);
}
