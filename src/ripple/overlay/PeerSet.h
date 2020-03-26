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

#ifndef RIPPLE_APP_PEERS_PEERSET_H_INCLUDED
#define RIPPLE_APP_PEERS_PEERSET_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/overlay/Peer.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <mutex>
#include <set>

namespace ripple {

/** Supports data retrieval by managing a set of peers.

    When desired data (such as a ledger or a transaction set)
    is missing locally it can be obtained by querying connected
    peers. This class manages common aspects of the retrieval.
    Callers maintain the set by adding and removing peers depending
    on whether the peers have useful information.

    This class is an "active" object. It maintains its own timer
    and dispatches work to a job queue. Implementations derive
    from this class and override the abstract hook functions in
    the base.

    The data is represented by its hash.
*/
class PeerSet
{
protected:
    using ScopedLockType = std::unique_lock<std::recursive_mutex>;

    PeerSet(
        Application& app,
        uint256 const& hash,
        std::chrono::milliseconds interval,
        beast::Journal journal);

    virtual ~PeerSet() = 0;

    /** Add at most `limit` peers to this set from the overlay. */
    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> score);

    /** Hook called from addPeers(). */
    virtual void
    onPeerAdded(std::shared_ptr<Peer> const&) = 0;

    /** Hook called from invokeOnTimer(). */
    virtual void
    onTimer(bool progress, ScopedLockType&) = 0;

    /** Queue a job to call invokeOnTimer(). */
    virtual void
    queueJob() = 0;

    /** Return a weak pointer to this. */
    virtual std::weak_ptr<PeerSet>
    pmDowncast() = 0;

    bool
    isDone() const
    {
        return mComplete || mFailed;
    }

    /** Calls onTimer() if in the right state. */
    void
    invokeOnTimer();

    /** Send a GetLedger message to one or all peers. */
    void
    sendRequest(
        const protocol::TMGetLedger& message,
        std::shared_ptr<Peer> const& peer);

    /** Schedule a call to queueJob() after mTimerInterval. */
    void
    setTimer();

    // Used in this class for access to boost::asio::io_service and
    // ripple::Overlay. Used in subtypes for the kitchen sink.
    Application& app_;
    beast::Journal m_journal;

    std::recursive_mutex mLock;

    /** The hash of the object (in practice, always a ledger) we are trying to
     * fetch. */
    uint256 const mHash;
    int mTimeouts;
    bool mComplete;
    bool mFailed;
    /** Whether forward progress has been made. */
    bool mProgress;

    /** The identifiers of the peers we are tracking. */
    std::set<Peer::id_t> mPeers;

private:
    /** The minimum time to wait between calls to execute(). */
    std::chrono::milliseconds mTimerInterval;
    // VFALCO TODO move the responsibility for the timer to a higher level
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> mTimer;
};

}  // namespace ripple

#endif
