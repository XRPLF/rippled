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

#ifndef RIPPLE_APP_PATHS_PATHREQUESTS_H_INCLUDED
#define RIPPLE_APP_PATHS_PATHREQUESTS_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/app/paths/PathRequest.h>
#include <ripple/app/paths/RippleLineCache.h>
#include <ripple/core/Job.h>
#include <atomic>
#include <mutex>

namespace ripple {

class PathRequests
{
public:
    PathRequests (Application& app,
            beast::Journal journal, beast::insight::Collector::ptr const& collector)
        : app_ (app)
        , mJournal (journal)
        , mLastIdentifier (0)
    {
        mFast = collector->make_event ("pathfind_fast");
        mFull = collector->make_event ("pathfind_full");
    }

    void updateAll (std::shared_ptr<ReadView const> const& ledger,
                    Job::CancelCallback shouldCancel);

    RippleLineCache::pointer getLineCache (
        std::shared_ptr <ReadView const> const& ledger, bool authoritative);

    Json::Value makePathRequest (
        std::shared_ptr <InfoSub> const& subscriber,
        std::shared_ptr<ReadView const> const& ledger,
        Json::Value const& request);

    Json::Value makeLegacyPathRequest (
        PathRequest::pointer& req,
        std::function <void (void)> completion,
        std::shared_ptr<ReadView const> const& inLedger,
        Json::Value const& request);

    void reportFast (int milliseconds)
    {
        mFast.notify (static_cast < beast::insight::Event::value_type> (milliseconds));
    }

    void reportFull (int milliseconds)
    {
        mFull.notify (static_cast < beast::insight::Event::value_type> (milliseconds));
    }

private:
    void insertPathRequest (PathRequest::pointer const&);

    Application& app_;
    beast::Journal                   mJournal;

    beast::insight::Event            mFast;
    beast::insight::Event            mFull;

    // Track all requests
    std::vector<PathRequest::wptr>   mRequests;

    // Use a RippleLineCache
    RippleLineCache::pointer         mLineCache;

    std::atomic<int>                 mLastIdentifier;

    using ScopedLockType = std::lock_guard <std::recursive_mutex>;
    std::recursive_mutex mLock;

};

} // ripple

#endif
