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

#ifndef RIPPLE_PEERSET_H
#define RIPPLE_PEERSET_H

/** A set of peers used to acquire data.

    A peer set is used to acquire a ledger or a transaction set.
*/
class PeerSet : LeakChecked <PeerSet>
{
public:
    uint256 const& getHash () const
    {
        return mHash;
    }
    bool isComplete () const
    {
        return mComplete;
    }
    bool isFailed () const
    {
        return mFailed;
    }
    int getTimeouts () const
    {
        return mTimeouts;
    }

    bool isActive ();
    void progress ()
    {
        mProgress = true;
        mAggressive = false;
    }
    void clearProgress ()
    {
        mProgress = false;
    }
    bool isProgress ()
    {
        return mProgress;
    }
    void touch ()
    {
        mLastAction = UptimeTimer::getInstance ().getElapsedSeconds ();
    }
    int getLastAction ()
    {
        return mLastAction;
    }

    bool peerHas (Peer::ref);
    void badPeer (Peer::ref);
    void setTimer ();

    int takePeerSetFrom (const PeerSet& s);
    int getPeerCount () const;
    virtual bool isDone () const
    {
        return mComplete || mFailed;
    }

private:
    static void TimerEntry (boost::weak_ptr<PeerSet>, const boost::system::error_code& result);
    static void TimerJobEntry (Job&, boost::shared_ptr<PeerSet>);

    // VFALCO TODO try to make some of these private
protected:
    typedef RippleRecursiveMutex LockType;
    typedef LockType::ScopedLockType ScopedLockType;

    PeerSet (uint256 const& hash, int interval, bool txnData);
    virtual ~PeerSet () { }

    virtual void newPeer (Peer::ref) = 0;
    virtual void onTimer (bool progress, ScopedLockType&) = 0;
    virtual boost::weak_ptr<PeerSet> pmDowncast () = 0;

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
    void sendRequest (const protocol::TMGetLedger& message, Peer::ref peer);

protected:
    LockType mLock;

    uint256 mHash;
    int mTimerInterval;
    int mTimeouts;
    bool mComplete;
    bool mFailed;
    bool mAggressive;
    bool mTxnData;
    int mLastAction;
    bool mProgress;


    // VFALCO TODO move the responsibility for the timer to a higher level
    boost::asio::deadline_timer             mTimer;

    // VFALCO TODO Verify that these are used in the way that the names suggest.
    typedef uint64 PeerIdentifier;
    typedef int ReceivedChunkCount;
    boost::unordered_map <PeerIdentifier, ReceivedChunkCount> mPeers;
};

#endif
