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
    boost::recursive_mutex::scoped_lock ml (mLock);
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
    boost::recursive_mutex::scoped_lock ml (mLock);
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
        reason = "No recently-validated ledger";
        return false;
    }
    boost::recursive_mutex::scoped_lock ml (mLock);
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
    boost::recursive_mutex::scoped_lock ml (mLock);
    mHeldTransactions.push_back (transaction->getSTransaction ());
}

void LedgerMaster::pushLedger (Ledger::pointer newLedger)
{
    // Caller should already have properly assembled this ledger into "ready-to-close" form --
    // all candidate transactions must already be applied
    WriteLog (lsINFO, LedgerMaster) << "PushLedger: " << newLedger->getHash ();

    boost::recursive_mutex::scoped_lock ml (mLock);

    if (!mPubLedger)
        mPubLedger = newLedger;

    if (!!mFinalizedLedger)
    {
        mFinalizedLedger->setClosed ();
        WriteLog (lsTRACE, LedgerMaster) << "Finalizes: " << mFinalizedLedger->getHash ();
    }

    mFinalizedLedger = mCurrentLedger;
    mCurrentLedger = newLedger;
    mEngine.setLedger (newLedger);
}

void LedgerMaster::pushLedger (Ledger::pointer newLCL, Ledger::pointer newOL, bool fromConsensus)
{
    assert (newLCL->isClosed () && newLCL->isAccepted ());
    assert (!newOL->isClosed () && !newOL->isAccepted ());

    boost::recursive_mutex::scoped_lock ml (mLock);

    if (newLCL->isAccepted ())
    {
        assert (newLCL->isClosed ());
        assert (newLCL->isImmutable ());
        mLedgerHistory.addAcceptedLedger (newLCL, fromConsensus);
        WriteLog (lsINFO, LedgerMaster) << "StashAccepted: " << newLCL->getHash ();
    }

    {
        boost::recursive_mutex::scoped_lock ml (mLock);
        mFinalizedLedger = newLCL;
        mCurrentLedger = newOL;
        mEngine.setLedger (newOL);
    }

    checkAccept (newLCL->getHash (), newLCL->getLedgerSeq ());
}

void LedgerMaster::switchLedgers (Ledger::pointer lastClosed, Ledger::pointer current)
{
    assert (lastClosed && current);

    {
        boost::recursive_mutex::scoped_lock ml (mLock);
        mFinalizedLedger = lastClosed;
        mFinalizedLedger->setClosed ();
        mFinalizedLedger->setAccepted ();
        mCurrentLedger = current;

        assert (!mCurrentLedger->isClosed ());
        mEngine.setLedger (mCurrentLedger);
    }
    checkAccept (lastClosed->getHash (), lastClosed->getLedgerSeq ());
}

void LedgerMaster::storeLedger (Ledger::pointer ledger)
{
    mLedgerHistory.addLedger (ledger);

    if (ledger->isAccepted ())
        mLedgerHistory.addAcceptedLedger (ledger, false);

    checkAccept (ledger->getHash(), ledger->getLedgerSeq());
}

Ledger::pointer LedgerMaster::closeLedger (bool recover)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
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
        boost::recursive_mutex::scoped_lock sl (mLock);
        result = mEngine.applyTransaction (*txn, params, didApply);
        ledger = mEngine.getLedger ();
    }
    if (didApply)
       getApp().getOPs ().pubProposedTransaction (ledger, txn, result);
    return result;
}

bool LedgerMaster::haveLedgerRange (uint32 from, uint32 to)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    uint32 prevMissing = mCompleteLedgers.prevMissing (to + 1);
    return (prevMissing == RangeSet::absent) || (prevMissing < from);
}

bool LedgerMaster::haveLedger (uint32 seq)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return mCompleteLedgers.hasValue (seq);
}

bool LedgerMaster::getValidatedRange (uint32& minVal, uint32& maxVal)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (!mValidLedger)
        return false;

    maxVal = mValidLedger->getLedgerSeq ();

    if (maxVal == 0)
        return false;

    minVal = mCompleteLedgers.prevMissing (maxVal);

    if (minVal == RangeSet::absent)
        minVal = 0;
    else
        ++minVal;

    return true;
}

