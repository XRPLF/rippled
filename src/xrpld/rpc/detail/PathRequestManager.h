/** @file
 *  Coordination hub for all active pathfinding requests.
 *
 *  Declares `PathRequestManager`, which owns (weakly) the live set of
 *  `PathRequest` objects, drives their periodic updates as the ledger
 *  advances, and manages the shared `AssetCache` used during graph traversal.
 */
#pragma once

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/PathRequest.h>
#include <xrpld/rpc/detail/RippleLineCache.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace xrpl {

/** Coordination hub for all active pathfinding requests.
 *
 *  Lives in `Application` as a singleton service. RPC handlers call the
 *  factory methods (`makePathRequest`, `makeLegacyPathRequest`,
 *  `doLegacyPathRequest`) to create new requests; `LedgerMaster` calls
 *  `updateAll()` on a dedicated `jtPATH_FIND` job-queue thread whenever
 *  the validated ledger advances.
 *
 *  **Ownership model:** `requests_` holds `weak_ptr` entries only. The
 *  strong reference lives with the subscriber (`path_find`) or the calling
 *  coroutine (`ripple_path_find`). When either releases it, the weak pointer
 *  expires and the manager silently prunes it on the next `updateAll` sweep.
 *
 *  **Thread safety:** `lock_` is a `recursive_mutex` because `updateAll()`
 *  calls `getAssetCache()` while holding it, and `getAssetCache()` re-acquires
 *  it internally. `lastIdentifier_` is `std::atomic<int>` so unique request
 *  IDs are assigned without entering the main lock.
 */
