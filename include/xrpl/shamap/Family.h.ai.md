# `include/xrpl/shamap/Family.h`

## Role in the System

`Family` is the abstract interface that a `SHAMap` uses to reach everything outside itself: persistent storage, in-memory caches, a logging channel, and the recovery mechanism for gaps in local data. Every `SHAMap` holds a `Family&` reference (stored as `f_` in `SHAMap`), and all I/O and caching decisions flow through it. The interface exists to decouple the pure Merkle-radix-tree logic from the application infrastructure that backs it, making the map testable in isolation and allowing different deployment contexts (live node vs. unit test) to supply different implementations.

## Why a Bundled Interface Rather Than Individual Injections?

A `SHAMap` needs at least four external collaborators simultaneously: a database, two caches, and a log. Passing each one as a separate constructor argument would create a wide constructor signature that callers would have to assemble correctly on every construction. Wrapping them in `Family` gives a single dependency that carries a coherent set of resources with a shared lifetime. The caches and database for a given ledger family must be consistent with each other; bundling them prevents mismatched combinations from being constructed accidentally.

## The Two Caches

`getFullBelowCache()` returns a `FullBelowCache` (a `KeyCache<uint256>`). An entry in this cache means "I have confirmed that every descendant of this tree node is already stored locally." During tree traversal or sync, if a node's hash is found in this cache, the subtree beneath it can be skipped entirely — there is nothing to fetch. The cache is generation-stamped (`m_gen`): calling `clear()` increments the generation, which invalidates all cached entries without purging them one-by-one. `reset()` clears and resets the generation back to 1, used when rebuilding from scratch.

`getTreeNodeCache()` returns a `TreeNodeCache`, a `TaggedCache<uint256, SHAMapTreeNode>` that holds deserialized `SHAMapTreeNode` objects keyed by hash. When a node is read from the `NodeStore::Database`, it is deserialized into a `SHAMapTreeNode` and placed here. Subsequent lookups by the same hash retrieve the already-decoded object, avoiding redundant disk reads and deserialization. The use of `SharedWeakUnionPtr` as the internal pointer type lets the cache hold weak references that can be upgraded to strong ones — nodes can be evicted from the cache without immediately invalidating all live trees that share a pointer to them.

## Missing Node Recovery

The two `missingNode` methods are the error-recovery path triggered when a traversal reaches a node hash that is not in the cache and not in the local database. This signals an incomplete ledger — the local node joined the network after this ledger was validated and has not yet synced all its tree data.

Two overloads exist because callers may have different identifying information available:

- `missingNodeAcquireBySeq(refNum, nodeHash)` is called when the ledger is identified by its sequence number (the common case during normal validation). `nodeHash` is included for logging only.
- `missingNodeAcquireByHash(refHash, refNum)` is called when the ledger is identified by its hash (used during sync flows).

In the concrete `NodeFamily` implementation, both paths eventually call an internal `acquire()` that forwards to `app_.getInboundLedgers().acquire(...)`, triggering peer-to-peer ledger fetching. The `missingNodeAcquireBySeq` path also maintains a `maxSeq_` high-water mark under `maxSeqMutex_` to avoid launching redundant acquisition requests when many concurrent SHAMap operations simultaneously discover missing nodes in the same or nearby ledgers.

## Lifecycle: `sweep()` and `reset()`

`sweep()` is called periodically by the application's maintenance loop to expire stale entries from both caches, preventing unbounded memory growth. `reset()` tears down the entire cache state — used when the family's data is being rebuilt (e.g., after a database wipe or during certain ledger-replaying scenarios). The clean separation of these two operations reflects different operational needs: sweeping is a routine background task, while reset is a destructive one-time action.

## Non-Copyable, Non-Movable Design

`Family` deletes all copy and move operations. This is intentional: `SHAMap` stores a `Family&` reference (not a pointer), and multiple `SHAMap` instances can share the same `Family`. If a `Family` could be moved, the stored references in all associated maps would dangle. The deleted operations enforce at compile time that `Family` instances have stable addresses for their full lifetime.

## Relationship to `NodeFamily`

The only production implementation, `NodeFamily` (in `src/xrpld/shamap/NodeFamily.h`), is constructed with an `Application&` and a `CollectorManager&`, wires the `db_` reference to the application's node store, and instantiates the two caches with appropriate sizes and expiration policies. Test code supplies lighter-weight implementations (see `src/test/shamap/common.h`) that use in-memory stores without the full application stack.