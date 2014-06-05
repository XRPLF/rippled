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

#ifndef RIPPLE_PATHREQUESTS_H
#define RIPPLE_PATHREQUESTS_H

namespace ripple {

class PathRequests
{
public:
    PathRequests (beast::Journal journal, beast::insight::Collector::ptr const& collector)
        : mJournal (journal)
        , mLastIdentifier (0)
    {
        mFast = collector->make_event ("pathfind_fast");
        mFull = collector->make_event ("pathfind_full");
    }

    void updateAll (const std::shared_ptr<Ledger>& ledger, CancelCallback shouldCancel);

    RippleLineCache::pointer getLineCache (Ledger::pointer& ledger, bool authoritative);

    Json::Value makePathRequest (
        std::shared_ptr <InfoSub> const& subscriber,
        const std::shared_ptr<Ledger>& ledger,
        const Json::Value& request);

    void reportFast (int milliseconds)
    {
        mFast.notify (static_cast < beast::insight::Event::value_type> (milliseconds));
    }

    void reportFull (int milliseconds)
    {
        mFull.notify (static_cast < beast::insight::Event::value_type> (milliseconds));
    }

private:
    beast::Journal                   mJournal;

    beast::insight::Event            mFast;
    beast::insight::Event            mFull;

    // Track all requests
    std::vector<PathRequest::wptr>   mRequests;

    // Use a RippleLineCache
    RippleLineCache::pointer         mLineCache;

    beast::Atomic<int>               mLastIdentifier;

    typedef RippleRecursiveMutex     LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType                         mLock;

};

} // ripple

#endif
