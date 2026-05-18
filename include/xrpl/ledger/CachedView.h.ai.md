# `include/xrpl/ledger/CachedView.h`

## Role in the System

`CachedView.h` implements a transparent caching layer over the ledger's read-only view hierarchy. Its primary purpose is to avoid redundant deserialization of `SLE` (Serialized Ledger Entry) objects when the same ledger state entries are accessed repeatedly during transaction processing. The canonical instantiation, `CachedLedger`, defined in `Ledger.h` as `using CachedLedger = CachedView<Ledger>`, wraps the immutable closed ledger that serves as the base for applying new transactions.

## Class Structure: Implementation Split

The design separates a non-template base class `detail::CachedViewImpl` from the public-facing template `CachedView<Base>`. This is a deliberate architectural choice to avoid template bloat: all caching logic lives in `CachedViewImpl` and is compiled once, while `CachedView<Base>` is merely a thin wrapper that adds `shared_ptr<Base const>` ownership and a `static_assert` that `Base` derives from `DigestAwareReadView`.

`CachedViewImpl` holds a raw reference `base_` — safe because `CachedView<Base>` stores the `shared_ptr` that keeps the base alive. Both classes delete copy constructors and assignment operators, enforcing that a cached view always represents a unique, coherent view over a specific snapshot.

## Two Levels of Caching

The design uses two distinct caches that serve complementary roles:

1. **`CachedSLEs`** (a `TaggedCache<uint256, SLE const>`) is an **externally owned, process-wide** LRU cache keyed by SLE *digest* (the SHA-512Half hash of the serialized entry). Multiple `CachedView` instances over different ledgers share this cache. If two ledgers share an unchanged SLE (as closed ledgers often do), only one deserialized copy is kept in memory. The caller injects this cache at construction time.

2. **`map_`** (an `unordered_map<key_type, uint256, hardened_hash<>>`) is a **per-instance** map from ledger key (a 256-bit position in the SHAMap) to SLE digest. Its sole purpose is to avoid repeated calls to `base_.digest()`. Once a key has been resolved to its digest, subsequent reads of that key don't need to touch the underlying SHAMap node.

## The `read()` Path

The `read()` implementation in `CachedView.cpp` is the only non-trivial method; `exists()` simply delegates to it. The flow is:

1. Lock `mutex_` and look up `k.key` in `map_`. If found, the digest is known locally — skip the base lookup.
2. If not in `map_`, call `base_.digest(k.key)` *outside* the lock. This query walks the SHAMap to find the hash of the leaf node, but does not deserialize the SLE.
3. If no digest exists, the key is absent; return `nullptr`.
4. Call `cache_.fetch(digest, loader)` on the shared `CachedSLEs`. The `loader` calls `base_.read(k)` only if the digest is not already in the cache. This is the expensive path: it deserializes the raw SLE bytes into a live C++ object.
5. Update statistics via static `CountedObjects::Counter` instances — `CachedView::hit`, `CachedView::hitExpired`, and `CachedView::miss` — which distinguish a full hit, a digest-hit with an expired shared-cache entry, and a total miss.
6. If the key was a miss (not in `map_`), acquire the lock again and insert `{k.key, digest}` into `map_`.
7. Validate the returned SLE type with `k.check(*sle)` before returning.

The lock on `mutex_` is deliberately not held across steps 2-4. Holding it through a potential SHAMap traversal or deserialization would unnecessarily serialize concurrent readers. The absence of double-checked locking is intentional: two concurrent threads missing `map_` will both call `base_.digest()`, but that is idempotent, and only one will win the race to populate `map_` in step 6.

## Thread Safety

`mutex_` protects only `map_`. The external `CachedSLEs` cache has its own internal lock (a `std::recursive_mutex` in `TaggedCache`). The base view is a `const` reference to an immutable ledger snapshot, so all access to it is inherently safe without locking.

## `hardened_hash` in `map_`

The internal `map_` uses `hardened_hash<>` rather than `std::hash<uint256>`. Ledger keys are derived from account IDs and object types, all of which are network-visible. A predictable hash would allow an adversary to craft transactions that flood the same hash bucket, degrading `map_` lookups from O(1) to O(n). `hardened_hash` seeds its xxhasher with 128 bits of randomness at construction time, defeating such attacks.

## `base()` and Encapsulation

`CachedView<Base>` exposes a `base()` accessor that returns the underlying `shared_ptr<Base const>`. Its comment explicitly flags this as breaking encapsulation: callers using `base()` interact with the underlying `DigestAwareReadView` directly, bypassing both the local `map_` and the shared `CachedSLEs`. This escape hatch exists because some operations — particularly those that need the full `Ledger` type rather than just the `ReadView` interface — cannot be expressed through the view abstraction alone.

## Relationship to Other Files

- **`CachedSLEs.h`** — defines `CachedSLEs` as a single-line type alias: `using CachedSLEs = TaggedCache<uint256, SLE const>`. Separating this alias from `CachedView.h` allows the shared cache to be created and owned at a higher level (the application) and passed in.
- **`ReadView.h`** — defines `ReadView` and `DigestAwareReadView`. The `digest()` virtual method on `DigestAwareReadView` is what makes two-level caching possible: without a stable content hash per key, the shared `CachedSLEs` could not safely be used across multiple views.
- **`Ledger.h`** — defines `CachedLedger = CachedView<Ledger>`, the production use of this template, confirming that `Ledger` satisfies the `DigestAwareReadView` contract.