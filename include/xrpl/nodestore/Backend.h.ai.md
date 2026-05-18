# `include/xrpl/nodestore/Backend.h`

## Role in the NodeStore System

`Backend` is the storage abstraction at the heart of the XRP Ledger's persistence layer. Every ledger object — account states, transactions, ledger headers — is a `NodeObject` that gets written to and read from a `Backend`. The class is a pure abstract interface that lets the higher-level `Database` layer remain entirely indifferent to which database engine is actually in use: RocksDB, NuDB, or a heap-allocated in-memory store for unit tests all satisfy this contract identically.

The design choice here is deliberate and conservative. By fixing the interface to the narrowest possible surface — keyed blob storage with 256-bit hashes — it avoids leaking database-specific semantics upward into ledger logic. Backends can be swapped at deployment time through configuration without any code changes above this abstraction boundary.

## The Data Model

`Backend` is fixed at construction to a particular key size (always 32 bytes in practice, matching `NodeObject::keyBytes`). Values are `NodeObject` instances — a type tag (`hotLEDGER`, `hotACCOUNT_NODE`, `hotTRANSACTION_NODE`), a 256-bit hash that serves as the primary key, and an opaque `Blob` of serialized data. The `Status` return type from `fetch()` covers the full range of outcomes: `ok`, `notFound`, `dataCorrupt`, `unknown`, `backendError`, and a `customCode` escape hatch for backend-specific error conditions.

## Lifecycle: Open and Close

The interface separates construction from initialization via the `open(bool createIfMissing)` method. This two-phase pattern lets callers catch exceptions from file I/O or database initialization without wrapping constructors in try/catch. `isOpen()` provides a query for implementations that track this state explicitly.

A second overload of `open()` accepts `appType`, `uid`, and `salt` for deterministic database creation. This overload exists exclusively to support NuDB's header-level application identification — all other backends provide a default implementation that throws `std::runtime_error`, clearly documenting that this capability is not part of the general interface but is available for callers that know they are working with NuDB.

The destructor contract is strong: all open files are closed and flushed, and any batched writes or scheduled tasks will complete before the destructor returns. This ensures that callers who simply drop their `unique_ptr<Backend>` cannot silently lose data.

## Fetch and Store: Concurrency Contracts

The inline `@note` comments in `fetch()` and `store()` document a concurrency invariant that is critical for correctness: both `fetch()` and `store()` **will be called concurrently** by multiple threads. Implementations must be internally thread-safe for these two operations.

`storeBatch()` and `for_each()`, by contrast, are explicitly **not** called concurrently with each other or with other writes. This asymmetry reflects real usage: individual stores originate from ledger-processing threads (concurrent by nature), while batch writes and full-database iteration happen during controlled phases such as import or compaction. This layered concurrency contract allows backends to use coarse locking or lock-free structures selectively.

`fetchBatch()` accepts a vector of hashes and returns a paired vector of results plus a `Status`. Bulk fetches exist primarily to amortize round-trip or I/O costs when prefetching sets of related objects.

## Sync and Write Load

`sync()` provides an explicit flush point — callers can ensure that all previously submitted stores are durable before proceeding. `getWriteLoad()` returns an estimate of pending write operations, which the `Database` layer uses for back-pressure and diagnostic reporting. Neither method has a return value with failure semantics; they are best-effort operational utilities.

## Optional and NuDB-Specific Extensions

`getBlockSize()` returns `std::optional<std::size_t>`, defaulting to `std::nullopt`. NuDB organizes data into fixed-size blocks, and this method lets callers that care about alignment or prefetch granularity query the block size without requiring a downcast. Backends that have no concept of block size simply inherit the default.

`verify()` performs consistency checking and is currently implemented only by `NuDBBackend`. The comment explicitly acknowledges the gap: it is not yet called at startup, but the intent is that it could one day be invoked at launch to detect corruption before a crash. Providing a no-op default rather than a pure virtual method allows other backends to exist without implementing a concept that does not apply to them.

`setDeletePath()` marks the database for deletion of its on-disk files upon destruction. This is used by temporary databases — unit tests and ephemeral shard stores — where cleanup is required without explicit external management.

## Relationship to Factory and Database

`Backend` instances are never constructed directly. The `Factory` abstract class provides `createInstance()`, and the `Manager` singleton dispatches to the appropriate registered factory based on the `type` field in the `node_db` configuration section. `Factory::createInstance()` accepts a `Scheduler&` for deferred task execution — backends that batch writes or defer flushes to background threads use this scheduler rather than spawning their own threads.

`Database` wraps one or more `Backend` instances and adds a `TaggedCache` read cache, async fetch queuing via a pool of reader threads, and ledger-sequence-aware routing (the `DatabaseRotating` subclass directs writes to the current shard and reads to any shard that might hold the target sequence). From `Backend`'s perspective, it sees only individual or batched object operations; the routing and caching logic entirely lives in `Database`.

`fdRequired()` returns the number of file descriptors the backend expects to consume. The `Database` base class aggregates these values and exposes them so the process can pre-check against the OS file descriptor limit before opening any databases.