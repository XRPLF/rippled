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
#include <ripple/basics/Log.h>
#include <ripple/core/Job.h>
#include <ripple/overlay/Peer.h>
#include <beast/chrono/abstract_clock.h>
#include <beast/utility/Journal.h>
#include <boost/asio/deadline_timer.hpp>
#include <mutex>

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
public:
    using clock_type = beast::abstract_clock <std::chrono::steady_clock>;

    /** Returns the hash of the data we want. */
    uint256 const& getHash () const
    {
        return mHash;
    }

    /** Returns true if we got all the data. */
    bool isComplete () const
    {
        return mComplete;
    }

    /** Returns false if we failed to get the data. */
    bool isFailed () const
    {
        return mFailed;
    }

    /** Returns the number of times we timed out. */
    int getTimeouts () const
    {
        return mTimeouts;
    }

    bool isActive ();

    /** Called to indicate that forward progress has been made. */
    void progress ()
    {
        mProgress = true;
    }

    void touch ()
    {
        mLastAction = m_clock.now();
    }

    clock_type::time_point getLastAction () const
    {
        return mLastAction;
    }

    /** Insert a peer to the managed set.
        This will call the derived class hook function.
        @return `true` If the peer was added
    */
    bool insert (Peer::ptr const&);

    virtual bool isDone () const
    {
        return mComplete || mFailed;
    }

    Application&
    app()
    {
        return app_;
    }

private:
    static void timerEntry (
        std::weak_ptr<PeerSet>, const boost::system::error_code& result,
        beast::Journal j);
    static void timerJobEntry (std::shared_ptr<PeerSet>);

protected:
    using ScopedLockType = std::unique_lock <std::recursive_mutex>;

    PeerSet (Application& app, uint256 const& hash, int interval, bool txnData,
        clock_type& clock, beast::Journal journal);

    virtual ~PeerSet() = 0;

    virtual void newPeer (Peer::ptr const&) = 0;

    virtual void onTimer (bool progress, ScopedLockType&) = 0;

    virtual std::weak_ptr<PeerSet> pmDowncast () = 0;

    bool isProgress ()
    {
        return mProgress;
    }

    void setComplete ()
    {
        mComplete = true;
    }
    void setFailed ()
    {
        mFailed = true;
    }

    void invokeOnTimer ();

    void sendRequest (const protocol::TMGetLedger& message);

    void sendRequest (const protocol::TMGetLedger& message, Peer::ptr const& peer);

    void setTimer ();

    std::size_t getPeerCount () const;

protected:
    Application& app_;
    beast::Journal m_journal;
    clock_type& m_clock;

    std::recursive_mutex mLock;

    uint256 mHash;
    int mTimerInterval;
    int mTimeouts;
    bool mComplete;
    bool mFailed;
    bool mTxnData;
    clock_type::time_point mLastAction;
    bool mProgress;

    // VFALCO TODO move the responsibility for the timer to a higher level
    boost::asio::deadline_timer mTimer;

    // VFALCO TODO Verify that these are used in the way that the names suggest.
    using PeerIdentifier = Peer::id_t;
    using ReceivedChunkCount = int;
    using PeerSetMap = hash_map <PeerIdentifier, ReceivedChunkCount>;

    PeerSetMap mPeers;
};

} // ripple

#endif
