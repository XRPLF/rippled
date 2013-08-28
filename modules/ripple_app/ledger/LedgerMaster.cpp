//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#define MIN_VALIDATION_RATIO    150     // 150/256ths of validations of previous ledger
#define MAX_LEDGER_GAP          100     // Don't catch up more than 100 ledgers  (cannot exceed 256)

SETUP_LOG (LedgerMaster)

uint32 LedgerMaster::getCurrentLedgerIndex ()
{
    return mCurrentLedger->getLedgerSeq ();
}

Ledger::ref LedgerMaster::getCurrentSnapshot ()
{
    if (!mCurrentSnapshot || (mCurrentSnapshot->getHash () != mCurrentLedger->getHash ()))
        mCurrentSnapshot = boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);

    assert (mCurrentSnapshot->isImmutable ());
    return mCurrentSnapshot;
}

int LedgerMaster::getPublishedLedgerAge ()
{
    ScopedLockType ml (mLock, __FILE__, __LINE__);
    if (!mPubLedger)
    {
        WriteLog (lsDEBUG, LedgerMaster) << "No published ledger";
        return 999999;
    }

    int64 ret = getApp().getOPs ().getCloseTimeNC ();
    ret -= static_cast<int64> (mPubLedger->getCloseTimeNC ());
    ret = std::max (0LL, ret);

    WriteLog (lsTRACE, LedgerMaster) << "Published ledger age is " << ret;
    return static_cast<int> (ret);
}

int LedgerMaster::getValidatedLedgerAge ()
{
    ScopedLockType ml (mLock, __FILE__, __LINE__);
    if (!mValidLedger)
    {
        WriteLog (lsDEBUG, LedgerMaster) << "No validated ledger";
        return 999999;
    }

    int64 ret = getApp().getOPs ().getCloseTimeNC ();
    ret -= static_cast<int64> (mValidLedger->getCloseTimeNC ());
    ret = std::max (0LL, ret);

    WriteLog (lsTRACE, LedgerMaster) << "Validated ledger age is " << ret;
    return static_cast<int> (ret);
}

bool LedgerMaster::isCaughtUp(std::string& reason)
{
    if (getPublishedLedgerAge() > 180)
    {
        reason = "No recently-published ledger";
        return false;
    }
    ScopedLockType ml (mLock, __FILE__, __LINE__);
    if (!mValidLedger || !mPubLedger)
    {
        reason = "No published ledger";
        return false;
    }
    if (mValidLedger->getLedgerSeq() > (mPubLedger->getLedgerSeq() + 3))
    {
        reason = "Published ledger lags validated ledger";
        return false;
    }
    return true;
}

void LedgerMaster::addHeldTransaction (Transaction::ref transaction)
{
    // returns true if transaction was added
    ScopedLockType ml (mLock, __FILE__, __LINE__);
    mHeldTransactions.push_back (transaction->getSTransaction ());
}

void LedgerMaster::pushLedger (Ledger::pointer newLedger)
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

void LedgerMaster::pushLedger (Ledger::pointer newLCL, Ledger::pointer newOL)
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
        checkAccept (newLCL);
}

void LedgerMaster::switchLedgers (Ledger::pointer lastClosed, Ledger::pointer current)
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

void LedgerMaster::storeLedger (Ledger::pointer ledger)
{
    mLedgerHistory.addLedger (ledger);
}

void LedgerMaster::forceValid (Ledger::pointer ledger)
{
    ledger->setValidated();
    setFullLedger(ledger, true, false);
}

Ledger::pointer LedgerMaster::closeLedger (bool recover)
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

TER LedgerMaster::doTransaction (SerializedTransaction::ref txn, TransactionEngineParams params, bool& didApply)
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

bool LedgerMaster::haveLedgerRange (uint32 from, uint32 to)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    uint32 prevMissing = mCompleteLedgers.prevMissing (to + 1);
    return (prevMissing == RangeSet::absent) || (prevMissing < from);
}

