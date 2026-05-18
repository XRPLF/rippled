# `MemoryFactory.cpp` — In-Memory NodeStore Backend

## Purpose and Context

This file implements a fully in-memory backend for the XRPL NodeStore system. The NodeStore is the layer that persists `NodeObject` items (ledger nodes, transactions, account state) keyed by their 256-bit hash. Where production deployments use NuDB or RocksDB backends backed by disk, the Memory backend stores everything in a `std::map` in the process heap. Its primary use cases are unit testing (where test suites need a real, functioning store without filesystem side-effects) and any transient, ephemeral storage scenario where persistence across process restarts is explicitly unwanted.

The file defines three cooperating types and one module-level registration function: `MemoryDB`, `MemoryBackend`, `MemoryFactory`, and `registerMemoryFactory`.

## The Three-Layer Design

### `MemoryDB` — the actual storage cell

`MemoryDB` is deliberately minimal: a `bool open` flag, a `std::mutex`, and a `std::map<uint256 const, std::shared_ptr<NodeObject>>` named `table`. Separating raw storage into its own struct (rather than embedding it in `MemoryBackend`) is the key architectural move: it lets multiple `MemoryBackend` instances opened with the same path name share a single underlying map. The `MemoryFactory` owns all `MemoryDB` instances in its `map_` member, keyed by path string; the `MemoryBackend` only holds a raw pointer `db_` into that collection. When a backend closes, it nulls its pointer but the factory's map retains the `MemoryDB`, so a subsequent `open()` on the same path recovers the same data — the in-memory store survives backend lifecycle events within a single process.

### `MemoryFactory` — factory and registry

`MemoryFactory` implements the `Factory` interface and serves two roles: it acts as a registry of named `MemoryDB` instances, and it is the creator of `MemoryBackend` objects via `createInstance()`. The factory is a process-level singleton created by `registerMemoryFactory()` using a function-local static, which guarantees both lazy initialization and thread-safe construction under C++11 rules. A module-level raw pointer `memoryFactory` is set to the singleton's address so that `MemoryBackend::open()` can call back into it without holding a reference — a simple coupling that works because the factory's lifetime spans the process.

The `map_` inside `MemoryFactory` is indexed with `boost::beast::iless`, making path lookups case-insensitive. This mirrors how other configuration-driven backends treat path names and avoids accidental duplication when callers use different capitalizations of the same logical store name.

The `MemoryFactory::open()` method includes a guard: it throws `std::runtime_error("already open")` if `db.open` is `true`. However, `MemoryDB::open` is initialized to `false` and is **never set to `true`** anywhere in the file. The guard is therefore dead code in the current implementation — likely a vestigial remnant of a stricter ownership model that was never fully realized. In practice, nothing prevents the same `MemoryDB` from being pointed to by multiple `MemoryBackend` instances simultaneously.

### `MemoryBackend` — the `Backend` interface implementation

`MemoryBackend` wraps a `MemoryDB*` and implements the full `Backend` interface. Construction validates that a non-empty `"path"` key exists in the configuration `Section`; without it, a `std::runtime_error` is thrown immediately, consistent with how disk-based backends enforce their `"path"` requirement. The `bool` argument to `open()` (conventionally meaning "create if missing") is silently ignored — an in-memory store always starts empty and always "creates" implicitly.

The `db_` pointer starts null and is assigned during `open()`; all substantive methods (`fetch`, `store`, `fetchBatch`, `storeBatch`, `for_each`) guard against a null pointer via `XRPL_ASSERT`, which will abort or throw depending on the build configuration. The `isOpen()` predicate simply casts `db_` to `bool`.

## Concurrency Model

Every mutating and reading method on `MemoryDB` acquires `db_->mutex` via `std::lock_guard` before touching `table` — except `for_each`. The iteration path reads `db_->table` without holding any lock, which is safe only if the caller guarantees no concurrent writes during enumeration. Since `for_each` is used in the XRPL codebase primarily for database sweep operations (e.g., replication or validation passes) that happen outside normal read/write activity, this is acceptable in practice, but it is an implicit contract rather than an enforced one.

`fetchBatch()` is implemented by simply calling `fetch()` in a loop, acquiring and releasing the mutex for every element. There is no bulk lock optimization. For a testing backend where the map is small and contention is negligible, this is a reasonable tradeoff — simplicity over throughput.

## No-Op Operations

Several `Backend` interface methods are no-ops by design: `sync()` does nothing (there is no I/O to flush), `getWriteLoad()` returns 0 (no queue to measure), `setDeletePath()` does nothing (there is no path to delete), and `fdRequired()` returns 0 (no file descriptors are consumed). These stubs allow the memory backend to satisfy the full `Backend` interface contract without implementing concepts that have no meaning outside a disk-based store.

## Registration Pattern

The `registerMemoryFactory(Manager&)` free function is the intended entry point. Callers invoke it once at startup, passing the global `Manager` singleton. The function-local static `MemoryFactory instance` self-registers by calling `manager_.insert(*this)` in its constructor, making the factory discoverable by name (`"Memory"`) through `Manager::find()`. This registration-by-constructor pattern is shared with `NullFactory` and the other backends, enabling the `Manager` to act as a plugin registry without requiring explicit factory tables elsewhere.