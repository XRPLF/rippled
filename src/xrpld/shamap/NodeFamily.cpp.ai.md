# `NodeFamily.cpp` — Application-Level SHAMap Family Implementation

## Role in the System

`NodeFamily` is the production concrete implementation of the abstract `Family` interface defined in `include/xrpl/shamap/Family.h`. The `Family` abstraction exists so the SHAMap data structure — a Merkle tree used to represent ledger state and transaction sets — can be decoupled from the application-level concerns of storage, caching, and network acquisition. `NodeFamily` bridges those two worlds: it hands the SHAMap a `NodeStore::Database`, two caches, and a callback path for when the tree discovers it is missing a node.

## Cache Construction

The constructor initializes two caches whose lifetimes are owned by `NodeFamily` as `shared_ptr` members and exposed through the `Family` interface accessors.

`FullBelowCache` is a key-only `KeyCache` that remembers which SHAMap subtree roots already have all their descendants resident locally. When the tree is being synced from peers, this cache lets it skip entire subtrees it already knows are complete, avoiding redundant network fetches. It is constructed with a hard-coded target size of 524,288 entries (`fullBelowTargetSize` in `Tuning.h`) and a 10-minute expiration. The size is deliberately large because a false negative — thinking a subtree might need fetching when it doesn't — is cheap, but a false positive that prematurely marks a subtree as complete would cause data corruption.

`TreeNodeCache` is a `TaggedCache<uint256, SHAMapTreeNode>` that caches deserialized tree nodes by their hash. Its size and expiration age are drawn from `app.config().getValueFor(SizedItem::treeCacheSize/treeCacheAge)`, which scale with the configured node capacity class. This cache is the primary performance optimization for reads: SHAMap lookups hit it first before falling through to the backing `NodeStore::Database`.

## Missing Node Handling

When the SHAMap dereferences a node pointer and the node is absent from both the in-memory cache and on-disk store, it invokes one of two `Family` callbacks: `missingNodeAcquireBySeq()` (called when only a ledger sequence number is available) or `missingNodeAcquireByHash()` (called when the ledger hash is already known). The hash variant is a one-liner that immediately delegates to the private `acquire()` method.

`missingNodeAcquireBySeq()` is more involved because it must first resolve the sequence number to a ledger hash via `LedgerMaster::getHashBySeq()`. This lookup can fail for old or unknown ledgers, which is why `acquire()` guards on `hash.isNonZero()` before going to the network.

## Concurrency Design in `missingNodeAcquireBySeq`

The `maxSeq_` field and its mutex implement a coalescing mechanism for concurrent missing-node events. Multiple threads traversing different SHAMaps can simultaneously discover missing nodes in different ledgers. Without coordination, each would independently fire off an acquisition request — potentially for stale ledgers while a more recent one also needs fetching.

The design serializes this: only the thread that finds `maxSeq_ == 0` (or sets a new maximum) drives the acquisition loop. Other threads that call `missingNodeAcquireBySeq()` concurrently simply update `maxSeq_` if their sequence is higher and return immediately, relying on the driving thread to eventually pick up the new maximum.

The driving thread runs a `do/while` loop that captures `maxSeq_`, releases the lock (since `acquire()` can call back into the `missingNodeAcquireBySeq` re-entrantly through ledger fetch callbacks), acquires the ledger, re-acquires the lock, and only exits when `maxSeq_` hasn't changed during the acquisition. This loop handles a natural race: if a new missing ledger with a higher sequence is reported while the current acquisition is in flight, the loop picks it up on the next iteration. The lock is held only around reads and writes of `maxSeq_`, not around the potentially long-running network operation.

This pattern deliberately prioritizes the most recently discovered missing ledger. The assumption is that for a node that is catching up, the most recent missing data is the most important to repair first.

## `sweep()` and `reset()`

`sweep()` delegates to both caches to evict expired entries, called periodically by the application's sweep timer. `reset()` clears both caches entirely and sets `maxSeq_` back to zero under the mutex. This is used during a full state reset — for example when a validator needs to restart synchronization from scratch.

## Relationship to `Family` Interface

`NodeFamily` is the only `Family` implementation used for normal ledger operation. The `Family` abstraction also allows tests to inject alternative implementations with controlled behavior (e.g., caches that never evict, or databases that simulate missing nodes). `NodeFamily` itself is non-copyable and non-movable, since it holds references to application-owned singletons (`Application`, `NodeStore::Database`).