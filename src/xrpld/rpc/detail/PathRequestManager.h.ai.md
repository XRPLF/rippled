# `PathRequestManager` — Pathfinding Request Lifecycle Hub

## Role and Context

`PathRequestManager` is the single coordination point for all active pathfinding requests in the XRPL server. When a client wants to find a payment path across the trust-line graph, the request flows through this class. It holds the collection of live `PathRequest` objects, drives their periodic updates as new ledgers close, manages the shared `AssetCache` used during graph traversal, and surfaces timing telemetry to the metrics system.

The class lives in `Application` and is invoked from two directions: RPC handlers that create requests, and `LedgerMaster` which calls `updateAll()` on a dedicated pathfinding thread whenever a new ledger is ready.

## Two Request Flavors

The class exposes three factory methods that reflect two distinct client-facing RPCs with meaningfully different lifecycles.

`makePathRequest()` serves the `path_find` WebSocket command. The client subscribes and expects an ongoing stream of path updates as the ledger evolves. The created `PathRequest` is stored in the subscriber's `InfoSub` slot (`subscriber->setRequest(req)`) and in the manager's weak-pointer list. Ownership lives with the subscriber; the manager only holds a `wptr`. When the subscriber disconnects, the `shared_ptr` is released, the weak pointer expires, and the manager silently drops it on the next `updateAll()` pass.

`makeLegacyPathRequest()` serves `ripple_path_find` via a coroutine. The caller receives a `PathRequest::pointer` (out-parameter) and provides a completion callback that is invoked once the path engine finishes its first pass. The manager inserts the request into the list and signals `LedgerMaster::newPathRequest()` to schedule a pathfinding job. If the job queue is too busy (`newPathRequest()` returns false), the request is torn down immediately and the caller receives `rpcTOO_BUSY`.

`doLegacyPathRequest()` is the synchronous one-shot variant, designed for cases where the caller supplies its own ledger and wants an immediate answer without queuing. It creates a fresh `AssetCache` directly, runs `doCreate()` then `doUpdate()` inline, and returns the result. This path bypasses the manager's request list entirely — the `PathRequest` is constructed, used, and discarded in one call.

## Weak-Pointer Collection and Priority Ordering

The internal `requests_` vector holds `PathRequest::wptr` entries — weak pointers, not shared pointers. This is a deliberate ownership design: the manager does not keep requests alive. If a subscriber disconnects or a coroutine completes, the `shared_ptr` held by the subscriber/coroutine drops to zero, the weak pointer in the list becomes stale, and the manager cleans it up during the next update sweep. This prevents the manager from silently prolonging the lifetime of defunct request objects.

Insertion order matters. `insertPathRequest()` scans forward for the first already-serviced request (one where `isNew()` returns false) and inserts the new request before it. This ensures freshly-created requests are processed first in the upcoming `updateAll()` pass rather than queuing behind older requests that have already received at least one reply.

## `updateAll()` — The Batch Update Loop

This is the most complex method. `LedgerMaster` calls it on each pathfinding thread cycle, passing the current closed ledger. The logic handles a subtle race: new requests can arrive while the loop is running.

The method takes a snapshot of the request list and asset cache under the lock, then iterates without holding the lock (so individual `PathRequest::doUpdate()` calls — which may be expensive graph searches — don't block request insertion). For each live request it checks:

- If the subscriber is still connected and its `InfoSub` still references this request (guarding against a new `path_find` superseding the previous one on the same connection).
- Whether the request `needsUpdate()` for the current ledger index.
- Whether the subscriber's resource consumption warrants a warning (in which case the update is skipped but the request is kept).

After a successful update, the result is tagged with `"type": "path_find"` and sent to the subscriber. If instead the request has a completion function (legacy mode), `doUpdate()` runs and the completion is fired internally.

The outer `do`/`while` loop handles new-request preemption. If `LedgerMaster::isNewPathRequest()` transitions from false to true mid-loop, `mustBreak` is set, the current pass is abandoned, and the loop restarts with `newRequests = true`. This ensures a newly arrived subscription doesn't wait through the full backlog of existing requests before getting its first path result.

## `AssetCache` Lifetime Strategy

`AssetCache` wraps a ledger snapshot and caches the trust-line and MPT data fetched during pathfinding. It is potentially large, and is shared across all requests in a single update pass.

The manager holds it as a `std::weak_ptr<AssetCache>`. A strong reference is returned from `getAssetCache()`, which promotes the weak pointer. The cache is rebuilt when the ledger sequence changes beyond a threshold (more than 8 ledgers ahead or behind). The comment in the implementation is worth noting: the local `shared_ptr` variable is assigned before the member `weak_ptr` to ensure there is at least one strong reference alive before the weak pointer is set — assigning only the `weak_ptr` would cause immediate expiry.

## Concurrency and Telemetry

The `mLock` member is a `std::recursive_mutex`, needed because `updateAll()` calls `getAssetCache()` while holding the lock, and `getAssetCache()` also acquires it internally. The `mLastIdentifier` counter is `std::atomic<int>`, guaranteeing unique per-request IDs without entering the main lock.

`mFast` and `mFull` are `beast::insight::Event` handles. Individual `PathRequest` objects call back through `reportFast()` and `reportFull()` to record how long their quick-reply and full-reply passes took, feeding the server's telemetry collector. This indirection keeps timing instrumentation in the manager rather than having `PathRequest` depend on the collector directly.