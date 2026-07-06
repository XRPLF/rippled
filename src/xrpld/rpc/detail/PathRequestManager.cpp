#include <xrpld/rpc/detail/PathRequestManager.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/GraphPathfinder.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/PayGraph.h>
#include <xrpld/rpc/detail/PayGraphDelta.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/InfoSub.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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

    // ------------------------------------------------------------------
    // Update the PayGraph incrementally from this ledger's tx metadata.
    // Build it from scratch if it does not exist yet (startup / catchup).
    // Skip entirely when path finding is disabled.
    // ------------------------------------------------------------------
    if (!app_.config().pathSearch)
        return;

    {
        std::scoped_lock const sl(lock_);
        // Prefer a graph built after OrderBookDB's first full scan.  On
        // networked nodes the scan is async; signalOrderBookReady builds once
        // allBooks_ is populated.  Standalone builds immediately.
        bool const empty = !payGraph_ || payGraph_->currentStats().orderBooks == 0;
        if (empty)
        {
            if (!orderBookReady_.load(std::memory_order_acquire) && !app_.config().standalone())
                return;

            payGraph_ = PayGraph::build(
                app_.getOrderBookDB(),
                *inLedger,
                std::nullopt,  // domain
                journal_);
            graphLedgerSeq_ = inLedger->seq();
        }
        else
        {
            // Warm graph: only patch books that had offer activity this ledger.
            std::vector<Book> changedBooks;
            for (auto const& [stTx, stMeta] : inLedger->txs)
            {
                if (!stMeta)
                    continue;
                if (stMeta->isFieldPresent(sfAffectedNodes))
                {
                    auto const& nodes = stMeta->getFieldArray(sfAffectedNodes);
                    mergeBooks(changedBooks, extractChangedBooks(nodes, std::nullopt));
                }
            }
            payGraph_->applyLedgerDelta(app_.getOrderBookDB(), *inLedger, changedBooks);
            graphLedgerSeq_ = inLedger->seq();
        }
    }

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

        for (auto const& wr : requests)
        {
            if (app_.getJobQueue().isStopping())
                break;

            auto request = wr.lock();
            bool remove = true;
            JLOG(journal_.trace()) << "updateAll request " << (request ? "" : "not ") << "found";

            if (request)
            {
                auto continueCallback = [&getSubscriber, &request]() {
                    // This callback is used by doUpdate to determine whether to
                    // continue working. If getSubscriber returns null, that
                    // indicates that this request is no longer relevant.
                    return (bool)getSubscriber(request);
                };
                if (!request->needsUpdate(newRequests, cache->getLedger()->seq()))
                {
                    remove = false;
                }
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
                            json::Value update = request->doUpdate(cache, false, continueCallback);
                            request->updateComplete();
                            update[jss::type] = "path_find";
                            ipSub = getSubscriber(request);
                            if (ipSub)
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
                std::scoped_lock const sl(lock_);

                // Remove any dangling weak pointers or weak
                // pointers that refer to this path request.
                auto ret = std::ranges::remove_if(requests_, [&removed, &request](auto const& wl) {
                    auto r = wl.lock();

                    if (r && r != request)
                        return false;

                    ++removed;
                    return true;
                });

                requests_.erase(ret.begin(), ret.end());
            }

            mustBreak = !newRequests && app_.getLedgerMaster().isNewPathRequest();

            // We weren't handling new requests and then
            // there was a new request
            if (mustBreak)
                break;
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
    // Ensure the PayGraph is built before doCreate runs its fast pass; the
    // async updateAll() that normally builds it won't have run yet on this
    // first call from a fresh subscriber.
    ensurePayGraph(inLedger);

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
    // Ensure the PayGraph is built before doCreate runs its fast pass.
    ensurePayGraph(inLedger);

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

    // Ensure the PayGraph is built/refreshed for this ledger before running
    // the synchronous path-find pass; otherwise the first response would be
    // empty when called before the async updateAll() job runs.
    ensurePayGraph(inLedger);

    auto req =
        std::make_shared<PathRequest>(app_, [] {}, consumer, ++lastIdentifier_, *this, journal_);

    auto [valid, jvRes] = req->doCreate(cache, request);
    if (valid)
        jvRes = req->doUpdate(cache, false);
    return std::move(jvRes);
}

STPathSet
PathRequestManager::findPaths(
    std::shared_ptr<ReadView const> const& ledger,
    AccountID const& srcAccount,
    AccountID const& dstAccount,
    STAmount const& dstAmount,
    PathAsset const& srcAsset,
    std::optional<AccountID> const& srcIssuer,
    std::optional<uint256> const& domain,
    int maxPaths)
{
    if (!ledger)
        return {};

    // Domain payments need a domain-scoped PayGraph because domain-only
    // offers live in OrderBookDB::domainBooks_ rather than allBooks_.
    auto graph = domain ? PayGraph::build(app_.getOrderBookDB(), *ledger, domain, journal_)
                        : ensurePayGraph(ledger);
    if (!graph)
        return {};

    auto cache = std::make_shared<AssetCache>(ledger, app_.getJournal("AssetCache"));

    GraphPathfinder pf(
        graph,
        cache,
        srcAccount,
        dstAccount,
        srcAsset,
        srcIssuer,
        dstAmount,
        std::nullopt,
        domain,
        app_);

    if (!pf.findPaths())
        return {};

    pf.computePathRanks(maxPaths);
    return pf.getBestPaths(maxPaths, STPathSet{}, srcIssuer.value_or(srcAccount));
}

}  // namespace xrpl
