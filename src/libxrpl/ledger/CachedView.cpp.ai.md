# `CachedView.cpp` — Two-Level Ledger Entry Cache

## Role in the System

`CachedView.cpp` implements `CachedViewImpl::read()` and `CachedViewImpl::exists()`, the two methods that differentiate a `CachedView` from its underlying `DigestAwareReadView`. The file's entire purpose is to intercept ledger entry reads and serve them from a two-tier caching structure, avoiding redundant cryptographic lookups and deserialization during transaction processing.

`CachedView` is used throughout the XRPL transaction engine whenever a ledger must be read many times during a single validation pass. Rather than repeatedly hitting the underlying SHAMap or database, callers construct a `CachedView<Base>` wrapping a closed ledger and pass it around for the duration of the processing batch.

## The Two-Level Cache Design

The caching strategy is split across two distinct stores, each with a different scope and key scheme:

**Level 1 — `map_` (per-view, key → digest):** An `unordered_map<key_type, uint256>` owned by the `CachedViewImpl` instance. It maps a ledger entry's raw 256-bit key to its content digest (hash). This mapping is per-view — it reflects which keys exist in *this* ledger version and what their hashes are.

**Level 2 — `cache_` (shared, digest → SLE):** A `CachedSLEs` instance, which is a `TaggedCache<uint256, SLE const>`. This shared cache is keyed by the content digest, not the ledger key. Multiple `CachedView` instances, potentially for different ledger versions, share a single `CachedSLEs`. Since `SLE` objects are immutable and content-addressed by their hash, the same in-memory `SLE` can be safely reused across different ledger views when both versions contain an identical entry.

## Read Path Through `read()`

The `read()` method follows a precise sequence that reflects the cost hierarchy of each lookup:

1. **Local digest lookup (under lock):** The method first checks `map_` under `mutex_` to see if this view has already resolved the key to a digest. If found, the lock is released immediately and no call to `base_` is needed for this phase.

2. **Base digest lookup (lockless):** If the key isn't in `map_`, `base_.digest(key)` is called without holding any lock. For a SHAMap-backed ledger, this traverses the trie to produce the cryptographic hash of the entry. If the digest is absent, the key doesn't exist in this ledger and `nullptr` is returned.

3. **SLE fetch from TaggedCache:** `cache_.fetch(digest, handler)` is called. The `TaggedCache` first checks its own internal store. If present (and not GC'd to a weak pointer), the SLE is returned without invoking the handler. If absent, the handler calls `base_.read(k)` to deserialize the SLE from the SHAMap, and the result is inserted into the `TaggedCache` under the digest key.

4. **Key→digest backfill:** Only after a full miss — when the key was not found in `map_` — does the code re-acquire `mutex_` to insert `(key, digest)` into `map_`. The comment "Avoid acquiring this lock unless necessary" explicitly acknowledges this deferral: the expensive work (`base_.digest()`, `base_.read()`) is done outside the critical section, and the lock is taken only for the cheap map insertion.

5. **Keylet type validation:** Even after retrieving a valid SLE, `k.check(*sle)` is called. A `Keylet` carries both a key and an expected SLE type. Because the shared `TaggedCache` is keyed by digest and not Keylet, a `read()` for one type could theoretically retrieve an SLE for another if two Keylets share the same raw key. `k.check()` catches this at the boundary and returns `nullptr` on a type mismatch.

## Cache Hit Metrics

Three `static` `CountedObjects::Counter` instances track hit quality:

- **`CachedView::hit`**: The key was in `map_` (digest known), and the SLE was still live in `CachedSLEs`. Full cache satisfaction — no base reads.
- **`CachedView::hitExpired`**: The key was in `map_` (digest known from a prior read), but the SLE had been evicted from `CachedSLEs`. The view knew the entry existed but still had to call `base_.read()`. This "warm miss" is cheaper than a cold miss because `base_.digest()` is skipped.
- **`CachedView::miss`**: The key was not in `map_` at all — both `base_.digest()` and (on success) `base_.read()` were called.

The `XRPL_ASSERT(sle || baseRead, ...)` guards against an impossible state: if `baseRead` is false, it means the `TaggedCache` claimed to have an entry but returned null — which would indicate corruption in the cache internals.

## Concurrency Model

`mutex_` protects only `map_`. The `TaggedCache` (`CachedSLEs`) manages its own internal mutex. The design is deliberately layered to minimize contention: the outer lock guards the cheap key→digest map, while the heavier `TaggedCache` lock is not held during `base_.digest()` or `base_.read()` calls. This allows concurrent readers on different keys to proceed in parallel through the expensive base-read path, serializing only briefly to update the local map and within `TaggedCache` for its own internals.

## Class Hierarchy Note

The `CachedViewImpl` implementation class (in `detail::`) is separated from the public `CachedView<Base>` template. `CachedViewImpl` holds a non-owning reference (`base_`) to the underlying view; `CachedView<Base>` adds an owning `shared_ptr<Base const>` (`sp_`) to keep the base alive, and its `base()` accessor exposes the raw underlying view — intentionally bypassing the cache for callers that need the original object directly. This two-class split avoids template instantiation of the `read()`/`exists()` bodies for every concrete `Base` type.