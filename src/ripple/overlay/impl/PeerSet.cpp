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

#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/PeerSet.h>

namespace ripple {

using namespace std::chrono_literals;

class InboundLedger;

// VFALCO NOTE The txnData constructor parameter is a code smell.
//             It is true if we are the base of a TransactionAcquire,
//             or false if we are base of InboundLedger. All it does
//             is change the behavior of the timer depending on the
//             derived class. Why not just make the timer callback
//             function pure virtual?
//
PeerSet::PeerSet(
    Application& app,
    uint256 const& hash,
    std::chrono::milliseconds interval,
    clock_type& clock,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , m_clock(clock)
    , mHash(hash)
    , mTimerInterval(interval)
    , mTimeouts(0)
    , mComplete(false)
    , mFailed(false)
    , mProgress(false)
    , mTimer(app_.getIOService())
{
    mLastAction = m_clock.now();
    assert((mTimerInterval > 10ms) && (mTimerInterval < 30s));
}

PeerSet::~PeerSet() = default;

bool
PeerSet::insert(std::shared_ptr<Peer> const& ptr)
{
    ScopedLockType sl(mLock);

    if (!mPeers.insert(ptr->id()).second)
        return false;

    newPeer(ptr);
    return true;
}

void
PeerSet::setTimer()
{
    mTimer.expires_from_now(mTimerInterval);
    mTimer.async_wait(
        [wptr = pmDowncast()](boost::system::error_code const& ec) {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (auto ptr = wptr.lock())
                ptr->execute();
        });
}

void
PeerSet::invokeOnTimer()
{
    ScopedLockType sl(mLock);

    if (isDone())
        return;

    if (!isProgress())
    {
        ++mTimeouts;
        JLOG(m_journal.debug())
            << "Timeout(" << mTimeouts << ") pc=" << mPeers.size()
            << " acquiring " << mHash;
        onTimer(false, sl);
    }
    else
    {
        mProgress = false;
        onTimer(true, sl);
    }

    if (!isDone())
        setTimer();
}

bool
PeerSet::isActive()
{
    ScopedLockType sl(mLock);
    return !isDone();
}

void
PeerSet::sendRequest(
    const protocol::TMGetLedger& tmGL,
    std::shared_ptr<Peer> const& peer)
{
    if (!peer)
        sendRequest(tmGL);
    else
        peer->send(std::make_shared<Message>(tmGL, protocol::mtGET_LEDGER));
}

void
PeerSet::sendRequest(const protocol::TMGetLedger& tmGL)
{
    ScopedLockType sl(mLock);

    if (mPeers.empty())
        return;

    auto packet = std::make_shared<Message>(tmGL, protocol::mtGET_LEDGER);

    for (auto id : mPeers)
    {
        if (auto peer = app_.overlay().findPeerByShortID(id))
            peer->send(packet);
    }
}

std::size_t
PeerSet::getPeerCount() const
{
    std::size_t ret(0);

    for (auto id : mPeers)
    {
        if (app_.overlay().findPeerByShortID(id))
            ++ret;
    }

    return ret;
}

}  // namespace ripple
