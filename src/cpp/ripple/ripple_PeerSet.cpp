//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class LedgerAcquire;

PeerSet::PeerSet (uint256 const& hash, int interval)
    : mHash (hash)
    , mTimerInterval (interval)
    , mTimeouts (0)
    , mComplete (false)
    , mFailed (false)
    , mProgress (true)
    , mAggressive (false)
    , mTimer (theApp->getIOService ())
{
    mLastAction = UptimeTimer::getInstance ().getElapsedSeconds ();
    assert ((mTimerInterval > 10) && (mTimerInterval < 30000));
}

void PeerSet::peerHas (Peer::ref ptr)
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (!mPeers.insert (std::make_pair (ptr->getPeerId (), 0)).second)
        return;

    newPeer (ptr);
}

void PeerSet::badPeer (Peer::ref ptr)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    mPeers.erase (ptr->getPeerId ());
}

void PeerSet::setTimer ()
{
    mTimer.expires_from_now (boost::posix_time::milliseconds (mTimerInterval));
    mTimer.async_wait (boost::bind (&PeerSet::TimerEntry, pmDowncast (), boost::asio::placeholders::error));
}

void PeerSet::invokeOnTimer ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);

    if (isDone ())
        return;

    if (!mProgress)
    {
        ++mTimeouts;
        WriteLog (lsWARNING, LedgerAcquire) << "Timeout(" << mTimeouts << ") pc=" << mPeers.size () << " acquiring " << mHash;
        onTimer (false);
    }
    else
    {
        mProgress = false;
        onTimer (true);
    }

    if (!isDone ())
        setTimer ();
}

void PeerSet::TimerEntry (boost::weak_ptr<PeerSet> wptr, const boost::system::error_code& result)
{
    if (result == boost::asio::error::operation_aborted)
        return;

    boost::shared_ptr<PeerSet> ptr = wptr.lock ();

    if (ptr)
    {
        int jc = theApp->getJobQueue ().getJobCountTotal (jtLEDGER_DATA);

        if (jc > 4)
        {
            WriteLog (lsDEBUG, LedgerAcquire) << "Deferring PeerSet timer due to load";
            ptr->setTimer ();
        }
        else
            theApp->getJobQueue ().addJob (jtLEDGER_DATA, "timerEntry", BIND_TYPE (&PeerSet::TimerJobEntry, P_1, ptr));
    }
}

void PeerSet::TimerJobEntry (Job&, boost::shared_ptr<PeerSet> ptr)
{
    ptr->invokeOnTimer ();
}

bool PeerSet::isActive ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return !isDone ();
}
