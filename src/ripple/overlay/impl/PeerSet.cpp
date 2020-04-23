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

PeerSet::PeerSet(
    Application& app,
    uint256 const& hash,
    std::chrono::milliseconds interval,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , mHash(hash)
    , mTimeouts(0)
    , mComplete(false)
    , mFailed(false)
    , mProgress(false)
    , mTimerInterval(interval)
    , mTimer(app_.getIOService())
{
    assert((mTimerInterval > 10ms) && (mTimerInterval < 30s));
}

PeerSet::~PeerSet() = default;

void
PeerSet::addPeers(
    std::size_t limit,
    std::function<bool(std::shared_ptr<Peer> const&)> hasItem)
{
    using ScoredPeer = std::pair<int, std::shared_ptr<Peer>>;

    auto const& overlay = app_.overlay();

    std::vector<ScoredPeer> pairs;
    pairs.reserve(overlay.size());

    overlay.foreach([&](auto const& peer) {
        auto const score = peer->getScore(hasItem(peer));
        pairs.emplace_back(score, std::move(peer));
    });

    std::sort(
        pairs.begin(),
        pairs.end(),
        [](ScoredPeer const& lhs, ScoredPeer const& rhs) {
            return lhs.first > rhs.first;
        });

    std::size_t accepted = 0;
    ScopedLockType sl(mLock);
    for (auto const& pair : pairs)
    {
        auto const peer = pair.second;
        if (!mPeers.insert(peer->id()).second)
            continue;
        onPeerAdded(peer);
        if (++accepted >= limit)
            break;
    }
}

void
PeerSet::setTimer()
{
    mTimer.expires_after(mTimerInterval);
    mTimer.async_wait(
        [wptr = pmDowncast()](boost::system::error_code const& ec) {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (auto ptr = wptr.lock())
                ptr->queueJob();
        });
}

void
PeerSet::invokeOnTimer()
{
    ScopedLockType sl(mLock);

    if (isDone())
        return;

    if (!mProgress)
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

void
PeerSet::sendRequest(
    const protocol::TMGetLedger& tmGL,
    std::shared_ptr<Peer> const& peer)
{
    auto packet = std::make_shared<Message>(tmGL, protocol::mtGET_LEDGER);

    if (peer)
    {
        peer->send(packet);
        return;
    }

    ScopedLockType sl(mLock);

    for (auto id : mPeers)
    {
        if (auto p = app_.overlay().findPeerByShortID(id))
            p->send(packet);
    }
}

}  // namespace ripple
