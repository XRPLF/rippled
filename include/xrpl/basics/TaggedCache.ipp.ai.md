# `TaggedCache.ipp` — Template Implementation of the XRPL Cache/Tracker

## Role in the System

`TaggedCache.ipp` contains the out-of-line template method bodies for `xrpl::TaggedCache`, a general-purpose concurrent cache that underpins most of rippled's in-memory object reuse: ledger state entries (`SLE`s), transactions, account roots, trust lines, and other ledger objects that are expensive to decode or fetch from disk. The `.ipp` pattern separates implementation from declaration while keeping both in header-includable form — the `.h` defines the class interface and the `.ipp` is included at the bottom (or by consuming translation units) to instantiate methods.

The class solves a problem specific to ledger node processing: many concurrent paths may independently fetch or create an object identified by the same hash key. Rather than permitting duplicates, the cache enforces a single *canonical* instance per key, replacing or redirecting callers as needed. It also acts as a weak-reference *tracker* — once an object is evicted from the active cache, the map entry survives as a weak pointer, so any code that retained a reference before eviction can still benefit from deduplication.

## Template Parameters and the Dual Mode

`TaggedCache` is parameterized over eight template arguments. The most architecturally significant is `bool IsKeyCache`. When `false` (the default), each entry stores a `ValueEntry` — a timestamp plus a `SharedWeakUnionPointerType` that can hold either a strong or a weak reference to `T`. When `true`, each entry stores a `KeyOnlyEntry` — just a timestamp, with no associated value. Key-only mode is used for negative-existence caches (e.g., "have we seen this transaction hash before?") where the value is irrelevant and memory footprint matters.

This duality is resolved at compile time via `std::conditional<IsKeyCache, KeyOnlyEntry, ValueEntry>::type Entry`, allowing a single class template to cover both use cases without virtual dispatch or runtime branching in fast paths.

## The Strong/Weak Pointer Abstraction

`ValueEntry` wraps a `SharedWeakUnionPointerType`, which defaults to `SharedWeakCachePointer<T>`. This type holds either a `std::shared_ptr<T>` (strong reference) or a `std::weak_ptr<T>` inside a `std::variant`, providing `isStrong()`, `isWeak()`, `convertToWeak()`, `convertToStrong()`, `getStrong()`, and `lock()`. An alternative intrusive implementation (`SharedWeakUnion<T>`) stores the strong/weak tag in the low bit of the pointer itself, avoiding the variant overhead when objects participate in intrusive reference counting.

The key insight is that `m_cache_count` tracks only strong-reference entries. The total `m_cache.size()` (returned by `getTrackSize()`) counts both strong and weak entries. This is why the class distinguishes `getCacheSize()` from `size()` / `getTrackSize()` — the former answers "how many objects is the cache keeping alive?" while the latter answers "how many objects is the cache aware of?"

## `sweep()` — Adaptive Expiry and Parallel Eviction

`sweep()` is the periodic eviction function. Its first decision is the expiry cutoff. If the cache is at or below `m_target_size`, entries older than `m_target_age` are expired. If the cache is oversize, the cutoff age is *proportionally shortened*: `target_age * target_size / current_size`, clamped to a minimum of one second. This creates a feedback loop: the more overloaded the cache is, the more aggressively it evicts.

`sweep()` then spawns one `std::thread` per partition of the underlying `hardened_partitioned_hash_map`, calling `sweepHelper`. Each thread iterates its partition independently. Because each partition is a distinct data structure (not a subset of a shared one), there is no intra-sweep contention. All threads are joined before the outer `std::lock_guard` exits — the sweep is fully synchronous from the caller's perspective, but parallelised internally.

The two `sweepHelper` overloads handle the value and key-only cases:

- **Value cache sweep**: Three cases per entry — (1) weak and expired: move pointer into `stuffToSweep` and erase the map entry; (2) strong and expired but with external holders (`use_count() > 1`): demote to weak, leave in map; (3) strong and expired with no external holders: move into `stuffToSweep` and erase.
- **Key-only cache sweep**: Simpler — entries past the cutoff are erased. An extra guard clamps `last_access` to `now` if it is somehow in the future, preventing entries from appearing permanently recent.

The `stuffToSweep` vector pattern is deliberate: moved-out smart pointers are destroyed *after* the mutex is released, so potentially expensive object destructors never run under the lock. The vector is sized per-partition to avoid reallocations.

## `canonicalize()` — Deduplication Under Concurrency

`canonicalize()` is the cache's most subtle operation. Its contract: given a key and a caller's shared pointer, if the cache already has a live entry for that key, one of them must win and the other must be redirected to point at the canonical instance. The `replaceCallback` parameter decides who wins.

The callback has two supported signatures via `if constexpr`:
- `bool()` — a zero-argument predicate. The common variants `canonicalize_replace_cache` (always returns `true`, cache wins) and `canonicalize_replace_client` (always returns `false`, client wins) use this form.
- `bool(SharedPointerType)` — a unary predicate receiving the existing strong pointer, for policies that inspect content.

The zero-argument form exists as a performance optimisation: obtaining a strong pointer from a `SharedWeakUnion` requires an atomic operation (`checkoutStrongRefFromWeak`), which is unnecessary for the simple "always replace" and "never replace" cases.

The entry may be in one of three states: not present, present with strong reference, or present with weak reference. In the weak case, `canonicalize()` attempts to lock the weak pointer. If it succeeds, the object is still alive in memory (held by some other caller), and it is promoted back to a strong reference in the cache. If it fails (the object was destroyed), the new data is inserted as a fresh strong entry.

## `fetch()` and `initialFetch()`

`initialFetch()` is the shared internal lookup path: it handles the three states of a value entry (absent, strong, weak) and updates `m_cache_count` on weak→strong promotion. It does *not* increment `m_misses` — that is left to the callers so they can control miss accounting differently.

The two-argument `fetch(key, handler)` overload implements a double-checked locking pattern. It checks the cache under lock, releases the lock, calls the handler to load from an external source (e.g., the database), then re-acquires the lock to insert the result. This avoids holding the cache mutex during I/O while still protecting the map from concurrent modifications. A second check on re-entry (via `emplace`) ensures that if another thread beat this one to insert while the lock was dropped, both insertions are handled gracefully.

## `del()` — Conditional Erasure

`del(key, valid)` has a nuanced `valid` flag. When `valid=true`, the strong reference is released (decrementing `m_cache_count`) and the entry is converted to a weak pointer, but the key stays in the map so that any existing external holders still benefit from tracking. When `valid=false`, or when the existing entry has already expired, the key is erased entirely. This models the difference between "this object is no longer cached" and "this key is invalid and must not be returned."

## Metrics and Observability

`m_stats` wires the cache into the `beast::insight` telemetry system via a hook that calls `collect_metrics()` periodically. This exports `size` (strong-reference count) and `hit_rate` as gauges. There is a subtle inconsistency: `touch_if_exists()` increments `m_stats.hits` / `m_stats.misses` (the collector-facing counters), while `fetch()` increments `m_hits` / `m_misses` (the raw counters returned by `getHitRate()` and `rate()`). The two accounting streams serve different audiences — the collector feeds external monitoring, while `getHitRate()` / `rate()` are queried programmatically within the process.

`peekMutex()` exposes the internal `recursive_mutex` directly. Its use is intentional: callers that need to perform a composite operation atomically (e.g., fetch followed by conditional re-insert) must hold the same lock the cache uses. The `recursive_mutex` permits this without deadlocking when the same thread re-enters via a cache method while already holding the lock.