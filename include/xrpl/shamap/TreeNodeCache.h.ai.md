# `TreeNodeCache.h` — SHAMap Tree Node Cache Type Alias

## Role in the System

`TreeNodeCache.h` defines a single type alias that gives a name to the in-memory cache of `SHAMapTreeNode` objects used throughout the XRP Ledger's Merkle-tree implementation. Every ledger's account state and transaction tree is a SHAMap, and every inner or leaf node in that tree is a `SHAMapTreeNode`. Traversing, verifying, or modifying a ledger requires fetching these nodes repeatedly; the `TreeNodeCache` is the hot layer that sits in front of persistent node storage, keyed by each node's `uint256` content hash.

## The Type Alias

```cpp
using TreeNodeCache = TaggedCache<
    uint256,
    SHAMapTreeNode,
    /*IsKeyCache*/ false,
    intr_ptr::SharedWeakUnionPtr<SHAMapTreeNode>,
    intr_ptr::SharedPtr<SHAMapTreeNode>>;
```

`TaggedCache` is a map/cache hybrid: entries are keyed by hash, held by strong reference while they are "hot," and demoted to weak references as they age out. So long as anything external holds a strong reference, the entry survives in the map and any subsequent lookup returns the same canonical object — this deduplication is the core purpose of `canonicalize()` on the underlying cache.

## Why Intrusive Pointers Instead of `std::shared_ptr`

The default `TaggedCache` template uses `SharedWeakCachePointer<T>` (backed by `std::shared_ptr`) for its internal map entries. `TreeNodeCache` deliberately overrides this with `intr_ptr::SharedWeakUnionPtr<SHAMapTreeNode>` and `intr_ptr::SharedPtr<SHAMapTreeNode>`. The distinction matters for two reasons:

**Memory reclamation.** With `std::make_shared`, the control block and the object are co-allocated; even after the last strong reference drops and the destructor runs, the memory block cannot be freed until all weak references expire. `SHAMapInnerNode` children — potentially 16 child pointers — would linger in that block. The intrusive model stores ref counts directly inside the `SHAMapTreeNode` itself (via `IntrusiveRefCounts`), and `SHAMapTreeNode::partialDestructor()` is called the moment the strong count hits zero, releasing the expensive parts of the object while weak references (held by the map) are still live. Memory is reclaimed far sooner.

**Single-word strong/weak duality.** `SharedWeakUnion<T>` stores either a strong or a weak intrusive reference in one pointer-sized word, using the low-order bit as a tag (alignment guarantees this bit is always zero in a real pointer). When the cache sweeper demotes a hot entry to a tracking-only entry, it calls `convertToWeak()` in-place, flipping one bit rather than replacing a whole `shared_ptr`/`weak_ptr` pair. This is both compact and fast.

## Consumers

The `Family` interface (in `Family.h`) exposes `getTreeNodeCache()`, returning a `shared_ptr<TreeNodeCache>`. The concrete `NodeFamily` implementation holds one instance per application, shared across all live SHAMaps. Because multiple concurrent `SHAMap` instances can reference the same canonical nodes, the canonicalization logic in `TaggedCache` ensures that identical on-disk nodes are represented by a single in-memory object — which is essential for SHAMap's copy-on-write scheme, where unmodified nodes are shared freely between ledger generations.

## Summary

The file is intentionally minimal: it is purely a named instantiation point. Its value lies in binding together the right pointer machinery — intrusive, weak-capable, single-word-union pointers — with `TaggedCache`'s two-level strong/weak cache policy, producing an efficient and memory-safe cache for the most heavily accessed data structure in the XRP Ledger runtime.