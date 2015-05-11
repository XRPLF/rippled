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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Overlay.h>
#include <beast/asio/placeholders.h>

namespace ripple {

class InboundLedger;

// VFALCO NOTE The txnData constructor parameter is a code smell.
//             It is true if we are the base of a TransactionAcquire,
//             or false if we are base of InboundLedger. All it does
//             is change the behavior of the timer depending on the
//             derived class. Why not just make the timer callback
//             function pure virtual?
//
PeerSet::PeerSet (uint256 const& hash, int interval, bool txnData,
    clock_type& clock, beast::Journal journal)
    : m_journal (journal)
    , m_clock (clock)
    , mHash (hash)
    , mTimerInterval (interval)
    , mTimeouts (0)
    , mComplete (false)
    , mFailed (false)
    , mAggressive (false)
    , mTxnData (txnData)
    , mProgress (false)
    , mTimer (getApp().getIOService ())
{
    mLastAction = m_clock.now();
    assert ((mTimerInterval > 10) && (mTimerInterval < 30000));
}

PeerSet::~PeerSet ()
{
}

bool PeerSet::insert (Peer::ptr const& ptr)
{
    ScopedLockType sl (mLock);

    if (!mPeers.insert (std::make_pair (ptr->id (), 0)).second)
        return false;

    newPeer (ptr);
    return true;
}

void PeerSet::setTimer ()
{
    mTimer.expires_from_now (boost::posix_time::milliseconds (mTimerInterval));
    mTimer.async_wait (std::bind (&PeerSet::TimerEntry, pmDowncast (), beast::asio::placeholders::error));
}

void PeerSet::invokeOnTimer ()
{
    ScopedLockType sl (mLock);

    if (isDone ())
        return;

    if (!isProgress())
    {
        ++mTimeouts;
        WriteLog (lsDEBUG, InboundLedger) << "Timeout(" << mTimeouts
            << ") pc=" << mPeers.size () << " acquiring " << mHash;
        onTimer (false, sl);
    }
    else
    {
        mProgress = false;
        onTimer (true, sl);
    }

    if (!isDone ())
        setTimer ();
}

void PeerSet::TimerEntry (std::weak_ptr<PeerSet> wptr, const boost::system::error_code& result)
{
    if (result == boost::asio::error::operation_aborted)
        return;

    std::shared_ptr<PeerSet> ptr = wptr.lock ();

    if (ptr)
    {
        // VFALCO NOTE So this function is really two different functions depending on
        //             the value of mTxnData, which is directly tied to whether we are
        //             a base class of IncomingLedger or TransactionAcquire
        //
        if (ptr->mTxnData)
        {
            getApp().getJobQueue ().addJob (jtTXN_DATA, "timerEntryTxn",
                std::bind (&PeerSet::TimerJobEntry, std::placeholders::_1,
                           ptr));
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
                getApp().getJobQueue ().addJob (jtLEDGER_DATA, "timerEntryLgr",
                    std::bind (&PeerSet::TimerJobEntry, std::placeholders::_1,
                               ptr));
        }
    }
}

void PeerSet::TimerJobEntry (Job&, std::shared_ptr<PeerSet> ptr)
{
    ptr->invokeOnTimer ();
}

bool PeerSet::isActive ()
{
    ScopedLockType sl (mLock);
    return !isDone ();
}

void PeerSet::sendRequest (const protocol::TMGetLedger& tmGL, Peer::ptr const& peer)
{
    if (!peer)
        sendRequest (tmGL);
    else
        peer->send (std::make_shared<Message> (tmGL, protocol::mtGET_LEDGER));
}

void PeerSet::sendRequest (const protocol::TMGetLedger& tmGL)
{
    ScopedLockType sl (mLock);

    if (mPeers.empty ())
        return;

    Message::pointer packet (
        std::make_shared<Message> (tmGL, protocol::mtGET_LEDGER));

    for (auto const& p : mPeers)
    {
        Peer::ptr peer (getApp().overlay ().findPeerByShortID (p.first));

        if (peer)
            peer->send (packet);
    }
}

std::size_t PeerSet::getPeerCount () const
{
    std::size_t ret (0);

    for (auto const& p : mPeers)
    {
        if (getApp ().overlay ().findPeerByShortID (p.first))
            ++ret;
    }

    return ret;
}

} // ripple
