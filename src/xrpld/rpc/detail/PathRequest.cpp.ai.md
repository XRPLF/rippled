# `PathRequest.cpp` — Per-Request Pathfinding State Machine

`PathRequest` represents a single client's request to discover viable payment paths across the XRP Ledger's trust-line graph. It is the computational unit that `PathRequestManager` schedules, tracks, and updates: one object per outstanding `path_find` subscription or per-call `ripple_path_find` invocation. The file contains everything from JSON parsing and ledger-state validation through the actual pathfinding loop and `RippleCalc` cost estimation.

## Two Constructor Flavors, One State Machine

The class has two public constructors, each representing a distinct API contract. The subscription constructor (`path_find` semantics) stores a `std::weak_ptr<InfoSub>` to the connected client and has no completion callback — results are pushed on every ledger close. The legacy constructor (`ripple_path_find` semantics) accepts a `std::function<void(void)>` callback and a `Resource::Consumer` reference directly, with the intention that it runs once and fires the callback when done. `hasCompletion()` is the runtime predicate used throughout the file to branch on which mode is active, most visibly in `doCreate` (which skips the initial fast pass for legacy requests) and in `findPaths` (which adds `paths_canonical` to the JSON output for the old API).

Holding the subscriber as a `weak_ptr` is a deliberate choice: the subscriber can be destroyed by the network layer at any time, and `PathRequest` must not prevent that cleanup. `getSubscriber()` re-locks the weak pointer on demand; a null return means the subscription is gone.

## Two-Phase Initialization: `doCreate` → `doUpdate`

`doCreate` is the entry point called by `PathRequestManager` when a client first submits a request. It chains `parseJson` → `isValid`, and — only for subscription mode — immediately calls `doUpdate(cache, true)` for a fast preliminary answer. This "fast" pass uses `PATH_SEARCH_FAST` depth, giving the client something useful in the same request/response cycle before the background engine later runs a deeper search. If either parsing or validation fails, the error is embedded in `jvStatus` and returned to the caller; the request is effectively dead before it is ever queued.

## Parsing: `parseJson`

`parseJson` is strictly syntactic and structural. It enforces the presence of `source_account`, `destination_account`, and `destination_amount`; decodes them into `AccountID` and `STAmount`; and handles the optional `send_max`, `source_currencies`, and `domain` fields. Several non-obvious rules are codified here:

- `send_max` is only legal when `destination_amount` is the "convert all" sentinel (value `-1`, represented as `STAmount` with max-flag). This enforces the invariant that `send_max` is a source-side cap on a "deliver as much as possible" payment.
- Source currencies can be specified as either traditional `currency`/`issuer` pairs or as `mpt_issuance_id` hex strings, reflecting XRPL's newer Multi-Purpose Token standard. The `PathAsset` variant type and `sciSourceAssets` set (of type `std::set<Asset>`) hold both kinds without separate code paths.
- When `send_max` is present, the source-currency list is filtered to only the asset matching `send_max`. Issuer reconciliation logic handles cases where neither the explicit issuer nor the `send_max` issuer is the source account.
- The `domain` parameter (a 256-bit identifier) restricts pathfinding to a defined permissioned domain. It is parsed and stored as `std::optional<uint256>` and threaded through to both `Pathfinder` construction and `RippleCalc::rippleCalculate`.

Return values use preprocessor constants (`PFR_PJ_INVALID`, `PFR_PJ_NOCHANGE`) rather than an enum, which is a legacy holdover from before the code was unified with MPT support.

## Ledger-State Validation: `isValid`

`isValid` runs against a live `AssetCache` (a snapshot of the current ledger). It confirms that the source account exists and that, if the destination is new, the payment is XRP and meets the reserve. For an existing destination account, it populates `destination_currencies` in `jvStatus` via `accountDestAssets`, respecting the `lsfDisallowXRP` flag. This output is consumed by the client to build a currency picker UI without a separate RPC call. `isValid` is called both during `doCreate` and at the start of every `doUpdate`, because ledger state can change between updates.

## Core Pathfinding Loop: `findPaths`

`findPaths` is the heart of the file. It determines the effective set of source assets: explicit (`sciSourceAssets`), derived from `send_max`, or automatically enumerated from the source account's holdings via `accountSourceAssets` — capped at `RPC::Tuning::max_auto_src_cur` (88) to prevent runaway work. When source and destination are the same account, assets matching the destination amount are excluded.

For each candidate source asset, `getPathFinder` either retrieves or constructs a `Pathfinder` for that asset from a local `hash_map` keyed by `PathAsset`. The map lives only for the duration of one `findPaths` call, so `Pathfinder` objects are never reused across ledger updates — each update gets a fresh view of the graph. The `Pathfinder` runs `findPaths` at the configured depth level, then `computePathRanks` to score candidates, limiting results to `max_paths_` (hard-coded at 4).

`getBestPaths` returns a `STPathSet` plus a `fullLiquidityPath` — a single path that may unlock more liquidity but was too expensive to include in the ranked set. The code then calls `path::RippleCalc::rippleCalculate` on a `PaymentSandbox` (an ephemeral, non-committing view of the ledger) to get realistic `actualAmountIn`/`actualAmountOut` estimates. If the calculation fails with `terNO_LINE` or `tecPATH_PARTIAL` and a `fullLiquidityPath` exists, it retries with that path appended — a two-shot fallback that often rescues otherwise partial paths. This retry is intentionally skipped for `convert_all_` mode because partial payments are already allowed there.

The resource fee charged to the client follows the formula `clamp(size² + 34, 50, 400)`, where `size` is the number of source assets evaluated. The quadratic term captures the fact that path complexity grows super-linearly with source currencies; the clamp keeps the cost in a [50, 400] range regardless of edge cases.

## Adaptive Search Depth: `doUpdate` and `iLevel`

`doUpdate` manages `iLevel`, the search depth passed to `Pathfinder`. The level adapts over successive updates based on three signals: whether the server is locally loaded (`app_.getFeeTrack().isLoadedLocal()`), whether this is a fast pass, and whether the last update found any paths (`bLastSuccess`). Under load, depth is capped at `PATH_SEARCH_FAST`; if the last update succeeded, depth decrements toward `PATH_SEARCH`; if it failed, depth increments toward `PATH_SEARCH_MAX` unless load prevents it. This feedback loop prevents expensive searches when the server is stressed and gradually invests more effort into requests that are actually finding results.

## Concurrency: Two Locks with Distinct Purposes

`PathRequest` uses two recursive mutexes. `mIndexLock` guards the scheduling state (`mLastIndex`, `mInProgress`) that `PathRequestManager` reads from multiple threads via `needsUpdate` and `updateComplete`. `needsUpdate` atomically checks and sets `mInProgress`, ensuring only one background thread processes a given request at a time and that stale ledger indices are not reprocessed. `mLock` guards `jvStatus`, the JSON result object that may be read by the subscriber thread concurrently with a background update writing a new result. Using `recursive_mutex` rather than `mutex` avoids deadlock when `isValid` (which writes `jvStatus`) is called from `doUpdate` while `mLock` is already held.

## Timing Instrumentation

Each `PathRequest` records three `steady_clock` time points: `created_` (at construction), `quick_reply_` (first fast-pass completion), and `full_reply_` (first full-pass completion). These are reported to `PathRequestManager` via `reportFast`/`reportFull`, which feed `beast::insight::Event` metrics for monitoring. The destructor logs the total lifetime and both latencies at `info` level, making it straightforward to correlate pathfinding responsiveness with ledger activity in production logs.