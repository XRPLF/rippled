# `Pathfinder.h` — Payment Path Discovery Engine

## Role in the System

`Pathfinder` is the core engine responsible for discovering viable multi-hop payment paths across the XRP Ledger. When an account wants to send a payment in one currency and have the recipient receive a different currency — or route funds through intermediary accounts and order books — the pathfinder searches the ledger's trust-line graph and order-book structure to enumerate candidate routes. Its output is an `STPathSet` consumed by `RippleCalc` (the actual payment simulation engine), which then determines exact amounts and exchange rates.

The class lives under `rpc/detail/` because pathfinding is a service exposed to API callers (the `path_find` and `ripple_path_find` RPC commands), not to transaction processing itself. Transaction processing in consensus uses only the paths the client already submitted.

## The Three-Phase Pipeline

`Pathfinder` presents a deliberate three-step API that callers must invoke in sequence:

1. **`findPaths(searchLevel)`** — Enumerate candidate paths by template expansion, populating `mCompletePaths`.
2. **`computePathRanks(maxPaths)`** — Score each candidate via a simulated payment using `RippleCalc`, building `mPathRanks`.
3. **`getBestPaths(maxPaths, fullLiquidityPath, extraPaths, srcIssuer)`** — Merge ranked internal paths with any caller-supplied `extraPaths` and return the final `STPathSet`.

Separating enumeration from ranking is a deliberate performance decision: discovery is graph traversal (cheap), while ranking requires calling `RippleCalc` on a `PaymentSandbox` for every candidate (expensive). The split lets callers control how many ranked paths they want and allows the streaming `path_find` API to inject previously-found `extraPaths` across repeated invocations without re-running the full graph search.

## Path Templates and the Payment Type Table

The key design insight is that the topology of useful payment paths is small and domain-specific. Rather than a generic BFS, `findPaths()` works from a static lookup table (`mPathTable`) that maps each `PaymentType` to an ordered list of `PathType` templates at various search costs.

A `PathType` is a sequence of `NodeType` tokens — for example, `{nt_SOURCE, nt_BOOKS, nt_ACCOUNTS, nt_DESTINATION}`, represented compactly as the string `"sbad"`. `initPathTable()` seeds this table once at startup with domain-encoded patterns covering each of the five `PaymentType` categories:

- `pt_XRP_to_XRP` — no cross-currency routing needed; the table is empty because only direct paths apply.
- `pt_XRP_to_nonXRP` — patterns like `"sfd"` (source → book → gateway), `"sfad"`, `"sfaad"`.
- `pt_nonXRP_to_XRP` — patterns like `"sxd"` (source sells token, buys XRP via book).
- `pt_nonXRP_to_same` — same-currency routing through gateways or books.
- `pt_nonXRP_to_nonXRP` — cross-currency paths, the most complex category.

Each template has an integer `searchLevel` cost (0 = trivially cheap, 10 = most aggressive). `findPaths()` only expands templates whose cost is ≤ the caller-supplied `searchLevel`, giving a natural depth-budget knob. Streaming path-find responses can start at level 1 for a fast first reply and increment toward 10 for subsequent responses.

## Graph Expansion: `addLink` and `addLinks`

Template expansion proceeds node-by-node using `addLink()` / `addLinks()`. Internally, `addLink()` takes a partially-built `STPath`, examines the last node's type, and appends all valid next-hop candidates according to the `addFlags` bitmask:

- `afADD_ACCOUNTS` — extend the path through trust-line counterparties obtained from `AssetCache::getRippleLines()` or `getMPTs()`.
- `afADD_BOOKS` — extend through order books from `OrderBookDB`.
- `afOB_XRP` — only the XRP-denominated book.
- `afOB_LAST` — the book must lead to the destination currency (used for the `nt_DEST_BOOK` node).
- `afAC_LAST` — must terminate at the destination account.

When a path becomes complete (reaches the destination), it is appended to `mCompletePaths`. The search caps at `PATHFINDER_MAX_COMPLETE_PATHS = 1000` to prevent unbounded memory growth.

The `isNoRippleOut()` / `isNoRipple()` helpers prune dead-end branches early: a path through an account that has set the `lsfNoRipple` flag on its outgoing trust line cannot carry funds, so that branch is discarded without ever probing further.

`mPathsOutCountMap` (keyed by `Asset`) tracks how many paths have been discovered branching out from each asset type. This short-circuits redundant expansion of the same order book or trust-line cluster when the count already exceeds a useful threshold.

## Ranking and Liquidity Measurement

`getPathLiquidity()` measures how much value a candidate path can actually deliver. It wraps `RippleCalc::rippleCalculate()` in a `PaymentSandbox` (a copy-on-write snapshot of ledger state) so no actual changes occur. For non-`convert_all_` paths it runs the calculation twice: once to verify the path meets a minimum threshold (`smallestUsefulAmount`), and again with `partialPaymentAllowed = true` to capture the full available liquidity. This two-pass approach avoids discarding paths that can't single-handedly complete a payment but could contribute useful liquidity when combined with others.

`rankPaths()` scores all survivors with a three-key comparator: better exchange rate (lower quality number, i.e. less input per output) first; then higher liquidity; then shorter path. Shorter paths are preferred at equal quality because they carry less counterparty risk and succeed more reliably against the actual ledger state.

`computePathRanks()` first executes the default (empty) path to measure how much liquidity it contributes, then subtracts that from `mRemainingAmount`. Subsequent paths are only required to cover the remainder, which avoids over-counting when the default path already satisfies part of the payment.

## `getBestPaths` and the Covering Path

After ranking, `getBestPaths()` performs a merge-sort between the newly ranked `mPathRanks` and any caller-supplied `extraPathRanks` (re-ranked from `extraPaths`), filling the output `STPathSet` up to `maxPaths`. A notable edge case: the last slot requires a path whose liquidity meets or exceeds `remaining`, ensuring the selected paths are collectively sufficient to complete the payment assuming no liquidity overlap.

If no single selected path can cover the full remaining amount on its own, the method fills `fullLiquidityPath` with the best such "covering" path found in the tail of the ranked list. The caller can then attempt the payment with and without this extra path, falling back if needed.

## Cooperative Cancellation and Load Tracking

Every public and private method accepts an optional `std::function<bool(void)> continueCallback`. Callers can supply a predicate that returns `false` when the search should abort (e.g., when an RPC connection closes). This cooperative model avoids preemptive cancellation complexity while keeping latency bounded.

The constructor registers a `LoadEvent` via `app_.getJobQueue().makeLoadEvent(jtPATH_FIND, ...)` to track CPU time in the job queue's load-balancing system. Pathfinding is one of the most expensive RPC operations, and this accounting prevents it from starving other work on a busy server.

## `AssetCache` and the Shared Ledger View

Rather than reading trust lines directly from the ledger on every `addLink()` call, `Pathfinder` works through a shared `AssetCache`. The cache wraps a single frozen `ReadView const` snapshot and memoizes `getRippleLines()` results keyed by `(AccountID, LineDirection)`. This is critical for performance: the same account may appear on many partial paths, and re-reading its trust-line objects from the ledger on each encounter would be prohibitively expensive. The cache is owned by the caller (e.g., the streaming path-find session) and shared across multiple `Pathfinder` invocations, so warm entries persist for the lifetime of an interactive session.

## Domain-Scoped Pathfinding

The optional `domain` parameter (`std::optional<uint256>`) threads through to `RippleCalc` to restrict path simulation to a specific permissioned domain, supporting the XRPL's domain-scoped payment channels feature. Paths that violate domain constraints will be pruned at the liquidity-measurement stage rather than during enumeration.