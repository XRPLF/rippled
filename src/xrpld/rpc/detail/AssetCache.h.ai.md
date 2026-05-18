# AssetCache

`AssetCache` is a per-ledger, thread-safe cache purpose-built for the XRPL `Pathfinder`. During a pathfinding traversal, the engine iterates over many accounts and must repeatedly query their trust lines and Multi-Purpose Token (MPT) holdings. Without a cache, every step along a candidate path would re-read those entries from the underlying ledger view — an expensive operation at scale. `AssetCache` amortizes that cost by loading each account's asset data once and reusing it for the duration of a single pathfinding session.

## Ledger Binding

An `AssetCache` is constructed with a `std::shared_ptr<ReadView const>`, which is a read-only snapshot of a specific ledger version. All queries are answered against that snapshot, so the cache stays coherent. The ledger sequence is logged on both construction and destruction, making it straightforward to correlate cache lifetimes with ledger state during debugging. The destructor also logs the total number of accounts and distinct trust lines accumulated, which is useful for performance analysis.

## Trust Line Caching and the Direction Optimization

The most interesting part of `AssetCache` is how it handles `getRippleLines()`. The `Pathfinder` must know which trust lines an account can actually use at a given position in a path, and that depends on `LineDirection`. An *outgoing* account (the source, or an account reached via a rippling-enabled trust line) can use all of its trust lines. An *incoming* account (reached via a no-ripple trust line) can only use trust lines where rippling is enabled on its side — a strict subset.

Storing two separate sets for the same account — one for incoming, one for outgoing — would waste memory, since the outgoing set is always a superset of the incoming set. The implementation exploits this relationship directly. The internal `AccountKey` struct encodes both the `AccountID` and the `LineDirection`, and `getRippleLines()` always checks for the *opposite* direction's entry before inserting a new one:

- If the cache holds an **outgoing** entry and an **incoming** request arrives, the outgoing set is already a superset of what's needed. The method returns the outgoing set directly without loading anything new, and updates the lookup key so the returned pointer corresponds to the stored entry.
- If the cache holds an **incoming** entry and an **outgoing** request arrives, the stored subset is now incorrect for the broader query. The old entry is erased (with `totalLineCount_` adjusted accordingly) and a fresh full set is loaded from the ledger.

This logic ensures at most one `vector<PathFindTrustLine>` exists per account in the cache at any time, keeping peak memory use minimal even when the pathfinder queries the same account with different directions in different traversals.

## The `shared_ptr<vector>` Idiom

Rather than storing a `vector<PathFindTrustLine>` directly in the map, the cache stores `std::shared_ptr<std::vector<PathFindTrustLine>>`. When an account has no trust lines, the map entry is inserted with a `nullptr` rather than an empty vector. The comment in the header explains the reasoning: more than 90% of accounts are estimated to have no trust lines at all. Storing a null pointer for those accounts avoids allocating a vector object entirely, which — at the scale of pathfinding across a large ledger — is a meaningful memory saving. Callers must check the returned pointer for null before iterating.

The `shared_ptr` wrapper also serves a secondary purpose: entries can be safely handed out to callers while the map is unlocked. If a future eviction or re-fetch were added, outstanding `shared_ptr` copies held by the `Pathfinder` would remain valid.

## AccountKey and Hashing

`AccountKey` is a small private struct that bundles `AccountID`, `LineDirection`, and a pre-computed hash. The hash is computed once from the `AccountID` alone (using `xrpl::hardened_hash<>`) and reused for both the outgoing and incoming keys, since they share the same account. This avoids re-hashing when checking for the opposite-direction entry. The `Hash` functor is a passthrough to `get_hash()`, so the map never recomputes the hash during lookup.

## MPT Caching

`getMPTs()` follows a simpler pattern. It walks the account's directory entries via `forEachItem`, collecting any `ltMPTOKEN_ISSUANCE` entries (tokens the account has issued) and `ltMPTOKEN` entries (tokens the account holds). Each `PathFindMPT` captures three facts: the MPTID, whether the holder's balance is zero, and whether the issuance has reached its maximum outstanding amount. These flags allow the `Pathfinder` to skip unusable MPT paths early without additional ledger reads. Like trust lines, the map stores a null pointer rather than an empty vector when an account has no MPTs.

## Thread Safety

`AssetCache` is shared across path requests via `std::shared_ptr<AssetCache>`, and both `getRippleLines()` and `getMPTs()` acquire `mLock` for their entire duration. There is no lock-free fast path or reader-writer split — the expectation is that the coarse-grained mutex is sufficient given that cache hits are rare (first call per account always misses) and the locked section is short.

## Relationship to Callers

`Pathfinder` holds a `std::shared_ptr<AssetCache>` and passes it through the path-expansion steps. `PathRequest` also receives an `AssetCache` on `doCreate()`, `doUpdate()`, and `findPaths()`, allowing it to share the same cache across multiple pathfinder invocations within a single request lifecycle. The cache is thus scoped to one client request session, not shared across requests, which keeps it from accumulating stale entries across different ledgers.