# `include/xrpl/ledger/CachedSLEs.h`

This file declares exactly one thing: a named type alias that pins together the two halves of the ledger object caching subsystem.

```cpp
using CachedSLEs = TaggedCache<uint256, SLE const>;
```

`SLE` (`STLedgerEntry`) is the in-memory representation of a single entry in the ledger state tree — an account root, offer, trust line, escrow, and so on. Reading an SLE from the database requires deserializing a binary blob into a rich, heap-allocated object. The purpose of `CachedSLEs` is to ensure that once an entry has been deserialized, every code path that needs it during a given time window shares the same object rather than re-deserializing from disk.

The `const` qualifier on the mapped type is load-bearing. `TaggedCache` documents that callers must not modify stored objects unless they hold a lock over all cache operations. Instantiating on `SLE const` enforces that invariant at compile time, making the entire cache effectively immutable from the user's perspective and safe to share across threads without additional locking on the objects themselves.

The key type `uint256` is the cryptographic hash (digest) of the serialized SLE — not an account ID or ledger key. This integrates directly with `DigestAwareReadView`, which can return the on-disk hash for any ledger entry. `CachedView` (in `CachedView.h`) uses this: it wraps a `DigestAwareReadView`, delegates `read()` calls to `CachedSLEs::fetch(digest, handler)`, and only falls through to the underlying store on a cache miss. The handler supplies the freshly deserialized `SLE const` which is then inserted and returned.

In `Application.cpp`, the single application-wide `CachedSLEs` instance is constructed with a target size of `0` (meaning no fixed count limit) and a one-minute expiration window. `TaggedCache::sweep()` is called periodically by `Application` to demote cached (strong-reference) entries to weak references and eventually reclaim them, with the sweep size logged for debugging. `OpenLedger` holds a non-owning reference to the same instance, passing it through to each `CachedView` it creates so that all open-ledger reads share the same warm cache.

The alias also serves a documentation role: callers that include this header see `CachedSLEs` — a self-describing name — rather than a bare `TaggedCache<uint256, SLE const>` instantiation. Any future change to the underlying container type (key hasher, pointer policy, mutex type) can be made here in one place, opaquely to all consumers.