#include <xrpld/rpc/detail/PathRequestManager.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathRequest.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/InfoSub.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace xrpl {

/** Get the current AssetCache, updating it if necessary.
    Get the correct ledger to use.
*/
std::shared_ptr<AssetCache>
PathRequestManager::getAssetCache(std::shared_ptr<ReadView const> const& ledger, bool authoritative)
{
    std::scoped_lock const sl(lock_);

    auto assetCache = assetCache_.lock();

    std::uint32_t const lineSeq = assetCache ? assetCache->getLedger()->seq() : 0;
    std::uint32_t const lgrSeq = ledger->seq();
    JLOG(journal_.debug()) << "getLineCache has cache for " << lineSeq << ", considering "
                           << lgrSeq;

    if ((lineSeq == 0) ||                               // no ledger
        (authoritative && (lgrSeq > lineSeq)) ||        // newer authoritative ledger
        (authoritative && ((lgrSeq + 8) < lineSeq)) ||  // we jumped way back for some reason
        (lgrSeq > (lineSeq + 8)))                       // we jumped way forward for some reason
    {
        JLOG(journal_.debug()) << "getLineCache creating new cache for " << lgrSeq;
        // Assign to the local before the member, because the member is a
        // weak_ptr, and will immediately discard it if there are no other
        // references.
        assetCache_ = assetCache =
            std::make_shared<AssetCache>(ledger, app_.getJournal("AssetCache"));
    }
    return assetCache;
}

void
PathRequestManager::updateAll(std::shared_ptr<ReadView const> const& inLedger)
{
    auto event = app_.getJobQueue().makeLoadEvent(JtPathFind, "PathRequest::updateAll");

    std::vector<PathRequest::wptr> requests;
    std::shared_ptr<AssetCache> cache;

    // Get the ledger and cache we should be using
    {
        std::scoped_lock const sl(lock_);
        requests = requests_;
        cache = getAssetCache(inLedger, true);
    }

    bool newRequests = app_.getLedgerMaster().isNewPathRequest();
    bool mustBreak = false;

    JLOG(journal_.trace()) << "updateAll seq=" << cache->getLedger()->seq() << ", "
                           << requests.size() << " requests";

    int processed = 0, removed = 0;

    auto getSubscriber = [](PathRequest::pointer const& request) -> InfoSub::pointer {
        if (auto ipSub = request->getSubscriber(); ipSub && ipSub->getRequest() == request)
        {
            return ipSub;
        }
        request->doAborting();
        return nullptr;
    };

    do
    {
        JLOG(journal_.trace()) << "updateAll looping";

        struct WorkItem
        {
            PathRequest::pointer request;
            bool isOneShot;
        };
        std::vector<WorkItem> workItems;
        std::vector<PathRequest::pointer> toRemove;
        bool hasExpired = false;

        if (!app_.getJobQueue().isStopping())
        {
            for (auto const& wr : requests)
            {
                auto request = wr.lock();
                JLOG(journal_.trace())
                    << "updateAll request " << (request ? "" : "not ") << "found";

                if (!request)
                {
                    hasExpired = true;
                    continue;
                }

                if (!request->needsUpdate(newRequests, cache->getLedger()->seq()))
                    continue;

                if (auto ipSub = getSubscriber(request))
                {
                    if (!ipSub->getConsumer().warn()) {
                        workItems.push_back({request, false});
                    } else {
                        toRemove.push_back(request);
}
                }
                else if (request->hasCompletion())
                {
                    workItems.push_back({request, true});
                }
                else
                {
                    toRemove.push_back(request);
                }
            }
        }

        struct WorkResult
        {
            PathRequest::pointer request;
            json::Value update;
            bool isOneShot;
        };

        // Use PATH_WORKERS config to cap parallel pathfinding threads.
        std::size_t const maxParallel = std::max(2, app_.config().PATH_WORKERS);

        JLOG(journal_.trace()) << "updateAll processing " << workItems.size()
                               << " requests, parallelism=" << maxParallel;

        std::vector<WorkResult> allResults;
        allResults.reserve(workItems.size());

        for (std::size_t batchStart = 0; batchStart < workItems.size(); batchStart += maxParallel)
        {
            std::size_t const batchEnd = std::min(batchStart + maxParallel, workItems.size());

            std::vector<std::future<WorkResult>> futures;
            futures.reserve(batchEnd - batchStart);

            for (std::size_t i = batchStart; i < batchEnd; ++i)
            {
                auto& item = workItems[i];
                futures.push_back(
                    std::async(
                        std::launch::async,
                        [req = item.request, oneShot = item.isOneShot, &cache, &getSubscriber]()
                            -> WorkResult {
                            std::function<bool(void)> cb;
                            if (!oneShot)
                            {
                                cb = [&getSubscriber, req]() { return (bool)getSubscriber(req); };
                            }
                            json::Value update = req->doUpdate(cache, false, cb);
                            req->updateComplete();
                            return {.request=req, .update=std::move(update), .isOneShot=oneShot};
                        }));
            }

            for (auto& f : futures)
                allResults.push_back(f.get());
        }

        for (auto& result : allResults)
        {
            if (result.isOneShot)
            {
                ++processed;
            }
            else
            {
                result.update[jss::type] = "path_find";
                if (auto ipSub = getSubscriber(result.request))
                {
                    ipSub->send(result.update, false);
                    ++processed;
                }
                else
                {
                    toRemove.push_back(result.request);
                }
            }
        }

        if (!toRemove.empty() || hasExpired)
        {
            std::scoped_lock const sl(lock_);

            auto ret = std::ranges::remove_if(
                requests_, [&removed, &toRemove](auto const& wl) {
                    auto r = wl.lock();
                    if (!r)
                    {
                        ++removed;
                        return true;
                    }
                    if (std::find(toRemove.begin(), toRemove.end(), r) != toRemove.end())
                    {
                        ++removed;
                        return true;
                    }
                    return false;
                });

            requests_.erase(ret.begin(), ret.end());
        }

        mustBreak = !newRequests && app_.getLedgerMaster().isNewPathRequest();

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
        std::shared_ptr<AssetCache> lastCache;
        {
            // Get the latest requests, cache, and ledger for next pass
            std::scoped_lock const sl(lock_);

            if (requests_.empty())
                break;
            requests = requests_;
            lastCache = cache;
            cache = getAssetCache(cache->getLedger(), false);
        }
    } while (!app_.getJobQueue().isStopping());

    JLOG(journal_.debug()) << "updateAll complete: " << processed << " processed and " << removed
                           << " removed";
}

