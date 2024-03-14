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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/basics/Log.h>
#include <ripple/core/JobQueue.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <algorithm>

namespace ripple {

/** Get the current RippleLineCache, updating it if necessary.
    Get the correct ledger to use.
*/
std::shared_ptr<RippleLineCache>
PathRequests::getLineCache(
    std::shared_ptr<ReadView const> const& ledger,
    bool authoritative)
{
    std::lock_guard sl(mLock);

    auto lineCache = lineCache_.lock();

    std::uint32_t const lineSeq = lineCache ? lineCache->getLedger()->seq() : 0;
    std::uint32_t const lgrSeq = ledger->seq();
    JLOG(mJournal.debug()) << "getLineCache has cache for " << lineSeq
                           << ", considering " << lgrSeq;

    if ((lineSeq == 0) ||                         // no ledger
        (authoritative && (lgrSeq > lineSeq)) ||  // newer authoritative ledger
        (authoritative &&
         ((lgrSeq + 8) < lineSeq)) ||  // we jumped way back for some reason
        (lgrSeq > (lineSeq + 8)))      // we jumped way forward for some reason
    {
        JLOG(mJournal.debug())
            << "getLineCache creating new cache for " << lgrSeq;
        // Assign to the local before the member, because the member is a
        // weak_ptr, and will immediately discard it if there are no other
        // references.
        lineCache_ = lineCache = std::make_shared<RippleLineCache>(
            ledger, app_.journal("RippleLineCache"));
    }
    return lineCache;
}

void
PathRequests::updateAll(std::shared_ptr<ReadView const> const& inLedger)
{
    auto event =
        app_.getJobQueue().makeLoadEvent(jtPATH_FIND, "PathRequest::updateAll");

    std::vector<PathRequest::wptr> requests;
    std::shared_ptr<RippleLineCache> cache;

    // Get the ledger and cache we should be using
    {
        std::lock_guard sl(mLock);
        requests = requests_;
        cache = getLineCache(inLedger, true);
    }

    bool newRequests = app_.getLedgerMaster().isNewPathRequest();
    bool mustBreak = false;

    JLOG(mJournal.trace()) << "updateAll seq=" << cache->getLedger()->seq()
                           << ", " << requests.size() << " requests";

    int processed = 0, removed = 0;

    auto getSubscriber =
        [](PathRequest::pointer const& request) -> InfoSub::pointer {
        if (auto ipSub = request->getSubscriber();
            ipSub && ipSub->getRequest() == request)
        {
            return ipSub;
        }
        request->doAborting();
        return nullptr;
    };

    do
    {
        JLOG(mJournal.trace()) << "updateAll looping";
        for (auto const& wr : requests)
        {
            if (app_.getJobQueue().isStopping())
                break;

            auto request = wr.lock();
            bool remove = true;
            JLOG(mJournal.trace())
                << "updateAll request " << (request ? "" : "not ") << "found";

            if (request)
            {
                auto continueCallback = [&getSubscriber, &request]() {
                    // This callback is used by doUpdate to determine whether to
                    // continue working. If getSubscriber returns null, that
                    // indicates that this request is no longer relevant.
                    return (bool)getSubscriber(request);
                };
                if (!request->needsUpdate(
                        newRequests, cache->getLedger()->seq()))
                    remove = false;
                else
                {
                    if (auto ipSub = getSubscriber(request))
                    {
                        if (!ipSub->getConsumer().warn())
                        {
                            // Release the shared ptr to the subscriber so that
                            // it can be freed if the client disconnects, and
                            // thus fail to lock later.
                            ipSub.reset();
                            Json::Value update = request->doUpdate(
                                cache, false, continueCallback);
                            request->updateComplete();
                            update[jss::type] = "path_find";
                            if ((ipSub = getSubscriber(request)))
                            {
                                ipSub->send(update, false);
                                remove = false;
                                ++processed;
                            }
                        }
                    }
                    else if (request->hasCompletion())
                    {
                        // One-shot request with completion function
                        request->doUpdate(cache, false);
                        request->updateComplete();
                        ++processed;
                    }
                }
            }

            if (remove)
            {
                std::lock_guard sl(mLock);

                // Remove any dangling weak pointers or weak
                // pointers that refer to this path request.
                auto ret = std::remove_if(
                    requests_.begin(),
                    requests_.end(),
                    [&removed, &request](auto const& wl) {
                        auto r = wl.lock();

                        if (r && r != request)
                            return false;
                        ++removed;
                        return true;
                    });

                requests_.erase(ret, requests_.end());
            }

            mustBreak =
                !newRequests && app_.getLedgerMaster().isNewPathRequest();

            // We weren't handling new requests and then
            // there was a new request
            if (mustBreak)
                break;
        }

        if (mustBreak)
        {  // a new request came in while we were working
            newRequests = true;
        }
        else if (newRequests)
        {  // we only did new requests, so we always need a last pass
            newRequests = app_.getLedgerMaster().isNewPathRequest();
        }
        else
        {  // if there are no new requests, we are done
            newRequests = app_.getLedgerMaster().isNewPathRequest();
            if (!newRequests)
                break;
        }

        // Hold on to the line cache until after the lock is released, so it can
        // be destroyed outside of the lock
        std::shared_ptr<RippleLineCache> lastCache;
        {
            // Get the latest requests, cache, and ledger for next pass
            std::lock_guard sl(mLock);

            if (requests_.empty())
                break;
            requests = requests_;
            lastCache = cache;
            cache = getLineCache(cache->getLedger(), false);
        }
    } while (!app_.getJobQueue().isStopping());

    JLOG(mJournal.debug()) << "updateAll complete: " << processed
                           << " processed and " << removed << " removed";
}

bool
PathRequests::requestsPending() const
{
    std::lock_guard sl(mLock);
    return !requests_.empty();
}

void
PathRequests::insertPathRequest(PathRequest::pointer const& req)
{
    std::lock_guard sl(mLock);

    // Insert after any older unserviced requests but before
    // any serviced requests
    auto ret =
        std::find_if(requests_.begin(), requests_.end(), [](auto const& wl) {
            auto r = wl.lock();

            // We come before handled requests
            return r && !r->isNew();
        });

    requests_.emplace(ret, req);
}

// Make a new-style path_find request
Json::Value
PathRequests::makePathRequest(
    std::shared_ptr<InfoSub> const& subscriber,
    std::shared_ptr<ReadView const> const& inLedger,
    Json::Value const& requestJson)
{
    auto req = std::make_shared<PathRequest>(
        app_, subscriber, ++mLastIdentifier, *this, mJournal);

    auto [valid, jvRes] =
        req->doCreate(getLineCache(inLedger, false), requestJson);

    if (valid)
    {
        subscriber->setRequest(req);
        insertPathRequest(req);
        app_.getLedgerMaster().newPathRequest();
    }
    return std::move(jvRes);
}

// Make an old-style ripple_path_find request
Json::Value
PathRequests::makeLegacyPathRequest(
    PathRequest::pointer& req,
    std::function<void(void)> completion,
    Resource::Consumer& consumer,
    std::shared_ptr<ReadView const> const& inLedger,
    Json::Value const& request)
{
    // This assignment must take place before the
    // completion function is called
    req = std::make_shared<PathRequest>(
        app_, completion, consumer, ++mLastIdentifier, *this, mJournal);

    auto [valid, jvRes] = req->doCreate(getLineCache(inLedger, false), request);

    if (!valid)
    {
        req.reset();
    }
    else
    {
        insertPathRequest(req);
        if (!app_.getLedgerMaster().newPathRequest())
        {
            // The newPathRequest failed.  Tell the caller.
            jvRes = rpcError(rpcTOO_BUSY);
            req.reset();
        }
    }

    return std::move(jvRes);
}

Json::Value
PathRequests::doLegacyPathRequest(
    Resource::Consumer& consumer,
    std::shared_ptr<ReadView const> const& inLedger,
    Json::Value const& request)
{
    auto cache = std::make_shared<RippleLineCache>(
        inLedger, app_.journal("RippleLineCache"));

    auto req = std::make_shared<PathRequest>(
        app_, [] {}, consumer, ++mLastIdentifier, *this, mJournal);

    auto [valid, jvRes] = req->doCreate(cache, request);
    if (valid)
        jvRes = req->doUpdate(cache, false);
    return std::move(jvRes);
}

}  // namespace ripple
