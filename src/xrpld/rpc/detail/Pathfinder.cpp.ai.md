# `Pathfinder.cpp` — Core Payment Path Discovery Engine

`Pathfinder.cpp` implements the complete payment path search algorithm for the XRP Ledger. When a client calls `ripple_path_find` or the server wants to compute a payment path, this file's logic is what drives the discovery, pruning, ranking, and selection of viable multi-hop routes between two accounts across potentially many currencies or MPT assets.

## Architecture at a Glance

The `Pathfinder` class is constructed with a source account, destination account, source and destination assets, and an `AssetCache` (a memoized wrapper around the current ledger's trust lines and MPT holdings). It then drives a three-phase pipeline:

1. **`findPaths()`** — graph traversal that enumerates candidate complete paths
2. **`computePathRanks()`** — liquidity simulation for each candidate path
3. **`getBestPaths()`** — selection of the top-N paths that together cover the required payment

These three phases correspond to the comment at the top of the file and are also reflected in the call-graph comment inside the header.

## The Path Table: Encoding Domain Knowledge as a Static Table

The most distinctive design decision in this file is the static `mPathTable`, a `std::map<PaymentType, CostedPathList>` initialized once at startup by `initPathTable()`. Rather than writing ad-hoc graph search logic for each of the five payment categories (`pt_XRP_to_XRP`, `pt_XRP_to_nonXRP`, `pt_nonXRP_to_XRP`, `pt_nonXRP_to_same`, `pt_nonXRP_to_nonXRP`), the developer encoded the topology of useful payment routes as short strings:

```
"sfd"    → source → book → destination
"sfad"   → source → book → account → destination
"saxfd"  → source → account → XRP book → book → destination
```

Each character maps to a `NodeType` enum value via `makePath()`. The `'s'` (source), `'a'` (accounts/trust-line hops), `'b'` (all order books), `'x'` (book-to-XRP only), `'f'` (book to destination currency), and `'d'` (destination account) encode the full space of useful route shapes. Each entry carries a `searchLevel` cost (0–10) so that `findPaths()` can limit the search depth to faster, cheaper paths at lower levels and expand to more aggressive paths at higher levels without re-running the entire traversal.

This approach means path shape knowledge is declarative and auditable without reading algorithmic code.

## Recursive Memoized Graph Traversal

`addPathsForType()` drives traversal with an elegant recursive memoization pattern. Given a `PathType` like `{nt_SOURCE, nt_ACCOUNTS, nt_BOOKS, nt_DESTINATION}`, it first strips the last node type to get the parent path type, recurses to get all partial paths of that parent type (already memoized in `mPaths`), then extends those partial paths by one hop via `addLinks()`. Completed paths land in `mCompletePaths`; incomplete ones are stored in `mPaths[type]` for subsequent type extensions that share the same prefix.

This means the engine never re-expands the same prefix twice, even when multiple entries in the path table share a common prefix like `"sf"`. The memoization is keyed by `PathType` (a `std::vector<NodeType>`), and the map is populated lazily on demand.

The search is bounded by `PATHFINDER_MAX_COMPLETE_PATHS = 1000`, a hard cutoff that prevents combinatorial explosion on large well-connected ledger states.

## Account Candidate Scoring and Fan-Out Control

Inside `addLink()`, when extending a path through an account hop, the engine enumerates all trust-line peers or MPT holders at the path's current endpoint and scores each as an `AccountCandidate`. Accounts that connect directly to the destination get a `highPriority` value of 10,000. Other accounts are scored by `getPathsOut()`, which counts viable outgoing paths (order book entries plus non-frozen, non-noRipple trust lines) with a 10,000 bonus if the peer is the destination. Results are memoized in `mPathsOutCountMap`.

The fan-out is then hard-capped: at most 10 candidates when branching away from the source, or 50 when branching from the source account itself. Sorting via `compareAccountCandidate()` uses three keys: priority descending, account ID descending (deterministic), and `(priority ^ ledger_seq)` as a pseudo-random tie-breaker. The XOR with ledger sequence is subtle but deliberate: it ensures that across different ledger versions, the ordering of equally-scored candidates varies, preventing systematic starvation of any particular route.

## noRipple Awareness

The engine respects the `noRipple` flag on trust lines. `isNoRippleOut()` checks whether the last account node in the current partial path has set noRipple on the outgoing side. If so, the next account step must proceed via an account that has rippling enabled on the incoming side (`LineDirection::incoming`). This is implemented by choosing the correct `LineDirection` when querying `AssetCache::getRippleLines()`. Without this, the engine would generate paths that would fail at execution time.

## Liquidity Estimation via Simulation

`getPathLiquidity()` doesn't estimate liquidity analytically — it runs an actual `RippleCalc::rippleCalculate` against a `PaymentSandbox` (which wraps the ledger read-only). This is expensive but correct. A two-pass strategy is used: the first pass checks whether the path can deliver at least the minimum useful amount (`dstAmount / (maxPaths + 2)`); if it fails, the path is dropped entirely. If it succeeds, a second partial-payment pass checks for additional liquidity above that minimum and accumulates it.

For "convert all" payments (where the destination amount is the ledger maximum, meaning "send as much as possible"), `partialPaymentAllowed` is set from the first pass.

## Default Path Accounting in `computePathRanks()`

`computePathRanks()` begins by computing how much the default path (no explicit intermediate hops) can deliver, by calling `RippleCalc` with an empty `STPathSet`. This contribution is subtracted from `mRemainingAmount` so that the explicitly discovered paths only need to cover the residual. This correctly handles the common case where direct trust-line paths already satisfy a large fraction of the payment.

## Path Selection in `getBestPaths()`

`getBestPaths()` merges two sorted-by-quality iterators — `mPathRanks` (from discovered paths) and `extraPathRanks` (re-ranked from client-injected `extraPaths`) — in a single linear pass. The selection rule is: fill up to `maxPaths` slots in quality order, but the last slot must contribute enough liquidity to cover the remaining amount (ensuring the full payment can succeed if there's no liquidity overlap). If no regular path covers the full amount independently, the function also searches for a `fullLiquidityPath` — a single path with capacity ≥ the entire payment that can be used as a fallback.

## MPT Dual-Path Handling

Throughout `addLink()` and `getPathsOut()`, both IOU trust lines and MPTokens are handled via the `PathAsset::visit()` visitor pattern. The key difference: IOU trust lines are bidirectional (either peer can be the outgoing node), while MPTs are not — they always use `LineDirection::incoming` because MPT rippling semantics differ. The code uses a C++20 `if constexpr` lambda template (`forAssets`) to share the candidate collection logic while differentiating asset-type behavior at compile time.

## Relationship to Surrounding Files

- **`Pathfinder.h`** declares the class interface, `PathType`, `PaymentType`, `PathRank`, and the flag constants (`afADD_ACCOUNTS`, `afADD_BOOKS`, etc.).
- **`AssetCache.h`** provides a mutex-protected, memoized view of trust lines and MPT holdings for a specific ledger, avoiding redundant ledger reads during traversal.
- **`PathfinderUtils.h`** provides `largestAmount()`, `convertAmount()`, and `convertAllCheck()` used to detect and handle "convert all" payment semantics.
- **`RippleCalc`** (via `xrpl/tx/paths/RippleCalc.h`) is the actual payment execution engine used by `getPathLiquidity()` for liquidity probing. The `PaymentSandbox` ensures these probes don't mutate ledger state.