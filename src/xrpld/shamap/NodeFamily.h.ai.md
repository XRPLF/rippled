# `NodeFamily.h` — Concrete `Family` for Live Node Storage

## Role in the System

`NodeFamily` is the primary concrete implementation of the abstract `xrpl::Family` interface, connecting SHAMap tree operations to the live XRPL node database. A `Family` object is the context object passed to every `SHAMap` instance: it answers the question "where do I read and write nodes?" and "what caches am I allowed to use?" `NodeFamily` answers those questions by wiring up the application's actual `NodeStore::Database`, two SHAMap-specific caches, and a missing-node acquisition path that delegates to the inbound ledger subsystem.

There is a second concrete `Family` in the codebase for use during ledger replay or testing (`SHAMapStoreImp`), but `NodeFamily` is the one that backs every live SHAMap — account state trees and transaction trees — during normal node operation.

## Interface Contract (`Family`)

The abstract base in `include/xrpl/shamap/Family.h` establishes six pure-virtual methods: `db()`, `journal()`, `getFullBelowCache()`, `getTreeNodeCache()`, `sweep()`, `reset()`, and two `missingNodeAcquire` overloads. The split into `missingNodeAcquireBySeq` and `missingNodeAcquireByHash` reflects the two different starting points a SHAMap traversal might have when it discovers a missing node: sometimes the ledger sequence is all that's known, and sometimes a specific ledger hash is already in hand.

## Two Cache Tiers

`NodeFamily` holds two independent caches, both constructed in the initializer list.

`fbCache_` is a `FullBelowCache` — a time-expiring key-only set keyed on `uint256`. When a SHAMap walks a subtree and successfully resolves all nodes under a given inner node, that inner node's hash is inserted into this cache. On subsequent syncs or traversals, the presence of a hash here signals that no further network fetches are needed below that point. The cache is sized to 524,288 entries with a 10-minute expiration (`fullBelowTargetSize` / `fullBelowExpiration` from `Tuning.h`), which is deliberately large relative to a normal ledger state size. The `CollectorManager` is passed in so the cache exposes hit/miss metrics through the metrics collection framework.

`tnCache_` is a `TreeNodeCache`, which is a `TaggedCache<uint256, SHAMapTreeNode, ...>`. This keeps deserialized `SHAMapTreeNode` objects in memory, keyed by their hash, using a weak/shared union pointer scheme that lets the cache hold a weak reference so tree nodes already held alive by active SHAMap instances are not double-owned. Its size and age are driven by the application's `SizedItem::treeCacheSize` and `SizedItem::treeCacheAge` configuration values, making it scale with the configured node size class.

## Missing-Node Acquisition Logic

When a `SHAMap` traversal encounters a node that is not in the database or either cache, it calls one of the two missing-node methods.

`missingNodeAcquireByHash` is straightforward: it delegates directly to the private `acquire()` helper, which calls `app_.getInboundLedgers().acquire(hash, seq, InboundLedger::Reason::GENERIC)` if the hash is non-zero.

`missingNodeAcquireBySeq` is more nuanced because it must handle the scenario where many concurrent SHAMap traversals all hit missing nodes in the same ledger — or even different ledgers — simultaneously. Rather than allowing each caller to fire its own acquisition request, the method uses a `maxSeq_` / `maxSeqMutex_` pair to serialize and coalesce these calls:

1. Under the mutex, if `maxSeq_ == 0` (no acquisition is currently in flight), the caller sets `maxSeq_` to the missing ledger's sequence and takes ownership of the acquisition loop.
2. While holding ownership it unlocks the mutex, looks up the ledger's hash via `LedgerMaster.getHashBySeq()`, calls `acquire()`, then re-acquires the mutex to check if any other thread bumped `maxSeq_` to a newer sequence while the network fetch was in flight.
3. It loops until `maxSeq_` stabilizes — that is, until no later missing ledger was discovered during the last acquisition attempt.
4. If `maxSeq_` is already non-zero when a caller enters (another thread owns the loop), the caller simply updates `maxSeq_` to the max of the current value and its own sequence, then returns. The loop-owning thread will pick up the update.

This design prevents a thundering herd of redundant `acquire()` calls while also ensuring that if a newer missing ledger is discovered mid-acquisition, it will not be silently ignored.

## Lifecycle: `sweep()` and `reset()`

`sweep()` calls the expiration sweep on both caches, allowing expired entries to be reclaimed without clearing everything. This is meant to be called periodically by the application's maintenance timer.

`reset()` is more aggressive: it clears both caches entirely and resets `maxSeq_` back to zero under the mutex. It is called when the node store is rotated or when a large-scale re-sync makes all cached "full below" information stale.

## Design Notes

The class holds a reference to `Application` rather than injecting individual subsystems (ledger master, inbound ledger manager) because the missing-node handler is called on a hot path but must avoid circular initialization. The `Application&` reference lets `NodeFamily` reach those subsystems lazily at call time.

Both caches are held as `shared_ptr`s even though `NodeFamily` is their sole owner, because `SHAMap` instances receive copies of those `shared_ptr`s via `getFullBelowCache()` and `getTreeNodeCache()`, allowing SHAMap trees to outlive a `reset()` call without use-after-free — they will simply hold stale cache state (which is safe; it only affects performance, not correctness, since the database remains authoritative).