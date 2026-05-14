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
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace xrpl {

/** Return the shared AssetCache for the given ledger, rebuilding it when necessary.
 *
 *  The manager holds `assetCache_` as a `weak_ptr` so the cache lives only as
 *  long as at least one `PathRequest` or in-progress update holds a
 *  `shared_ptr` to it. The local `assetCache` variable is populated first and
 *  then stored to `assetCache_`; if the assignment were made in the reverse
 *  order the cache would be immediately destroyed because no other owner would
 *  exist yet.
 *
 *  The cache is rebuilt when any of the following conditions hold:
 *  - No prior cache exists (`lineSeq == 0`).
 *  - `authoritative` is true and `ledger` is strictly newer — the normal
 *    ledger-advance case.
 *  - `authoritative` is true and `ledger` is more than 8 slots *older* than
 *    the cached ledger — indicates a reorg or sync restart.
 *  - `ledger` is more than 8 slots *newer* than the cached ledger — a forward
 *    jump large enough to make the cache meaningfully stale.
 *
 *  The ±8 tolerance avoids unnecessary rebuilds during minor gaps while still
 *  catching drift that would produce incorrect pathfinding results.
 *
 *  @param ledger        The ledger the caller intends to use for pathfinding.
 *  @param authoritative True when called from the main background sweep
 *      (`updateAll`); false for setup or one-shot calls. Only authoritative
 *      callers trigger rebuilds on a normal ledger advance.
 *  @return A `shared_ptr` to the current (possibly freshly built) cache for
 *      `ledger`. Never null.
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

/** Drive one full background pass over all live `path_find` subscriptions.
 *
 *  Called on a `jtPATH_FIND` job-queue thread dispatched by `LedgerMaster`
 *  whenever the validated ledger advances. The method re-snapshots `requests_`
 *  under lock at the start of each pass so that individual `doUpdate` calls —
 *  which can be lengthy — are made without holding the lock.
 *
 *  **Re-entrant loop:** new subscriptions arriving mid-pass are detected via
 *  `LedgerMaster::isNewPathRequest()`. When a new request appears:
 *  - `mustBreak` is set and the current pass aborts early.
 *  - The loop restarts with `newRequests = true` so the newcomer is serviced
 *    promptly rather than waiting for the next ledger close.
 *  - After a pass that started with `newRequests = true`, one additional pass
 *    is always performed to catch any further arrivals during the second pass.
 *  - The loop exits only when a full pass completes with no new requests
 *    detected at its end.
 *
 *  **Subscriber liveness (the `getSubscriber` lambda):** the subscriber weak
 *  pointer is locked and its `getRequest()` is compared against the current
 *  `PathRequest`. If they do not match — indicating the client closed and
 *  reopened a session, replacing the subscriber's current request — the stale
 *  request has `doAborting()` called and the lambda returns `nullptr`.
 *
 *  The subscriber `shared_ptr` is deliberately released (`ipSub.reset()`)
 *  before calling `doUpdate` so that a client disconnecting during the
 *  (potentially long) computation can free its `InfoSub` immediately. After
 *  `doUpdate` returns, `getSubscriber` is called again; if the lock fails the
 *  update result is silently discarded.
 *
 *  **Rate limiting:** if `Consumer::warn()` returns true the update is skipped
 *  for that iteration; the request stays in the queue and is retried on the
 *  next ledger pass.
 *
 *  **Removal:** a request is removed from `requests_` when its weak pointer
 *  is expired, its subscriber has been replaced, or its one-shot callback has
 *  fired. Dangling weak pointers are reaped in the same `remove_if` pass.
 *
 *  @param inLedger The newly validated ledger to use for this sweep.
 */
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

    // Two-part liveness check: the InfoSub weak pointer must be lockable AND
    // its current request must still point to this PathRequest. A mismatch
    // means the client opened a new path_find session; the old request is
    // aborted and nullptr is returned so the caller skips the update.
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

/** Return true if there is at least one active path request queued.
 *
 *  Used by `LedgerMaster` to decide whether a path-find job needs to be
 *  dispatched after a ledger advance.
 */
bool
PathRequestManager::requestsPending() const
{
    std::scoped_lock const sl(lock_);
    return !requests_.empty();
}

/** Insert a new request into `requests_`, ahead of any already-serviced entries.
 *
 *  Maintains the invariant that unserviced (new) requests appear before
 *  already-serviced ones. The insertion point is the first entry where
 *  `!r->isNew()`, so new requests are encountered early during the next
 *  `updateAll` pass and serviced quickly rather than buried behind a long
 *  queue of already-updated subscriptions.
 *
 *  @param req The freshly constructed `PathRequest` to enqueue. Must be
 *      non-null; the caller owns the `shared_ptr` contract.
 */
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

/** Create a subscription-based `path_find` request and push an initial result.
 *
 *  Implements the `path_find` WebSocket command. The request is registered in
 *  `requests_` and the subscriber is notified of updates on every subsequent
 *  ledger advance via `updateAll`. The subscriber holds a weak reference back
 *  to the `PathRequest`; when the client disconnects the next `updateAll` pass
 *  will detect the broken weak pointer and silently discard the request.
 *
 *  If `doCreate` reports the request parameters are invalid, the request is not
 *  registered and the error JSON is returned directly.
 *
 *  @param subscriber  The WebSocket subscriber that will receive push updates.
 *  @param inLedger    Current validated ledger to use for the initial result.
 *  @param requestJson Parsed `path_find` request object from the client.
 *  @return JSON response for the initial `path_find` reply (success or error).
 */
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

/** Register an asynchronous `ripple_path_find` request for background processing.
 *
 *  Implements the legacy `ripple_path_find` coroutine variant. The request is
 *  enqueued in `requests_` and `LedgerMaster::newPathRequest()` is called to
 *  schedule a `jtPATH_FIND` job. The `completion` callback is invoked when
 *  `updateAll` finishes processing this request.
 *
 *  `req` is assigned before `completion` could possibly fire so that the caller
 *  always sees a valid pointer when the callback runs.
 *
 *  On failure (invalid parameters or job queue at capacity) `req` is reset to
 *  `nullptr` and an error JSON is returned. Callers **must** check `req` on
 *  return:
 *  - If `req` is null and the return value is `rpcTOO_BUSY`, the job queue was
 *    full; `LedgerMaster::newPathRequest()` returned false.
 *  - If `req` is null for any other reason, parameter validation failed.
 *
 *  @param req        Out-parameter populated with the new `PathRequest` on
 *      success; reset to `nullptr` on any failure.
 *  @param completion Callback invoked by `updateAll` when the result is ready.
 *      Must be set on `req` before this function returns.
 *  @param consumer   RPC resource consumer for rate-limiting bookkeeping.
 *  @param inLedger   Current validated ledger for the initial path computation.
 *  @param request    Parsed `ripple_path_find` request object from the client.
 *  @return JSON response — either the initial pathfinding result or an error
 *      (`rpcTOO_BUSY` / parameter error).
 */
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

/** Execute a synchronous `ripple_path_find` immediately on the caller's ledger.
 *
 *  Fully synchronous fallback for the `ripple_path_find` command. Creates a
 *  private, ephemeral `AssetCache` bound to `inLedger`, runs `doUpdate`
 *  inline, and returns the result to the caller. The request is **never**
 *  added to `requests_` and never interacts with the background thread or
 *  `LedgerMaster`.
 *
 *  @param consumer  RPC resource consumer for rate-limiting bookkeeping.
 *  @param inLedger  Ledger to use for path computation; typically the current
 *      validated ledger supplied by the handler.
 *  @param request   Parsed `ripple_path_find` request object from the client.
 *  @return JSON result of the path computation, or an error object if the
 *      request parameters are invalid.
 */
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
