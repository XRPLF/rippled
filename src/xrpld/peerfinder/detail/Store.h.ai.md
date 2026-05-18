# `peerfinder/detail/Store.h` — Abstract Persistence Interface for the Bootstrap Cache

`Store.h` defines a narrow abstract interface that acts as the persistence boundary for the PeerFinder subsystem's bootstrap cache. Its sole responsibility is to serialize and deserialize the set of known peer endpoints that a node can use to re-establish network connections after a restart. Nothing in this file deals with connection logic, ranking, or routing — it is purely a load/save contract.

## Role in the System

When an XRPL node starts up, it needs a warm list of candidate peer addresses to connect to before it has received gossip from the live network. The `Bootcache` class (in `Bootcache.h`) maintains this list in memory, using a bidirectional map keyed on both the `beast::IP::Endpoint` (for fast lookup) and an entry `valence` value (for ranked iteration). `Store` is the interface through which `Bootcache` persists that map to and from durable storage across restarts.

`Bootcache` holds a `Store&` reference injected at construction, and `Logic` (the central PeerFinder coordinator) wires everything together. This layering lets the in-memory cache operate entirely independently of the storage mechanism — only two moments cross the boundary: initial load at startup and periodic saves triggered by `Bootcache::periodicActivity()`.

## Interface Design

The interface has exactly two pure virtual methods and one nested value type:

```cpp
using load_callback = std::function<void(beast::IP::Endpoint, int)>;
virtual std::size_t load(load_callback const& cb) = 0;

struct Entry { beast::IP::Endpoint endpoint; int valence{}; };
virtual void save(std::vector<Entry> const& v) = 0;
```

The `load` method uses a callback rather than returning a container. This is a deliberate design choice: the caller (`Bootcache::load()`) wants to insert each record directly into its bimap as it streams in. Returning a `std::vector` would require an intermediate allocation that is immediately consumed and discarded. The callback eliminates the temporary, and the returned `std::size_t` lets the caller log or act on how many entries were actually valid and consumed.

The `save` method goes in the opposite direction — it takes a snapshot of the whole cache as a flat vector of `Entry` structs and overwrites the persistent store entirely. This is a full replace, not a diff or an incremental append, which matches how `StoreSqdb` works: it clears and rewrites the table atomically. The simplicity of the semantics (no primary keys, no update-or-insert logic at the `Store` level) keeps the interface stable regardless of what the underlying storage does internally.

## The `valence` Field

The `Entry::valence` integer encodes connection quality history. A positive valence indicates consecutive successful handshakes to that peer; a negative value indicates consecutive failed attempts. This value is what `Bootcache` uses to sort candidates in decreasing priority order when selecting outbound connection targets. By persisting valence alongside the address, the node avoids wasting connection budget on historically unreliable peers immediately after restart.

## Concrete Implementation: `StoreSqdb`

The only concrete implementation in this codebase is `StoreSqdb` (in `StoreSqdb.h`), which stores data in a local SQLite database via SOCI. It delegates the actual SQL to functions defined in `xrpld/app/rdb/PeerFinder.h` (`readPeerFinderDB`, `savePeerFinderDB`, `updatePeerFinderDB`), following the repository's pattern of keeping raw SQL out of domain classes. `StoreSqdb` also handles schema migration: it tracks a `currentSchemaVersion` (currently 4) and calls `update()` on open to convert older on-disk formats.

The separation between `Store` (the abstract interface) and `StoreSqdb` (the SQLite implementation) means the bootstrap cache can be tested with an in-memory mock without touching any database code, and future storage backends (e.g., a flat file or a key-value store) could be substituted without touching `Bootcache` or `Logic`.

## Summary

`Store.h` is intentionally minimal — 32 lines including the namespace boilerplate. It exists to enforce a clean separation of concerns: the in-memory peer ranking logic in `Bootcache` never sees SQL, file handles, or schema versions, and the storage layer never needs to understand valence ordering or bimap internals. The callback-based `load` and full-replace `save` semantics reflect the actual usage pattern and avoid unnecessary data copies at both endpoints of the persistence boundary.