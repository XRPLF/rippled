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
#include <ripple/app/paths/PathRequests.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/resource/Fees.h>
#include <algorithm>

namespace ripple {

/** Get the current RippleLineCache, updating it if necessary.
    Get the correct ledger to use.
*/
RippleLineCache::pointer PathRequests::getLineCache (
    std::shared_ptr <ReadView const> const& ledger, bool authoritative)
{

    ScopedLockType sl (mLock);

    std::uint32_t lineSeq = mLineCache ? mLineCache->getLedger()->seq() : 0;
    std::uint32_t lgrSeq = ledger->seq();

    if ( (lineSeq == 0) ||                                 // no ledger
         (authoritative && (lgrSeq > lineSeq)) ||          // newer authoritative ledger
         (authoritative && ((lgrSeq + 8)  < lineSeq)) ||   // we jumped way back for some reason
         (lgrSeq > (lineSeq + 8)))                         // we jumped way forward for some reason
    {
        mLineCache = std::make_shared<RippleLineCache> (ledger);
    }
    return mLineCache;
}

void PathRequests::updateAll (std::shared_ptr <ReadView const> const& inLedger,
                              Job::CancelCallback shouldCancel)
{
    std::vector<PathRequest::wptr> requests;

    LoadEvent::autoptr event (app_.getJobQueue().getLoadEventAP(jtPATH_FIND, "PathRequest::updateAll"));

    // Get the ledger and cache we should be using
    RippleLineCache::pointer cache;
    {
        ScopedLockType sl (mLock);
        requests = mRequests;
        cache = getLineCache (inLedger, true);
    }

    bool newRequests = app_.getLedgerMaster().isNewPathRequest();
    bool mustBreak = false;

    mJournal.trace << "updateAll seq=" << cache->getLedger()->seq() << ", " <<
        requests.size() << " requests";
    int processed = 0, removed = 0;

    do
    {
        for (auto& wRequest : requests)
        {
            if (shouldCancel())
                break;

            bool remove = true;
            PathRequest::pointer pRequest = wRequest.lock ();

            if (pRequest)
            {
                if (!pRequest->needsUpdate (newRequests, cache->getLedger()->seq()))
                    remove = false;
                else
                {
                    InfoSub::pointer ipSub = pRequest->getSubscriber ();
                    if (ipSub)
                    {
                        ipSub->getConsumer ().charge (Resource::feePathFindUpdate);
                        if (!ipSub->getConsumer ().warn ())
                        {
                            Json::Value update = pRequest->doUpdate (cache, false);
                            pRequest->updateComplete ();
                            update[jss::type] = "path_find";
                            ipSub->send (update, false);
                            remove = false;
                            ++processed;
                        }
                    }
                    else if (pRequest->hasCompletion ())
                    {
                        // One-shot request with completion function
                        pRequest->doUpdate (cache, false);
                        pRequest->updateComplete();
                        ++processed;
                    }
                }
            }

            if (remove)
            {
                ScopedLockType sl (mLock);

                // Remove any dangling weak pointers or weak
                // pointers that refer to this path request.
                auto ret = std::remove_if (
                    mRequests.begin(), mRequests.end(),
                    [&removed,&pRequest](auto const& wl)
                    {
                        auto r = wl.lock();

                        if (r && r != pRequest)
                            return false;
                        ++removed;
                        return true;
                    });

                mRequests.erase (ret, mRequests.end());
            }

            mustBreak = !newRequests && app_.getLedgerMaster().isNewPathRequest();
            if (mustBreak) // We weren't handling new requests and then there was a new request
                break;

        }

        if (mustBreak)
        { // a new request came in while we were working
            newRequests = true;
        }
        else if (newRequests)
        { // we only did new requests, so we always need a last pass
            newRequests = app_.getLedgerMaster().isNewPathRequest();
        }
        else
        { // check if there are any new requests, otherwise we are done
            newRequests = app_.getLedgerMaster().isNewPathRequest();
            if (!newRequests) // We did a full pass and there are no new requests
                return;
        }

        {
            // Get the latest requests, cache, and ledger for next pass
            ScopedLockType sl (mLock);

            if (mRequests.empty())
                break;
            requests = mRequests;

            cache = getLineCache (cache->getLedger(), false);
        }

    }
    while (!shouldCancel ());

    mJournal.debug << "updateAll complete " << processed << " process and " <<
        removed << " removed";
}

void PathRequests::insertPathRequest (PathRequest::pointer const& req)
{
    ScopedLockType sl (mLock);

    // Insert after any older unserviced requests but before
    // any serviced requests
    auto ret = std::find_if (
        mRequests.begin(), mRequests.end(),
        [](auto const& wl)
        {
            auto r = wl.lock();

            // This request has been handled, we come before it
            return r && !r->isNew();
        });

    mRequests.insert (ret, PathRequest::wptr (req));
}

// Make a new-style path_find request
Json::Value
PathRequests::makePathRequest(
    std::shared_ptr <InfoSub> const& subscriber,
    std::shared_ptr<ReadView const> const& inLedger,
    Json::Value const& requestJson)
{
    auto req = std::make_shared<PathRequest> (
        app_, subscriber, ++mLastIdentifier, *this, mJournal);

    auto result = req->doCreate (
        getLineCache (inLedger, false), requestJson);

    if (result.first)
    {
        subscriber->setPathRequest (req);
        insertPathRequest (req);
        app_.getLedgerMaster().newPathRequest();
    }
    return result.second;
}

// Make an old-style ripple_path_find request
Json::Value
PathRequests::makeLegacyPathRequest(
    PathRequest::pointer& req,
    std::function <void (void)> completion,
    std::shared_ptr<ReadView const> const& inLedger,
    Json::Value const& request)
{
    // This assignment must take place before the
    // completion function is called
    req = std::make_shared<PathRequest> (
        app_, completion, ++mLastIdentifier, *this, mJournal);

    auto result = req->doCreate (
        getLineCache (inLedger, false), request);

    if (!result.first)
    {
        req.reset();
    }
    else
    {
        insertPathRequest (req);
        app_.getLedgerMaster().newPathRequest();
    }

    return result.second;
}

} // ripple
