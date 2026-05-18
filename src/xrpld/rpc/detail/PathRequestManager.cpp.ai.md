# PathRequestManager.cpp

`PathRequestManager` is the central coordinator for all live payment pathfinding requests in rippled. Payment pathfinding is inherently expensive — it involves graph traversal over trust lines and order books — so this class exists to batch and amortize that work: a shared `AssetCache` is built once per ledger and reused across all concurrent client requests, and a background thread processes the full request list whenever the validated ledger advances.

## The Three Pathfinding Modes

The class exposes three distinct entry points that correspond to two different RPC APIs and their operating models:

**`makePathRequest`** is the subscription-based `path_find` WebSocket command. A client registers interest and is pushed an update every time a new ledger validates. The `PathRequest` holds a weak reference back to the `InfoSub` subscriber. When `updateAll` fires, it re-checks liveness via the `getSubscriber` lambda before each update, allowing the manager to silently discard requests whose clients have disconnected without any explicit cleanup message from the client.

**`makeLegacyPathRequest`** is the asynchronous variant of the old `ripple_path_find` command. It registers the request with the manager and signals `LedgerMaster` via `newPathRequest()` to schedule a background path-find pass. A completion callback is invoked when the update is done. If `newPathRequest()` returns false (job queue at capacity), the method immediately returns `rpcTOO_BUSY` and resets the request pointer — the caller must handle this gracefully.

**`doLegacyPathRequest`** is the fully synchronous fallback. It creates a fresh, ephemeral `AssetCache` bound to the caller's ledger and runs `doUpdate` immediately without registering any persistent state. It never enters the `requests_` vector and never interacts with the background thread.

## AssetCache Lifecycle: Deliberate Weak Ownership

The manager holds `assetCache_` as a `std::weak_ptr<AssetCache>`, not a `shared_ptr`. This is an intentional design decision explained by a comment in `getAssetCache`: if the member were a `shared_ptr`, the cache would be kept alive even after all requests needing it had finished. Instead, the cache lives only as long as there is at least one outstanding path request or in-progress update holding a `shared_ptr` to it. In `getAssetCache`, the new cache is first assigned to a local `shared_ptr` and only then stored to `assetCache_` — if it were assigned directly to the weak member first, it would be immediately destroyed since no other owner exists yet.

## Cache Invalidation in `getAssetCache`

The `authoritative` parameter distinguishes whether the caller is driving the main background sweep (authoritative) or is a one-shot or setup call (non-authoritative). The cache is rebuilt under four conditions:

1. No prior cache exists (`lineSeq == 0`).
2. An authoritative call presents a strictly newer ledger — the normal advance case.
3. An authoritative call presents a ledger more than 8 slots *older* than the cached one — a backward jump indicating chain reorganization or a sync restart.
4. Any call presents a ledger more than 8 slots *newer* than the cached one — a forward jump that would make the cache stale by too large a margin.

The ±8 tolerance prevents rebuilding the cache on every minor ledger gap during initial sync or fast validation while still catching situations where the cache would become meaningfully incorrect.

## The `updateAll` Processing Loop

`updateAll` runs on a `jtPATH_FIND` job queue thread dispatched by `LedgerMaster`. The central challenge it solves is that new `path_find` subscriptions can arrive while the existing queue is being processed. If the loop processed each request once and exited, a client subscribing near the end of the pass would wait an entire ledger close before seeing a first result. The loop handles this with a re-entrant structure driven by `LedgerMaster::isNewPathRequest()`:

- If a new request arrives mid-pass (detected by comparing the `newRequests` flag before and after iterating), `mustBreak` is set and the loop restarts from the beginning with `newRequests = true`, ensuring the newcomer is processed promptly.
- If the pass started with `newRequests = true`, it performs one more pass after draining to handle any requests that arrived during the second pass.
- The loop exits only when no new requests appeared during the last full pass.

The request list is copied into a local vector under lock, and the lock is released before iterating. This keeps the critical section minimal — individual `doUpdate` calls can be lengthy, and holding the lock would block `insertPathRequest` from adding new subscriptions.

## Subscriber Validity and the `getSubscriber` Lambda

The `getSubscriber` lambda enforces a two-part liveness check: the `InfoSub` weak pointer must be lockable *and* `ipSub->getRequest()` must still point to the same `PathRequest`. The second check handles the case where a client closes and reopens a `path_find` session in quick succession. In that scenario the old `PathRequest` still has a valid `InfoSub`, but the subscriber's "current request" has been replaced by the new one. The lambda catches this mismatch and calls `request->doAborting()` to clean up the stale request.

Just before calling `doUpdate`, the code explicitly calls `ipSub.reset()` to release the subscriber reference. This is intentional: if the client disconnects during the (potentially long) path computation, the `InfoSub` can be freed immediately rather than being kept alive by the local shared pointer for the duration of the update. After `doUpdate` returns, `getSubscriber` is called again to acquire a fresh pointer; if that second lock fails, the update result is silently discarded.

## Request Ordering and Backpressure

`insertPathRequest` maintains a sorted invariant in `requests_`: new (unserviced) requests are inserted before already-serviced ones. It finds the first request where `!r->isNew()` using `std::find_if` and inserts immediately before it. This ensures that during a pass started because of new arrivals, those new requests are encountered early and serviced quickly rather than buried behind a large queue of already-updated subscriptions.

Rate limiting is integrated at the `Consumer::warn()` check point in `updateAll`. If the RPC resource manager considers the client to be generating excessive load, the update cycle is skipped for that iteration, but the request is retained in the queue and will be retried on the next ledger pass. This gives the rate limiter a natural integration point without requiring any separate eviction mechanism.