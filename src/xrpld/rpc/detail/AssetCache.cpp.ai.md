# `AssetCache.cpp` — Pathfinder Asset Cache for Trust Lines and MPTs

## Role in the System

`AssetCache` is a per-ledger, thread-safe cache that sits between the XRPL pathfinding engine and the ledger state database. It exists because the `Pathfinder` algorithm repeatedly queries trust lines and MPTokenIssuance objects for the same accounts during a single path-search pass — without a cache, each traversal step would re-read ledger SLEs from disk. The cache is scoped to a single immutable `ReadView` (one ledger version), which means its contents never become stale within the lifetime of a path request.

The class is declared as `final` and inherits `CountedObject<AssetCache>`, placing it under the XRPL object-counting diagnostic framework so live instances can be observed in memory stats. The destructor logs the final cache size at `debug` level — how many accounts were touched and how many distinct trust lines were materialized — giving operators insight into path-search workload.

## Trust Line Caching: The Direction Superset Optimization

The central complexity in `AssetCache` is `getRippleLines()`, which caches `PathFindTrustLine` vectors keyed by `AccountKey` — a composite of `AccountID`, `LineDirection`, and a precomputed hash. The `LineDirection` enum captures whether an account is "outgoing" (source or rippling-enabled side) or "incoming" (rippling-disabled side) on a path.

The direction distinction matters because `PathFindTrustLine::getItems()` filters trust lines differently depending on the direction: outgoing accounts return **all** trust lines, while incoming accounts return only the subset with rippling enabled. This means the outgoing set is always a superset of the incoming set for the same account.

`getRippleLines()` exploits this relationship to prevent the same account from occupying two cache entries with overlapping data. The logic at the top of the function:

1. Computes both `key` (the requested direction) and `otherkey` (the opposite direction).
2. Checks whether an entry already exists for `otherkey`.
3. If an **incoming** entry already exists and **outgoing** is requested, the incoming subset is **erased** and replaced by the larger outgoing set. The comment makes the motivation explicit: *"The full set will be built below, and will be returned, if needed, on subsequent calls for either value of outgoing."*
4. If an **outgoing** entry already exists and **incoming** is requested, the function simply **returns the outgoing superset** directly, redirecting the key to avoid storing a duplicate.

This means the cache always converges to storing at most one entry per account — the outgoing set — regardless of the order in which direction variants are requested. The `totalLineCount_` counter is kept consistent across these insertions and erasures, and an `XRPL_ASSERT` guards against underflow when subtracting the erased entry's size.

After the direction-reconciliation logic, the function emplaces a `nullptr` sentinel and then populates it with the results of `PathFindTrustLine::getItems()`. This two-phase approach (emplace with null, then fill) is guarded by a second `XRPL_ASSERT` ensuring the emplace only fires when the slot genuinely started as null, preventing accidental double-population.

The returned value is a `shared_ptr<vector<PathFindTrustLine>>` rather than a raw vector. The header comment explains the memory rationale: the estimate is that over 90% of accounts on the ledger have no usable trust lines for a given path search, so storing a null `shared_ptr` for those accounts costs far less than allocating empty vectors. The `shared_ptr` wrapper also lets map entries be safely shared across threads without copying.

## MPT Caching: `getMPTs()`

Multi-Purpose Token (MPT) caching in `getMPTs()` is structurally simpler. For a given account, it iterates through all items in the account's owner directory via `forEachItem()` and collects two categories:

- `ltMPTOKEN_ISSUANCE` entries: the account is the issuer; a `PathFindMPT` is built with `zeroBalance = false` and `maxedOut` derived from whether `sfOutstandingAmount` has reached `maxMPTAmount`.
- `ltMPTOKEN` entries: the account is a token holder; `zeroBalance` reflects whether `sfMPTAmount == 0`, and `maxedOut` is determined by reading the corresponding issuance SLE. If the issuance SLE cannot be found, `maxedOut` defaults to `true` (conservative safe assumption).

Like the trust line cache, accounts with no MPTs are stored as `nullptr` rather than an empty vector. The function returns `const&` to the internal `shared_ptr`, avoiding an unnecessary reference-count increment on the hot path.

## Concurrency Model

Both `getRippleLines()` and `getMPTs()` acquire `mLock` — a plain `std::mutex` — via `std::lock_guard` for the entire operation. This provides straightforward mutual exclusion: concurrent `Pathfinder` instances working on the same ledger (via a shared `AssetCache`) serialize their cache lookups and insertions. The granularity is intentionally coarse; the operations complete quickly (a hash map lookup or a single ledger directory scan), so the cost of holding the lock is small relative to the alternative of fine-grained per-entry locking.

The `ledger_` member is a `shared_ptr<ReadView const>`, meaning the cache keeps the ledger snapshot alive as long as any `Pathfinder` holds an `AssetCache` reference — this is correct and intentional, as the ledger must not be freed while path results derived from it are still being assembled.