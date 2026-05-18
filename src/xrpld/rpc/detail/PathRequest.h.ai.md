# PathRequest.h — Per-request Pathfinding State Machine

`PathRequest` is the object that backs a single in-flight payment-path query on the XRPL node. It bridges the RPC layer — where clients ask "how can I pay X to account Y?" — and the low-level `Pathfinder`/`RippleCalc` machinery that searches the ledger's trust-line and order-book graph. The class header is deliberately narrow; almost all logic lives in the paired `.cpp`.

## Two Operational Modes

The dual constructors reflect two distinct client-facing APIs with very different lifetime semantics:

**`path_find` mode** (WebSocket subscription): The first constructor takes a `shared_ptr<InfoSub>` subscriber. The node keeps the `PathRequest` alive for the duration of the subscription and re-runs `doUpdate()` on every ledger close, pushing new results to the subscriber. The subscriber holds the only strong reference; `PathRequestManager` stores only `weak_ptr<PathRequest>` references in its `requests_` vector, so the request is automatically cleaned up when the WebSocket connection drops.

**`ripple_path_find` mode** (legacy one-shot): The second constructor takes a `std::function<void(void)>` completion callback and a `Resource::Consumer` reference directly. No subscriber exists; the request runs once, calls the completion function, and the coroutine that holds the strong pointer lets it go. `hasCompletion()` and the presence of `fCompletion` distinguish this mode throughout the implementation.

The comment in the header — *"The request issuer must maintain a strong pointer"* — is a hard ownership invariant. `PathRequestManager` never prevents garbage collection; it is entirely the calling context's responsibility.

## Lifecycle and State Transitions

`doCreate()` is called immediately after construction. It runs `parseJson()` to validate and internalize the client's JSON parameters (source account, destination account, destination amount, optional source currencies, optional send max, optional domain), then `isValid()` to check ledger-level preconditions (accounts exist, amount meets reserve, etc.). For WebSocket mode only, it then calls `doUpdate(cache, /*fast=*/true)` to produce a quick preliminary result before the full search runs on the background thread.

`needsUpdate()` is the scheduler gate called by `PathRequestManager::updateAll()`. It atomically checks `mInProgress` and `mLastIndex` under `mIndexLock`, setting `mInProgress = true` only if the request is eligible. This prevents two threads from running the same request simultaneously. `updateComplete()` clears `mInProgress` and fires the one-shot completion callback (erasing it afterward to prevent double-firing).

## Path Search: Fast vs. Full, Level Adaptation

`doUpdate()` runs the actual pathfinding. The `fast` flag distinguishes a quick preliminary pass from a complete search. The `iLevel` integer controls how deeply `Pathfinder` explores the graph on each invocation. It adapts on every call:

- On the first pass: `PATH_SEARCH_FAST` if the node is under load or if this is a fast pass; `PATH_SEARCH` otherwise.
- Transitioning from fast to normal: bumps to `PATH_SEARCH`, then decrements by one if the server is loaded.
- On subsequent updates: increments toward `PATH_SEARCH_MAX` when the server is idle and the last search failed; decrements when loaded or when the search previously succeeded.

This adaptive throttle is a deliberate backpressure mechanism — expensive graph searches are scaled back under CPU pressure so pathfinding doesn't starve consensus.

`quick_reply_` and `full_reply_` timestamps are set on the first fast and first full completions respectively, and reported to `PathRequestManager` via `reportFast()` / `reportFull()` for metrics collection.

## findPaths(): Source Enumeration and RippleCalc Integration

`findPaths()` does three things. First, it collects the set of source assets: from explicit `source_currencies` in the request, from `send_max`'s asset type, or auto-discovered by querying the source account's trust lines and MPT holdings (capped by `RPC::Tuning::max_auto_src_cur`). Second, for each source asset it calls `getPathFinder()`, which lazily constructs and caches a `Pathfinder` in a local `hash_map<PathAsset, unique_ptr<Pathfinder>>`. Third, it runs `path::RippleCalc::rippleCalculate()` to simulate the payment end-to-end and obtain actual `source_amount` figures.

`getPathFinder()` constructs the `Pathfinder`, calls `findPaths(level, continueCallback)` and `computePathRanks(max_paths_)` on it. If `findPaths` fails (bad request — e.g., no usable trust lines), the `unique_ptr` is reset to null and that source asset is silently skipped. The pathfinder map is local to the `findPaths()` call stack; `mContext` is the persistent state, holding the best `STPathSet` per asset across updates so `getBestPaths()` can use previously discovered paths as seeds.

The retry path is worth noting: if `RippleCalc` returns `terNO_LINE` or `tecPATH_PARTIAL` and `Pathfinder` produced a `fullLiquidityPath` (a single path that consumes all available liquidity), `findPaths()` appends it and re-runs `rippleCalculate`. This recovers from the case where the ranked paths alone are insufficient but the liquidity path covers the gap.

## Resource Accounting

The `Resource::Consumer` charge is quadratic in the number of source currencies evaluated: `clamp((size * size) + 34, 50, 400)`. This reflects the super-linear computational cost of adding more source assets — each new asset requires its own `Pathfinder` graph traversal — and caps both the minimum (50) and maximum (400) charge per update cycle.

## Concurrency Design

Two separate `std::recursive_mutex` fields serve distinct purposes. `mLock` protects `jvStatus` — the cached JSON result returned to callers of `doStatus()` and `doClose()`. `mIndexLock` protects the scheduling fields `mLastIndex` and `mInProgress`. Keeping them separate avoids holding the broader `mLock` during potentially long operations in `doUpdate()` while still serializing scheduler state checks. `recursive_mutex` is used (rather than plain `mutex`) because completion callbacks may re-enter the lock through the call chain.

The `continueCallback` passed to `doUpdate()` and propagated through `findPaths()`, `getPathFinder()`, and into `Pathfinder` itself allows the `PathRequestManager` to abort a long-running search mid-flight — for example, when a new ledger closes and the current search is already stale.

## Constants and Limits

`max_paths_ = 4` is a compile-time cap on the number of alternative paths returned per source currency. It balances practical usefulness (wallets rarely need more than four options) against response payload size and the cost of `computePathRanks`. The `PFR_PJ_INVALID` / `PFR_PJ_NOCHANGE` preprocessor constants used in `parseJson()`'s return value are a relic of the original C-style API; `PFR_PJ_NOCHANGE` (0) serves as the success return since there is no separate "changed" state to signal.