# `include/xrpl/basics/TaggedCache.h`

## Role and Purpose

`TaggedCache` is the central in-memory caching primitive for the XRPL node. It appears in every subsystem that needs to keep parsed or computed data close to the CPU: ledger entries (`CachedSLEs`), SHAMap tree nodes (`TreeNodeCache`), accepted ledger objects, transaction history, and more. The design solves a problem that a plain LRU cache cannot: in a highly concurrent system where multiple threads may independently load the same keyed object from storage, you want all of them to converge on *one* canonical in-memory copy. An ordinary cache gives you a place to look things up; `TaggedCache` additionally enforces object identity.

The file defines only the class template declaration and its nested types. All method bodies live in the companion `TaggedCache.ipp`, which is included by consumers that need the full implementation.

## The Dual-Region Model

Every entry lives in one of two logical regions within the same `m_cache` hash map:

- **Strong region** ("cached"): The `ValueEntry` holds the `SharedWeakUnionPointerType` as a strong reference. This is what `m_cache_count` tracks. So long as an entry is here, the object stays alive regardless of whether any external code holds a reference to it.
- **Weak region** ("tracked"): After the entry is swept or explicitly demoted via `del()`, the pointer is converted to a weak reference. The entry remains in `m_cache` and allows later callers who fetch the key to re-promote it back to the strong region if the object is still alive — i.e., if external `shared_ptr` holders still exist.

This two-region model means the total map size (`getTrackSize()`) is always ≥ the cache count (`getCacheSize()`). The gap is objects that are evicted from the hot cache but still alive elsewhere in the system.

## Template Parameters and Pointer Abstraction

The class is parameterized over two distinct pointer type arguments:

- `SharedWeakUnionPointerType` (defaults to `SharedWeakCachePointer<T>`): stored inside each `ValueEntry`. Must support `isStrong()`, `isWeak()`, `isExpired()`, `lock()`, `getStrong()`, `convertToStrong()`, and `convertToWeak()`.
- `SharedPointerType` (defaults to `std::shared_ptr<T>`): returned to callers.

This abstraction allows two distinct implementations to plug in:

1. **`SharedWeakCachePointer<T>`** (the default): a `std::variant<std::shared_ptr<T>, std::weak_ptr<T>>`, saving the cost of storing both at once. Used with ordinary heap objects.
2. **`SharedWeakUnion<T>`** (from `IntrusivePointer.h`): a single tagged raw pointer whose low bit encodes whether it is a strong or weak intrusive reference. Used by `TreeNodeCache` for `SHAMapTreeNode`, where the intrusive reference counting avoids the separate control-block allocation of `std::make_shared`.

The `IsKeyCache` boolean parameter enables a third mode: a pure key-existence cache that stores no value at all — only a `KeyOnlyEntry` carrying a `last_access` timestamp. `KeyCache` (`TaggedCache<uint256, int, true>`) uses this to track, for example, which full-below ranges the SHAMap has validated, without storing any associated data.

## Canonicalization

The `canonicalize(key, data, replaceCallback)` method is the most architecturally important operation. It is called when a piece of code has already loaded or constructed an object and wants to register it with the cache — but in a world where another thread may have beaten it there.

The logic:

1. If the key is absent, insert the new object as a strong entry and return `false` (the caller was first).
2. If the key is present and cached (strong), invoke `replaceCallback` to decide which copy wins. If the callback returns `true`, the cache entry is replaced with the caller's data; otherwise the caller's `data` parameter is updated to point at the existing canonical copy.
3. If the key is present but only weakly tracked, attempt to promote. If the object is still alive, again apply `replaceCallback`; if dead, adopt the caller's data.

The two convenience wrappers bake in the replacement policy: `canonicalize_replace_cache` always prefers the caller's new data (useful when the cache may hold a stale version), while `canonicalize_replace_client` always prefers the existing cached copy (the usual object-identity guarantee). The callback receives `entry.ptr.getStrong()` only when `R` is not a no-argument callable, avoiding the cost of materializing a strong pointer for intrusive types when it isn't needed.

## Sweep and Eviction

`sweep()` is called periodically (typically from a timer thread). It computes a `when_expire` cutoff based on the configured `m_target_age` and the current cache pressure relative to `m_target_size`. When the cache is over capacity, the effective age window shrinks proportionally, clamped to a minimum of one second, so that a rapidly growing cache doesn't evict everything instantly.

The `m_cache` is a `hardened_partitioned_hash_map`, which shards the data across multiple independent `std::unordered_map` partitions. `sweep()` spawns one worker thread per partition (`sweepHelper`) so that the per-partition linear scan proceeds in parallel. All threads are joined before the main lock is released. Swept entries whose strong pointers are about to be released are moved into a `SweptPointersVector` per partition; these vectors outlive the lock scope and are destroyed after the lock is dropped, so potentially expensive object destructors don't run under the lock.

The sweep has two outcomes for a strong entry whose age exceeds `when_expire`:

- If `use_count() == 1` (cache is the sole owner): move the strong pointer out to be destroyed and erase the map entry entirely.
- If `use_count() > 1` (someone else holds a reference): demote to weak. The entry survives in the map as a tracker.

Expired weak entries (where the external owner has also released) are erased unconditionally.

## Concurrency and the Recursive Mutex

All public operations acquire `m_mutex`, which is a `std::recursive_mutex` by default. The recursive mutex is necessary because `del()` and `canonicalize()` may both be called from code paths that are already holding the lock via `peekMutex()`. The `peekMutex()` accessor is a deliberate escape hatch: callers like `ConsensusTransSetSF` need to hold the cache lock while issuing a batch of lookups to make the multi-step operation atomic. The class comment warns that callers must not modify cached objects unless they hold a lock over all cache operations, enforcing an implicit immutability contract on stored values.

`sweep()` passes a `std::lock_guard<std::recursive_mutex> const&` token to each `sweepHelper` overload as a proof-of-lock parameter — not to capture it, but to statically enforce at the call site that the lock is held when per-partition threads are spawned. Because `m_mutex` is recursive, the sweeper threads themselves don't attempt to re-acquire it; they work only on the partition they are handed.

## Metric Integration

The inner `Stats` struct integrates with the `beast::insight` metrics framework. Construction registers a hook callback (`collect_metrics`) that fires when the collector polls for data. The hook publishes two gauges: the current cache size and the hit-rate percentage. Hits and misses are accumulated as `uint64_t` counters (`m_hits`, `m_misses`) under the cache lock and converted to a rate on demand, so there is no atomic contention on the hot path.

## Relationship to Consumers

`CachedSLEs` is the simplest instantiation — `TaggedCache<uint256, SLE const>` with all defaults — used by `CachedView` to memoize ledger state entries looked up during transaction processing. `TreeNodeCache` substitutes the intrusive pointer pair for both the union pointer and the shared pointer type, avoiding control-block allocations for the high-frequency SHAMap node working set. `KeyCache` flips `IsKeyCache=true` to track membership of `uint256` keys with no associated value, used by `FullBelowCache` to short-circuit redundant SHAMap full-below validation.