void LedgerMaster::asyncAccept (Ledger::pointer ledger)
{
    uint32 seq = ledger->getLedgerSeq ();
    uint256 prevHash = ledger->getParentHash ();

    std::map< uint32, std::pair<uint256, uint256> > ledgerHashes;

    uint32 minHas = ledger->getLedgerSeq ();
    uint32 maxHas = ledger->getLedgerSeq ();

    while (seq > 0)
    {
        {
            boost::recursive_mutex::scoped_lock ml (mLock);
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
                boost::recursive_mutex::scoped_lock ml (mLock);
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
        boost::recursive_mutex::scoped_lock ml (mLock);
        mCompleteLedgers.setRange (minHas, maxHas);
    }

    resumeAcquiring ();
}

bool LedgerMaster::acquireMissingLedger (Ledger::ref origLedger, uint256 const& ledgerHash, uint32 ledgerSeq)
{
    // return: false = already gave up recently
    Ledger::pointer ledger = mLedgerHistory.getLedgerBySeq (ledgerSeq);

    if (ledger && (Ledger::getHashByIndex (ledgerSeq) == ledgerHash))
    {
        WriteLog (lsTRACE, LedgerMaster) << "Ledger hash found in database";
        getApp().getJobQueue ().addJob (jtPUBOLDLEDGER, "LedgerMaster::asyncAccept",
                                       BIND_TYPE (&LedgerMaster::asyncAccept, this, ledger));
        return true;
    }

    if (getApp().getInboundLedgers ().isFailure (ledgerHash))
    {
        WriteLog (lsTRACE, LedgerMaster) << "Already failed to acquire " << ledgerSeq;
        return false;
    }

    mMissingLedger = getApp().getInboundLedgers ().findCreate (ledgerHash, ledgerSeq);

    if (mMissingLedger->isComplete ())
    {
        Ledger::pointer lgr = mMissingLedger->getLedger ();

        if (lgr && (lgr->getLedgerSeq () == ledgerSeq))
            missingAcquireComplete (mMissingLedger);

        mMissingLedger.reset ();
        return true;
    }
    else if (mMissingLedger->isDone ())
    {
        mMissingLedger.reset ();
        return false;
    }

    mMissingSeq = ledgerSeq;

    if (mMissingLedger->setAccept ())
    {
        if (!mMissingLedger->addOnComplete (BIND_TYPE (&LedgerMaster::missingAcquireComplete, this, P_1)))
            getApp().getIOService ().post (BIND_TYPE (&LedgerMaster::missingAcquireComplete, this, mMissingLedger));
    }

    int fetchMax = theConfig.getSize (siLedgerFetch);
    int timeoutCount;
    int fetchCount = getApp().getInboundLedgers ().getFetchCount (timeoutCount);

    if ((fetchCount < fetchMax) && getApp().getOPs ().isFull ())
    {
        if (timeoutCount > 2)
        {
            WriteLog (lsDEBUG, LedgerMaster) << "Not acquiring due to timeouts";
        }
        else
        {
            typedef std::pair<uint32, uint256> u_pair;
            std::vector<u_pair> vec = origLedger->getLedgerHashes ();
            BOOST_REVERSE_FOREACH (const u_pair & it, vec)
            {
                if ((fetchCount < fetchMax) && (it.first < ledgerSeq) &&
                        !mCompleteLedgers.hasValue (it.first) && !getApp().getInboundLedgers ().find (it.second))
                {
                    InboundLedger::pointer acq = getApp().getInboundLedgers ().findCreate (it.second, it.first);

                    if (acq && acq->isComplete ())
                    {
                        acq->getLedger ()->setAccepted ();
                        setFullLedger (acq->getLedger ());
                        mLedgerHistory.addAcceptedLedger (acq->getLedger (), false);
                    }
                    else
                        ++fetchCount;
                }
            }
        }
    }

    if (getApp().getOPs ().shouldFetchPack (ledgerSeq) && (ledgerSeq > 40000))
    {
        // refill our fetch pack
        Ledger::pointer nextLedger = mLedgerHistory.getLedgerBySeq (ledgerSeq + 1);

        if (nextLedger)
        {
            protocol::TMGetObjectByHash tmBH;
            tmBH.set_type (protocol::TMGetObjectByHash::otFETCH_PACK);
            tmBH.set_query (true);
            tmBH.set_seq (ledgerSeq);
            tmBH.set_ledgerhash (ledgerHash.begin (), 32);
            std::vector<Peer::pointer> peerList = getApp().getPeers ().getPeerVector ();

            Peer::pointer target;
            int count = 0;

            BOOST_FOREACH (const Peer::pointer & peer, peerList)
            {
                if (peer->hasRange (ledgerSeq, ledgerSeq + 1))
                {
                    if (count++ == 0)
                        target = peer;
                    else if ((rand () % ++count) == 0)
                        target = peer;
                }
            }

            if (target)
            {
                PackedMessage::pointer packet = boost::make_shared<PackedMessage> (tmBH, protocol::mtGET_OBJECTS);
                target->sendPacket (packet, false);
            }
            else
                WriteLog (lsTRACE, LedgerMaster) << "No peer for fetch pack";
        }
    }

    return true;
}

void LedgerMaster::missingAcquireComplete (InboundLedger::pointer acq)
{
    boost::recursive_mutex::scoped_lock ml (mLock);

    if (acq->isFailed () && (mMissingSeq != 0))
    {
        WriteLog (lsWARNING, LedgerMaster) << "Acquire failed for " << mMissingSeq;
    }

    mMissingLedger.reset ();
    mMissingSeq = 0;

    if (acq->isComplete ())
    {
        acq->getLedger ()->setAccepted ();
        setFullLedger (acq->getLedger ());
        mLedgerHistory.addAcceptedLedger (acq->getLedger (), false);
    }
}

bool LedgerMaster::shouldAcquire (uint32 currentLedger, uint32 ledgerHistory, uint32 candidateLedger)
{
    bool ret;

    if (candidateLedger >= currentLedger)
        ret = true;
    else ret = (currentLedger - candidateLedger) <= ledgerHistory;

    WriteLog (lsTRACE, LedgerMaster) << "Missing ledger " << candidateLedger << (ret ? " should" : " should NOT") << " be acquired";
    return ret;
}

void LedgerMaster::resumeAcquiring ()
{
    // VFALCO NOTE These returns from the middle are troubling. You might think
    //             that calling a function called "resumeAcquiring" would
    //             actually resume acquiring. But it doesn't always resume acquiring,
    //             based on a myriad of conditions which short circuit the function
    //             in ways that the caller cannot expect or predict.
    //
    if (!getApp().getOPs ().isFull ())
        return;

    boost::recursive_mutex::scoped_lock ml (mLock);

    if (mMissingLedger && mMissingLedger->isDone ())
        mMissingLedger.reset ();

    if (mMissingLedger || !theConfig.LEDGER_HISTORY)
    {
        CondLog (mMissingLedger, lsDEBUG, LedgerMaster) << "Fetch already in progress, not resuming";
        return;
    }

    uint32 prevMissing = mCompleteLedgers.prevMissing (mFinalizedLedger->getLedgerSeq ());

    if (prevMissing == RangeSet::absent)
    {
        WriteLog (lsDEBUG, LedgerMaster) << "no prior missing ledger, not resuming";
        return;
    }

    if (shouldAcquire (mCurrentLedger->getLedgerSeq (), theConfig.LEDGER_HISTORY, prevMissing))
    {
        WriteLog (lsTRACE, LedgerMaster) << "Resuming at " << prevMissing;
        assert (!mCompleteLedgers.hasValue (prevMissing));
        Ledger::pointer nextLedger = getLedgerBySeq (prevMissing + 1);

        if (nextLedger)
            acquireMissingLedger (nextLedger, nextLedger->getParentHash (), nextLedger->getLedgerSeq () - 1);
        else
        {
            mCompleteLedgers.clearValue (prevMissing);
            WriteLog (lsINFO, LedgerMaster) << "We have a gap at: " << prevMissing + 1;
        }
    }
}

void LedgerMaster::fixMismatch (Ledger::ref ledger)
{
    int invalidate = 0;

    mMissingLedger.reset ();
    mMissingSeq = 0;

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

void LedgerMaster::setFullLedger (Ledger::pointer ledger)
{
    // A new ledger has been accepted as part of the trusted chain
    WriteLog (lsDEBUG, LedgerMaster) << "Ledger " << ledger->getLedgerSeq () << " accepted :" << ledger->getHash ();
    assert (ledger->peekAccountStateMap ()->getHash ().isNonZero ());

    if (getApp().getOPs ().isNeedNetworkLedger ())
        return;

    boost::recursive_mutex::scoped_lock ml (mLock);

    mCompleteLedgers.setValue (ledger->getLedgerSeq ());

    if (Ledger::getHashByIndex (ledger->getLedgerSeq ()) != ledger->getHash ())
    {
        ledger->pendSave (false);
        return;
    }

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

    if (mMissingLedger && mMissingLedger->isDone ())
    {
        if (mMissingLedger->isFailed ())
            getApp().getInboundLedgers ().dropLedger (mMissingLedger->getHash ());

        mMissingLedger.reset ();
    }

    if (mMissingLedger || !theConfig.LEDGER_HISTORY)
    {
        CondLog (mMissingLedger, lsDEBUG, LedgerMaster) << "Fetch already in progress, " << mMissingLedger->getTimeouts () << " timeouts";
        return;
    }

    if (getApp().getJobQueue ().getJobCountTotal (jtPUBOLDLEDGER) > 1)
    {
        WriteLog (lsDEBUG, LedgerMaster) << "Too many pending ledger saves";
        return;
    }

    // see if there's a ledger gap we need to fill
    if (!mCompleteLedgers.hasValue (ledger->getLedgerSeq () - 1))
    {
        if (!shouldAcquire (mCurrentLedger->getLedgerSeq (), theConfig.LEDGER_HISTORY, ledger->getLedgerSeq () - 1))
        {
            WriteLog (lsTRACE, LedgerMaster) << "Don't need any ledgers";
            return;
        }

        WriteLog (lsDEBUG, LedgerMaster) << "We need the ledger before the ledger we just accepted: " << ledger->getLedgerSeq () - 1;
        acquireMissingLedger (ledger, ledger->getParentHash (), ledger->getLedgerSeq () - 1);
    }
    else
    {
        uint32 prevMissing = mCompleteLedgers.prevMissing (ledger->getLedgerSeq ());

        if (prevMissing == RangeSet::absent)
        {
            WriteLog (lsDEBUG, LedgerMaster) << "no prior missing ledger";
            return;
        }

        if (shouldAcquire (mCurrentLedger->getLedgerSeq (), theConfig.LEDGER_HISTORY, prevMissing))
        {
            WriteLog (lsDEBUG, LedgerMaster) << "Ledger " << prevMissing << " is needed";
            assert (!mCompleteLedgers.hasValue (prevMissing));
            Ledger::pointer nextLedger = getLedgerBySeq (prevMissing + 1);

            if (nextLedger)
                acquireMissingLedger (ledger, nextLedger->getParentHash (), nextLedger->getLedgerSeq () - 1);
            else
            {
                mCompleteLedgers.clearValue (prevMissing);
                WriteLog (lsWARNING, LedgerMaster) << "We have a gap we can't fix: " << prevMissing + 1;
            }
        }
        else
            WriteLog (lsTRACE, LedgerMaster) << "Shouldn't acquire";
    }
}

void LedgerMaster::checkAccept (uint256 const& hash)
{
    Ledger::pointer ledger = mLedgerHistory.getLedgerByHash (hash);

    if (ledger)
        checkAccept (hash, ledger->getLedgerSeq ());
}

void LedgerMaster::checkAccept (uint256 const& hash, uint32 seq)
{
    // Can we advance the last fully accepted ledger? If so, can we publish?
    boost::recursive_mutex::scoped_lock ml (mLock);

    if (mValidLedger && (seq <= mValidLedger->getLedgerSeq ()))
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

    if (theConfig.RUN_STANDALONE)
        minVal = 0;
    else if (getApp().getOPs ().isNeedNetworkLedger ())
        minVal = 1;

    if (getApp().getValidations ().getTrustedValidationCount (hash) < minVal) // nothing we can do
        return;

    WriteLog (lsINFO, LedgerMaster) << "Advancing accepted ledger to " << seq << " with >= " << minVal << " validations";

    mLastValidateHash = hash;
    mLastValidateSeq = seq;

    Ledger::pointer ledger = mLedgerHistory.getLedgerByHash (hash);

    if (!ledger)
    {
        getApp().getInboundLedgers ().findCreate (hash, seq);
        return;
    }

    mValidLedger = ledger;

    uint64 fee, fee2, ref;
    ref = getApp().getFeeTrack().getLoadBase();
    int count = getApp().getValidations().getFeeAverage(ledger->getHash(), ref, fee);
    int count2 = getApp().getValidations().getFeeAverage(ledger->getParentHash(), ref, fee2);

    if ((count + count2) == 0)
        getApp().getFeeTrack().setRemoteFee(ref);
    else
        getApp().getFeeTrack().setRemoteFee(((fee * count) + (fee2 * count2)) / (count + count2));

    tryPublish ();
}

void LedgerMaster::tryPublish ()
{
    boost::recursive_mutex::scoped_lock ml (mLock);
    assert (mValidLedger);

    if (!mPubLedger)
    {
        mPubLedger = mValidLedger;
        mPubLedgers.push_back (mValidLedger);
    }
    else if (mValidLedger->getLedgerSeq () > (mPubLedger->getLedgerSeq () + MAX_LEDGER_GAP))
    {
        WriteLog (lsWARNING, LedgerMaster) << "Gap in validated ledger stream " << mPubLedger->getLedgerSeq () << " - " <<
                                           mValidLedger->getLedgerSeq () - 1;
        mPubLedger = mValidLedger;
        mPubLedgers.push_back (mValidLedger);
    }
    else if (mValidLedger->getLedgerSeq () > mPubLedger->getLedgerSeq ())
    {
        int acq = 0;
        for (uint32 seq = mPubLedger->getLedgerSeq () + 1; seq <= mValidLedger->getLedgerSeq (); ++seq)
        {
            WriteLog (lsTRACE, LedgerMaster) << "Trying to publish ledger " << seq;

            Ledger::pointer ledger;
            uint256 hash;

            if (seq == mValidLedger->getLedgerSeq ())
            { // We need to publish the ledger we just fully validated
                ledger = mValidLedger;
                hash = ledger->getHash ();
            }
            else
            {
                hash = mValidLedger->getLedgerHash (seq);

                if (hash.isZero ())
                {
                    WriteLog (lsFATAL, LedgerMaster) << "Ledger: " << mValidLedger->getLedgerSeq () << " does not have hash for " <<
                                                     seq;
                    assert (false);
                }

                ledger = mLedgerHistory.getLedgerByHash (hash);
            }

            if (!ledger && (++acq < 4))
            { // We can try to acquire the ledger we need
	        InboundLedger::pointer acq = getApp().getInboundLedgers ().findCreate (hash, seq);

                if (!acq->isDone ())
                {
                    acq->setAccept ();
                }
                else if (acq->isComplete () && !acq->isFailed ())
                {
                    ledger = acq->getLedger();
                }
                else
                    WriteLog (lsWARNING, LedgerMaster) << "Failed to acquire a published ledger";
            }

            if (ledger && (ledger->getLedgerSeq() == (mPubLedger->getLedgerSeq() + 1)))
            { // We acquired the next ledger we need to publish
	        mPubLedger = ledger;
                mPubLedgers.push_back (mPubLedger);
            }

        }
    }

    if (!mPubLedgers.empty () && !mPubThread)
    {
        getApp().getOPs ().clearNeedNetworkLedger ();
        mPubThread = true;
        getApp().getJobQueue ().addJob (jtPUBLEDGER, "Ledger::pubThread",
                                       BIND_TYPE (&LedgerMaster::pubThread, this));
        mPathFindNewLedger = true;

        if (!mPathFindThread)
        {
            mPathFindThread = true;
            getApp().getJobQueue ().addJob (jtUPDATE_PF, "updatePaths",
                                           BIND_TYPE (&LedgerMaster::updatePaths, this));
        }
    }
}

void LedgerMaster::pubThread ()
{
    std::list<Ledger::pointer> ledgers;
    bool published = false;

    while (1)
    {
        ledgers.clear ();

        {
            boost::recursive_mutex::scoped_lock ml (mLock);
            mPubLedgers.swap (ledgers);

            if (ledgers.empty ())
            {
                mPubThread = false;

                if (published && !mPathFindThread)
                {
                    mPathFindThread = true;
                    getApp().getJobQueue ().addJob (jtUPDATE_PF, "updatePaths",
                                                   BIND_TYPE (&LedgerMaster::updatePaths, this));
                }

                return;
            }
        }

        BOOST_FOREACH (Ledger::ref l, ledgers)
        {
            WriteLog (lsDEBUG, LedgerMaster) << "Publishing ledger " << l->getLedgerSeq ();
            setFullLedger (l); // OPTIMIZEME: This is actually more work than we need to do
            getApp().getOPs ().pubLedger (l);
            published = true;
        }
    }
}

void LedgerMaster::updatePaths ()
{
    Ledger::pointer lastLedger;

    do
    {
        bool newOnly = false;

        {
            boost::recursive_mutex::scoped_lock ml (mLock);

            if (mPathFindNewLedger || (lastLedger && (lastLedger.get () != mPubLedger.get ())))
                lastLedger = mPubLedger;
            else if (mPathFindNewRequest)
            {
                newOnly = true;
                lastLedger = boost::make_shared<Ledger> (boost::ref (*mCurrentLedger), false);
            }
            else
            {
                mPathFindThread = false;
                return;
            }

            lastLedger = mPubLedger;
            mPathFindNewLedger = false;
            mPathFindNewRequest = false;
        }

        // VFALCO TODO Fix this global variable
        PathRequest::updateAll (lastLedger, newOnly);

    }
    while (1);
}

void LedgerMaster::newPathRequest ()
{
    boost::recursive_mutex::scoped_lock ml (mLock);
    mPathFindNewRequest = true;

    if (!mPathFindThread)
    {
        mPathFindThread = true;
        getApp().getJobQueue ().addJob (jtUPDATE_PF, "updatePaths",
                                       BIND_TYPE (&LedgerMaster::updatePaths, this));
    }
}

// vim:ts=4
