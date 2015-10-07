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
PeerSet::PeerSet (Application& app, uint256 const& hash, int interval, bool txnData,
    clock_type& clock, beast::Journal journal)
    : app_ (app)
    , m_journal (journal)
    , m_clock (clock)
    , mHash (hash)
    , mTimerInterval (interval)
    , mTimeouts (0)
    , mComplete (false)
    , mFailed (false)
    , mTxnData (txnData)
    , mProgress (false)
    , mTimer (app_.getIOService ())
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
    mTimer.async_wait (std::bind (&PeerSet::timerEntry, pmDowncast (),
                                  beast::asio::placeholders::error, m_journal));
}

void PeerSet::invokeOnTimer ()
{
    ScopedLockType sl (mLock);

    if (isDone ())
        return;

    if (!isProgress())
    {
        ++mTimeouts;
        JLOG (m_journal.debug) << "Timeout(" << mTimeouts
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

void PeerSet::timerEntry (
    std::weak_ptr<PeerSet> wptr, const boost::system::error_code& result,
    beast::Journal j)
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
            ptr->app_.getJobQueue ().addJob (
                jtTXN_DATA, "timerEntryTxn", [ptr] (Job&) {
                    timerJobEntry(ptr);
                });
        }
        else
        {
            int jc = ptr->app_.getJobQueue ().getJobCountTotal (jtLEDGER_DATA);

            if (jc > 4)
            {
                JLOG (j.debug) << "Deferring PeerSet timer due to load";
                ptr->setTimer ();
            }
            else
                ptr->app_.getJobQueue ().addJob (
                    jtLEDGER_DATA, "timerEntryLgr", [ptr] (Job&) {
                        timerJobEntry(ptr);
                    });
        }
    }
}

void PeerSet::timerJobEntry (std::shared_ptr<PeerSet> ptr)
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
        Peer::ptr peer (app_.overlay ().findPeerByShortID (p.first));

        if (peer)
            peer->send (packet);
    }
}

std::size_t PeerSet::getPeerCount () const
{
    std::size_t ret (0);

    for (auto const& p : mPeers)
    {
        if (app_.overlay ().findPeerByShortID (p.first))
            ++ret;
    }

    return ret;
}

} // ripple