bool
PathRequestManager::requestsPending() const
{
    std::scoped_lock const sl(lock_);
    return !requests_.empty();
}

void
PathRequestManager::insertPathRequest(PathRequest::pointer const& req)
{
    std::scoped_lock const sl(lock_);

    // Insert after any older unserviced requests but before
    // any serviced requests
    auto ret = std::ranges::find_if(requests_, [](auto const& wl) {
        auto r = wl.lock();

        // We come before handled requests
        return r && !r->isNew();
    });

    requests_.emplace(ret, req);
}

// Make a new-style path_find request
json::Value
PathRequestManager::makePathRequest(
    std::shared_ptr<InfoSub> const& subscriber,
    std::shared_ptr<ReadView const> const& inLedger,
    json::Value const& requestJson)
{
    auto req = std::make_shared<PathRequest>(app_, subscriber, ++lastIdentifier_, *this, journal_);

    auto [valid, jvRes] = req->doCreate(getAssetCache(inLedger, false), requestJson);

    if (valid)
    {
        subscriber->setRequest(req);
        insertPathRequest(req);
        app_.getLedgerMaster().newPathRequest();
    }
    return std::move(jvRes);
}

// Make an old-style ripple_path_find request
json::Value
PathRequestManager::makeLegacyPathRequest(
    PathRequest::pointer& req,
    std::function<void(void)> completion,
    Resource::Consumer& consumer,
    std::shared_ptr<ReadView const> const& inLedger,
    json::Value const& request)
{
    // This assignment must take place before the
    // completion function is called
    req = std::make_shared<PathRequest>(
        app_, completion, consumer, ++lastIdentifier_, *this, journal_);

    auto [valid, jvRes] = req->doCreate(getAssetCache(inLedger, false), request);

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
            jvRes = rpcError(RpcTooBusy);
            req.reset();
        }
    }

    return std::move(jvRes);
}

json::Value
PathRequestManager::doLegacyPathRequest(
    Resource::Consumer& consumer,
    std::shared_ptr<ReadView const> const& inLedger,
    json::Value const& request)
{
    auto cache = std::make_shared<AssetCache>(inLedger, app_.getJournal("AssetCache"));

    auto req =
        std::make_shared<PathRequest>(app_, [] {}, consumer, ++lastIdentifier_, *this, journal_);

    auto [valid, jvRes] = req->doCreate(cache, request);
    if (valid)
        jvRes = req->doUpdate(cache, false);
    return std::move(jvRes);
}

}  // namespace xrpl