bool LedgerMaster::haveLedger (uint32 seq)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return mCompleteLedgers.hasValue (seq);
}

bool LedgerMaster::getFullValidatedRange (uint32& minVal, uint32& maxVal)
{ // Ledgers we have all the nodes for
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (!mPubLedger)
        return false;

    maxVal = mPubLedger->getLedgerSeq ();

    if (maxVal == 0)
        return false;

    minVal = mCompleteLedgers.prevMissing (maxVal);

    if (minVal == RangeSet::absent)
        minVal = maxVal;
    else
        ++minVal;

    return true;
}

bool LedgerMaster::getValidatedRange (uint32& minVal, uint32& maxVal)
{ // Ledgers we have all the nodes for and are indexed
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (!mPubLedger)
        return false;

    maxVal = mPubLedger->getLedgerSeq ();

    if (maxVal == 0)
        return false;

    minVal = mCompleteLedgers.prevMissing (maxVal);

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

void LedgerMaster::tryFill (Ledger::pointer ledger)
{
    uint32 seq = ledger->getLedgerSeq ();
    uint256 prevHash = ledger->getParentHash ();

    std::map< uint32, std::pair<uint256, uint256> > ledgerHashes;

    uint32 minHas = ledger->getLedgerSeq ();
    uint32 maxHas = ledger->getLedgerSeq ();

    while (seq > 0)
    {
        {
            ScopedLockType ml (mLock, __FILE__, __LINE__);
            minHas = seq;
            --seq;

            if (mCompleteLedgers.hasValue (seq))
                break;
        }

        std::map< uint32, std::pair<uint256, uint256> >::iterator it = ledgerHashes.find (seq);

        if (it == ledgerHashes.end ())
        {
            if (getApp().isShutdown ())
                return;

            {
                ScopedLockType ml (mLock, __FILE__, __LINE__);
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
        ScopedLockType ml (mLock, __FILE__, __LINE__);
        mCompleteLedgers.setRange (minHas, maxHas);
        mFillInProgress = false;
        tryAdvance();
    }
}

void LedgerMaster::getFetchPack (Ledger::ref nextLedger)
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
            else if ((rand () % count) == 0)
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

bool LedgerMaster::shouldAcquire (uint32 currentLedger, uint32 ledgerHistory, uint32 candidateLedger)
{
    bool ret;

    if (candidateLedger >= currentLedger)
        ret = true;
    else
        ret = (currentLedger - candidateLedger) <= ledgerHistory;

    WriteLog (lsTRACE, LedgerMaster) << "Missing ledger " << candidateLedger << (ret ? " should" : " should NOT") << " be acquired";
    return ret;
}

void LedgerMaster::fixMismatch (Ledger::ref ledger)
{
    int invalidate = 0;

    for (uint32 lSeq = ledger->getLedgerSeq () - 1; lSeq > 0; --lSeq)
        if (mCompleteLedgers.hasValue (lSeq))
        {
            uint256 hash = ledger->getLedgerHash (lSeq);

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

            mCompleteLedgers.clearValue (lSeq);
            ++invalidate;
        }

    // all prior ledgers invalidated
    CondLog (invalidate != 0, lsWARNING, LedgerMaster) << "All " << invalidate << " prior ledgers invalidated";
}

void LedgerMaster::setFullLedger (Ledger::pointer ledger, bool isSynchronous, bool isCurrent)
{
    // A new ledger has been accepted as part of the trusted chain
    WriteLog (lsDEBUG, LedgerMaster) << "Ledger " << ledger->getLedgerSeq () << " accepted :" << ledger->getHash ();
    assert (ledger->peekAccountStateMap ()->getHash ().isNonZero ());

    ledger->setValidated();
    mLedgerHistory.addLedger(ledger);

    ScopedLockType ml (mLock, __FILE__, __LINE__);

    mCompleteLedgers.setValue (ledger->getLedgerSeq ());

    ledger->pendSaveValidated (isSynchronous, isCurrent);

    if (!mValidLedger || (ledger->getLedgerSeq() > mValidLedger->getLedgerSeq()))
        mValidLedger = ledger;
    if (!mPubLedger)
        mPubLedger = ledger;

    if ((ledger->getLedgerSeq () != 0) && mCompleteLedgers.hasValue (ledger->getLedgerSeq () - 1))
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

void LedgerMaster::checkAccept (uint256 const& hash)
{
    Ledger::pointer ledger = mLedgerHistory.getLedgerByHash (hash);

    if (!ledger)
    {
        InboundLedger::pointer l = getApp().getInboundLedgers().findCreate(hash, 0, false);
        if (l->isComplete() && !l->isFailed())
            ledger = l->getLedger();
        else
        {
            WriteLog (lsDEBUG, LedgerMaster) << "checkAccept triggers acquire " << hash.GetHex();
        }
    }

    if (ledger)
        checkAccept (ledger);
}

void LedgerMaster::checkAccept (Ledger::ref ledger)
{
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
    mValidLedger = ledger;
    if (!mPubLedger)
    {
        ledger->pendSaveValidated(true, true);
        mPubLedger = ledger;
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

// Try to publish ledgers, acquire missing ledgers
void LedgerMaster::advanceThread()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    assert (mValidLedger && mAdvanceThread);

    bool progress;

    WriteLog (lsTRACE, LedgerMaster) << "advanceThread<";

    do
    {
        progress = false;

        std::list<Ledger::pointer> pubLedgers = findNewLedgersToPublish (sl);
        if (pubLedgers.empty())
        {
            if (!mFillInProgress && !getConfig().RUN_STANDALONE && !getApp().getFeeTrack().isLoadedLocal() &&
                (getApp().getJobQueue().getJobCount(jtPUBOLDLEDGER) < 10) &&
                (mValidLedger->getLedgerSeq() == mPubLedger->getLedgerSeq()))
            { // We are in sync, so can acquire
                uint32 missing = mCompleteLedgers.prevMissing(mPubLedger->getLedgerSeq());
                WriteLog (lsTRACE, LedgerMaster) << "tryAdvance discovered missing " << missing;
                if ((missing != RangeSet::absent) && (missing > 0) &&
                    shouldAcquire(mValidLedger->getLedgerSeq(), getConfig().LEDGER_HISTORY, missing))
                {
                    WriteLog (lsTRACE, LedgerMaster) << "advanceThread should acquire";
                    sl.unlock();
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
                            if (Ledger::getHashByIndex(ledger->getLedgerSeq() - 1) == ledger->getParentHash())
                            { // Previous ledger is in DB
                                sl.lock(__FILE__, __LINE__);
                                mFillInProgress = true;
                                getApp().getJobQueue().addJob(jtADVANCE, "tryFill", BIND_TYPE (&LedgerMaster::tryFill, this, ledger));
                                sl.unlock();
                            }
                            progress = true;
                        }
                        else
                        {
                            for (int i = 0; i < getConfig().getSize(siLedgerFetch); ++i)
                            {
                                uint32 seq = missing - i;
                                uint256 hash = nextLedger->getLedgerHash(seq);
                                if (hash.isNonZero())
                                    getApp().getInboundLedgers().findCreate(hash, seq, false);
                            }
                        }
                    }
                    else
                    {
                        WriteLog (lsFATAL, LedgerMaster) << "Unable to find ledger following prevMissing " << missing;
                        WriteLog (lsFATAL, LedgerMaster) << "Pub:" << mPubLedger->getLedgerSeq() << " Val:" << mValidLedger->getLedgerSeq();
                        assert(false);
                    }
                    sl.lock(__FILE__, __LINE__);
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
                sl.unlock();
                WriteLog(lsDEBUG, LedgerMaster) << "tryAdvance publishing seq " << ledger->getLedgerSeq();

                setFullLedger(ledger, true, true);
                getApp().getOPs().pubLedger(ledger);

                sl.lock(__FILE__, __LINE__);
                mPubLedger = ledger;
                progress = true;
            }

            getApp().getOPs().clearNeedNetworkLedger();

            if (!mPathFindThread)
            {
                mPathFindThread = true;
                getApp().getJobQueue ().addJob (jtUPDATE_PF, "updatePaths",
                    BIND_TYPE (&LedgerMaster::updatePaths, this));
            }
        }
    } while (progress);

    mAdvanceThread = false;
    WriteLog (lsTRACE, LedgerMaster) << "advanceThread>";
}

std::list<Ledger::pointer> LedgerMaster::findNewLedgersToPublish(ScopedLockType& sl)
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
    }
    else if (mValidLedger->getLedgerSeq () > mPubLedger->getLedgerSeq ())
    {
        int acqCount = 0;

        uint32 pubSeq = mPubLedger->getLedgerSeq() + 1; // Next sequence to publish
        uint32 valSeq = mValidLedger->getLedgerSeq();
        Ledger::pointer valLedger = mValidLedger;

        sl.unlock();
        for (uint32 seq = pubSeq; seq <= valSeq; ++seq)
        {
            WriteLog (lsTRACE, LedgerMaster) << "Trying to fetch/publish valid ledger " << seq;

            Ledger::pointer ledger;
            uint256 hash = valLedger->getLedgerHash (seq);

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
        sl.lock(__FILE__, __LINE__);
    }

    WriteLog (lsTRACE, LedgerMaster) << "findNewLedgersToPublish> " << ret.size();
    return ret;
}

void LedgerMaster::tryAdvance()
{
    ScopedLockType ml (mLock, __FILE__, __LINE__);

    // Can't advance without at least one fully-valid ledger
    if (!mAdvanceThread && mValidLedger)
    {
        mAdvanceThread = true;
        getApp().getJobQueue ().addJob (jtADVANCE, "advanceLedger",
                                        BIND_TYPE (&LedgerMaster::advanceThread, this));
    }
}

uint256 LedgerMaster::getLedgerHash(uint32 desiredSeq, Ledger::ref knownGoodLedger)
{ // Get the hash of the valid ledger with a particular sequence, given a subsequent ledger known valid

    assert(desiredSeq < knownGoodLedger->getLedgerSeq());

    uint256 hash = knownGoodLedger->getLedgerHash(desiredSeq);

    if (hash.isZero ())
    { // Not directly in the given ledger

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

void LedgerMaster::updatePaths ()
{
    Ledger::pointer lastLedger;

    do
    {
        bool newOnly = true;

        {
            ScopedLockType ml (mLock, __FILE__, __LINE__);

            if (!mPathLedger || (mPathLedger->getLedgerSeq() < mValidLedger->getLedgerSeq()))
            { // We have a new valid ledger since the last full pathfinding
                newOnly = false;
                mPathLedger = mValidLedger;
                lastLedger = mPathLedger;
            }
            else if (mPathFindNewRequest)
            { // We have a new request but no new ledger
                newOnly = true;
                lastLedger = boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);
            }
            else
            { // Nothing to do
                mPathFindThread = false;
                return;
            }

            mPathFindNewRequest = false;
        }

        // VFALCO TODO Fix this global variable
        PathRequest::updateAll (lastLedger, newOnly);

    }
    while (1);
}

void LedgerMaster::newPathRequest ()
{
    ScopedLockType ml (mLock, __FILE__, __LINE__);
    mPathFindNewRequest = true;

    if (!mPathFindThread)
    {
        mPathFindThread = true;
        getApp().getJobQueue ().addJob (jtUPDATE_PF, "updatePaths",
                                       BIND_TYPE (&LedgerMaster::updatePaths, this));
    }
}

// vim:ts=4
