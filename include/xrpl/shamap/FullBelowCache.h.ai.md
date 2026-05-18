# `FullBelowCache.h` — Subtree Completeness Cache for SHAMap Sync

## Purpose and Context

When an XRPL node needs to acquire or verify a complete SHAMap — the Merkle radix trie underpinning every ledger's transaction and account-state sets — it must traverse the trie depth-first to discover which nodes are absent from local storage. On a large, partially-synchronized tree this traversal is costly: every inner node potentially requires 16 child checks, each of which may hit the database. `FullBelowCache` exists to short-circuit this walk. Once a subtree rooted at some inner node is verified to have all descendants present, that inner node's hash is inserted into the cache. Subsequent traversals check the cache first; a hit means the entire subtree is complete and can be skipped without descending into it.

The public alias `FullBelowCache = detail::BasicFullBelowCache` is what the rest of the codebase uses. The `detail::` wrapper is a conventional XRPL pattern for hiding implementation internals while keeping the public name clean.

## Internal Structure

`BasicFullBelowCache` wraps a `KeyCache`, which is itself a type alias for `TaggedCache<uint256, int, true>`. This is a thread-safe, time-expiring key set — it stores only `uint256` hashes (the keys), not associated values. Items expire after a configurable duration (defaulting to two minutes) and the cache targets a configurable maximum size (defaulting to zero, meaning unbounded). All thread-safety guarantees are inherited from `TaggedCache`; every public method on `BasicFullBelowCache` delegates directly to it and is safe to call from any thread without external synchronization.

## The Generation Mechanism

The most non-obvious design element is the `m_gen` atomic counter. Each `SHAMapInnerNode` independently stores a `fullBelowGen_` field, and its `isFullBelow(generation)` check simply compares that field against the generation passed in. The generation is obtained at the start of a sync traversal via `getGeneration()` and then threaded through the entire walk.

This two-layer scheme decouples the in-memory per-node markers from the persistent cache entries:

- **`touch_if_exists(hash)`** is called during traversal. If an inner node's hash is already in `FullBelowCache`, the entire subtree is known complete; the traversal skips it entirely and returns `SHAMapAddNode::duplicate()`. This is the hot path during `SHAMap::addKnownNode`.

- **`insert(hash)`** is called after a complete subtree traversal confirms no missing nodes. This records the fact persistently across SHAMap lifetimes and threads.

- **`setFullBelowGen(gen)` on the inner node** records the same fact in the node's own memory. This short-circuits re-traversal within a single pass of `getMissingNodes` even before the hash is looked up in the cache.

Together these two layers avoid redundant work at different granularities: the per-node generation handles intra-pass short-circuiting; the `FullBelowCache` handles inter-pass and cross-SHAMap reuse.

## Invalidation with `clear()` vs. `reset()`

`clear()` empties the cache and increments `m_gen`. The increment is the key action: all `SHAMapInnerNode` instances that stored the old generation in `fullBelowGen_` will no longer match the new generation, so `isFullBelow()` returns false for every node. This is a zero-cost global invalidation of all in-memory completeness markers — no tree walk is required to clear them. `NodeFamily::reset()` calls this when the family is being torn down and rebuilt between ledger replays or after missing-node recovery.

`reset()` does the same cache purge but sets `m_gen = 1` rather than incrementing. This is used at initial construction and on full application restart, where it makes semantic sense to return to a canonical baseline generation rather than retaining a growing counter.

The difference matters because any `SHAMapInnerNode` carrying `fullBelowGen_ > 1` would not match the reset-to-1 state, which is fine — those nodes are expected to be recreated fresh after a hard reset.

## Integration Point: `Family` and `NodeFamily`

`BasicFullBelowCache` is surfaced through the `Family` abstract interface via `getFullBelowCache()`, which returns a `shared_ptr<FullBelowCache>`. The concrete implementation `NodeFamily` constructs a single instance owned as `fbCache_` and shares it across all SHAMaps in the same family. The `backed_` flag on each individual `SHAMap` controls whether the cache is consulted: unbacked (in-memory-only) maps bypass it, so only persistent maps that interact with the node store participate in cache sharing.

Sweeping — expiring time-out entries — is triggered by `NodeFamily::sweep()`, which delegates to `fbCache_->sweep()` and then `tnCache_->sweep()` in tandem, keeping both caches aligned on the same housekeeping cycle.

## Summary of Design Rationale

Storing only the hash (key) rather than any tree structure keeps memory usage minimal. Time-based expiration via `TaggedCache` handles the case where a previously-complete subtree is later invalidated by a database eviction — entries age out naturally rather than requiring explicit notification. The generation counter provides a cheap, lock-free mechanism to globally invalidate all in-memory markers on demand. The result is a small, focused component that measurably reduces the cost of the most expensive operation in ledger synchronization.