class PathRequestManager
{
public:
    /** Construct the manager and register telemetry event handles.
     *
     *  Wires up `fast_` and `full_` as named events in the metrics collector
     *  so individual `PathRequest` objects can report their quick-reply and
     *  full-reply latencies via `reportFast()` and `reportFull()`.
     *
     *  @param app       The running application instance.
     *  @param journal   Logging sink used by the manager and forwarded to
     *      constructed `PathRequest` objects.
     *  @param collector Metrics collector used to create the
     *      `pathfind_fast` and `pathfind_full` event handles.
     */
    PathRequestManager(
        Application& app,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
        : app_(app), journal_(journal), lastIdentifier_(0)
    {
        fast_ = collector->makeEvent("pathfind_fast");
        full_ = collector->makeEvent("pathfind_full");
    }

    /** Drive one full background pass over all live `path_find` subscriptions.
     *
     *  Called on a `jtPATH_FIND` job-queue thread by `LedgerMaster` each time
     *  the validated ledger advances. Snapshots `requests_` under lock, then
     *  iterates without the lock so individual `doUpdate` calls do not block
     *  new-request insertion.
     *
     *  The outer loop re-runs if a new subscription arrives mid-pass
     *  (`LedgerMaster::isNewPathRequest()` transitions to true), so freshly
     *  created subscriptions are never delayed by the full backlog of existing
     *  ones.
     *
     *  Expired weak pointers and requests whose subscribers have been replaced
     *  are removed from `requests_` during the sweep.
     *
     *  @param inLedger The newly validated ledger to use for this sweep.
     */
    void
    updateAll(std::shared_ptr<ReadView const> const& inLedger);

    /** Return true if there is at least one active path request queued.
     *
     *  Used by `LedgerMaster` to decide whether a path-find job should be
     *  dispatched after a ledger advance.
     *
     *  @return `true` when `requests_` is non-empty; `false` otherwise.
     */
    bool
    requestsPending() const;

    /** Return the shared `AssetCache` for the given ledger, rebuilding when stale.
     *
     *  The cache is held as a `weak_ptr` and lives only as long as a strong
     *  reference exists elsewhere (e.g. in an in-progress `updateAll` pass or
     *  a `doLegacyPathRequest` call). The cache is rebuilt when:
     *  - No prior cache exists.
     *  - `authoritative` is true and `ledger` is strictly newer than the
     *    cached sequence.
     *  - `authoritative` is true and `ledger` is more than 8 sequences
     *    behind (reorg or sync restart).
     *  - `ledger` is more than 8 sequences ahead (large forward jump).
     *
     *  @note The local `shared_ptr` is assigned before the member `weak_ptr`
     *      to ensure at least one strong reference exists before the weak
     *      pointer is set; assigning only the `weak_ptr` would cause immediate
     *      expiry.
     *
     *  @param ledger        The ledger the caller intends to use for
     *      pathfinding.
     *  @param authoritative `true` when called from the main background sweep
     *      (`updateAll`); `false` for setup or one-shot calls. Only
     *      authoritative callers trigger rebuilds on a normal ledger advance.
     *  @return A `shared_ptr` to the current (possibly freshly built) cache.
     *      Never null.
     */
    std::shared_ptr<AssetCache>
    getAssetCache(std::shared_ptr<ReadView const> const& ledger, bool authoritative);

    /** Create a subscription-based `path_find` request and return the initial result.
     *
     *  Implements the `path_find create` WebSocket command. The request is
     *  registered in `requests_` and the subscriber will receive push updates
     *  on every subsequent ledger advance via `updateAll`. Ownership of the
     *  `PathRequest` lives with the subscriber's `InfoSub` slot; the manager
     *  holds only a weak pointer.
     *
     *  If `doCreate` reports invalid parameters, the request is not registered
     *  and the error JSON is returned directly.
     *
     *  @param subscriber  The WebSocket subscriber that will receive push
     *      updates.
     *  @param ledger      Current validated ledger used for the initial result.
     *  @param request     Parsed `path_find create` request object from the
     *      client.
     *  @return JSON response for the initial `path_find` reply (success or
     *      error).
     */
    json::Value
    makePathRequest(
        std::shared_ptr<InfoSub> const& subscriber,
        std::shared_ptr<ReadView const> const& ledger,
        json::Value const& request);

    /** Register an asynchronous `ripple_path_find` request for background processing.
     *
     *  Implements the legacy `ripple_path_find` coroutine variant. The request
     *  is enqueued in `requests_` and `LedgerMaster::newPathRequest()` is
     *  called to schedule a `jtPATH_FIND` job. The `completion` callback is
     *  invoked by `updateAll` when processing finishes.
     *
     *  On failure (invalid parameters or job queue at capacity) `req` is reset
     *  to `nullptr` and an error JSON is returned. Callers must check `req`
     *  after return: a null `req` with `rpcTOO_BUSY` means the job queue was
     *  full; any other null indicates a parameter validation failure.
     *
     *  @param req        Out-parameter set to the new `PathRequest` on success;
     *      reset to `nullptr` on any failure.
     *  @param completion Callback invoked by `updateAll` when the single
     *      pathfinding pass finishes; fired at most once.
     *  @param consumer   RPC resource consumer for rate-limiting bookkeeping.
     *  @param inLedger   Current validated ledger for the initial path
     *      computation.
     *  @param request    Parsed `ripple_path_find` request object from the
     *      client.
     *  @return JSON response — either the initial pathfinding result or an
     *      error (`rpcTOO_BUSY` or parameter error).
     */
    json::Value
    makeLegacyPathRequest(
        PathRequest::pointer& req,
        std::function<void(void)> completion,
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        json::Value const& request);

    /** Execute a synchronous `ripple_path_find` immediately on the caller's ledger.
     *
     *  Fully synchronous, one-shot variant. Creates a private ephemeral
     *  `AssetCache` bound to `inLedger`, runs `doCreate` then `doUpdate`
     *  inline, and returns the result. The `PathRequest` is never added to
     *  `requests_` and never interacts with the background thread or
     *  `LedgerMaster`.
     *
     *  @param consumer  RPC resource consumer for rate-limiting bookkeeping.
     *  @param inLedger  Ledger to use for path computation; typically the
     *      current validated ledger supplied by the handler.
     *  @param request   Parsed `ripple_path_find` request object from the
     *      client.
     *  @return JSON result of the path computation, or an error object if
     *      request parameters are invalid.
     */
    json::Value
    doLegacyPathRequest(
        Resource::Consumer& consumer,
        std::shared_ptr<ReadView const> const& inLedger,
        json::Value const& request);

    /** Record the latency of a quick-reply (fast) pathfinding pass.
     *
     *  Called by individual `PathRequest` objects after their first
     *  `PATH_SEARCH_FAST` pass completes. Feeds the `pathfind_fast` telemetry
     *  event, keeping timing instrumentation in the manager rather than
     *  requiring `PathRequest` to depend on the collector directly.
     *
     *  @param ms Elapsed time of the fast pass.
     */
    void
    reportFast(std::chrono::milliseconds ms)
    {
        fast_.notify(ms);
    }

    /** Record the latency of a full pathfinding pass.
     *
     *  Called by individual `PathRequest` objects after their first full
     *  (non-fast) pass completes. Feeds the `pathfind_full` telemetry event.
     *
     *  @param ms Elapsed time of the full pass.
     */
    void
    reportFull(std::chrono::milliseconds ms)
    {
        full_.notify(ms);
    }

private:
    /** Insert a new request into `requests_`, ahead of any already-serviced entries.
     *
     *  Scans forward for the first entry where `!r->isNew()` and inserts
     *  before it, so freshly-created requests are processed early in the
     *  upcoming `updateAll` pass rather than queuing behind older requests
     *  that already have a cached result.
     *
     *  @param req The freshly constructed `PathRequest` to enqueue.
     */
    void
    insertPathRequest(PathRequest::pointer const&);

    Application& app_;
    beast::Journal journal_;

    /** Telemetry event handle for quick-reply (fast) pass latency. */
    beast::insight::Event fast_;

    /** Telemetry event handle for full pass latency. */
    beast::insight::Event full_;

    /** Weak-pointer collection of all live requests.
     *
     *  The manager does not own the requests; strong references live with
     *  subscribers (`path_find`) or calling coroutines (`ripple_path_find`).
     *  Expired entries are pruned during `updateAll`.
     */
    std::vector<PathRequest::wptr> requests_;

    /** Shared trust-line and MPT cache for the current update pass.
     *
     *  Held as a `weak_ptr` so the cache is released when no `PathRequest`
     *  or in-progress update holds a strong reference.
     */
    std::weak_ptr<AssetCache> assetCache_;

    /** Monotonically increasing counter for assigning unique request IDs.
     *
     *  Incremented with `operator++` before each `PathRequest` construction;
     *  `std::atomic` ensures uniqueness without acquiring `lock_`.
     */
    std::atomic<int> lastIdentifier_;

    /** Guards `requests_` and `assetCache_`.
     *
     *  Recursive because `updateAll` calls `getAssetCache` while holding
     *  this lock and `getAssetCache` re-acquires it internally.
     */
    std::recursive_mutex mutable lock_;
};

}  // namespace xrpl
