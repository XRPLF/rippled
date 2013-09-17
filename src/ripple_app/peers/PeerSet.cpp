//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class InboundLedger;

PeerSet::PeerSet (uint256 const& hash, int interval, bool txnData)
    : mLock (this, "PeerSet", __FILE__, __LINE__)
    , mHash (hash)
    , mTimerInterval (interval)
    , mTimeouts (0)
    , mComplete (false)
    , mFailed (false)
    , mAggressive (false)
    , mTxnData (txnData)
    , mTimer (getApp().getIOService ())
{
    mLastAction = mLastProgress = UptimeTimer::getInstance ().getElapsedSeconds ();
    assert ((mTimerInterval > 10) && (mTimerInterval < 30000));
}

bool PeerSet::peerHas (Peer::ref ptr)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (!mPeers.insert (std::make_pair (ptr->getPeerId (), 0)).second)
        return false;

    newPeer (ptr);
    return true;
}

void PeerSet::badPeer (Peer::ref ptr)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    mPeers.erase (ptr->getPeerId ());
}

void PeerSet::setTimer ()
{
    mTimer.expires_from_now (boost::posix_time::milliseconds (mTimerInterval));
    mTimer.async_wait (boost::bind (&PeerSet::TimerEntry, pmDowncast (), boost::asio::placeholders::error));
}

void PeerSet::invokeOnTimer ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (isDone ())
        return;

    if (!isProgress())
    {
        ++mTimeouts;
        WriteLog (lsWARNING, InboundLedger) << "Timeout(" << mTimeouts << ") pc=" << mPeers.size () << " acquiring " << mHash;
        onTimer (false, sl);
    }
    else
        onTimer (true, sl);

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
        if (ptr->mTxnData)
        {
            getApp().getJobQueue ().addJob (jtTXN_DATA, "timerEntry",
                BIND_TYPE (&PeerSet::TimerJobEntry, P_1, ptr));
        }
        else
        {
            int jc = getApp().getJobQueue ().getJobCountTotal (jtLEDGER_DATA);

            if (jc > 4)
            {
                WriteLog (lsDEBUG, InboundLedger) << "Deferring PeerSet timer due to load";
                ptr->setTimer ();
            }
            else
                getApp().getJobQueue ().addJob (jtLEDGER_DATA, "timerEntry",
                    BIND_TYPE (&PeerSet::TimerJobEntry, P_1, ptr));
	}
    }
}

void PeerSet::TimerJobEntry (Job&, boost::shared_ptr<PeerSet> ptr)
{
    ptr->invokeOnTimer ();
}

bool PeerSet::isActive ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return !isDone ();
}

void PeerSet::sendRequest (const protocol::TMGetLedger& tmGL, Peer::ref peer)
{
    if (!peer)
        sendRequest (tmGL);
    else
        peer->sendPacket (boost::make_shared<PackedMessage> (tmGL, protocol::mtGET_LEDGER), false);
}

void PeerSet::sendRequest (const protocol::TMGetLedger& tmGL)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (mPeers.empty ())
        return;

    PackedMessage::pointer packet = boost::make_shared<PackedMessage> (tmGL, protocol::mtGET_LEDGER);

    for (boost::unordered_map<uint64, int>::iterator it = mPeers.begin (), end = mPeers.end (); it != end; ++it)
    {
        Peer::pointer peer = getApp().getPeers ().getPeerById (it->first);

        if (peer)
            peer->sendPacket (packet, false);
    }
}

int PeerSet::takePeerSetFrom (const PeerSet& s)
{
    int ret = 0;
    mPeers.clear ();

    for (boost::unordered_map<uint64, int>::const_iterator it = s.mPeers.begin (), end = s.mPeers.end ();
            it != end; ++it)
    {
        mPeers.insert (std::make_pair (it->first, 0));
        ++ret;
    }

    return ret;
}

int PeerSet::getPeerCount () const
{
    int ret = 0;

    for (boost::unordered_map<uint64, int>::const_iterator it = mPeers.begin (), end = mPeers.end (); it != end; ++it)
        if (getApp().getPeers ().hasPeer (it->first))
            ++ret;

    return ret;
}

