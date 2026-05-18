# `include/xrpl/nodestore/Database.h` — NodeStore Database Abstract Base Class

## Role and Purpose

`Database` is the abstract base class that defines the persistence contract for all node storage in the XRP Ledger. Every piece of ledger data — account states, transaction records, ledger headers — is serialized into a `NodeObject`, keyed by a 256-bit hash of its payload. Because the total set of node objects vastly exceeds available memory, any hash not found in an in-memory cache must be retrieved from disk. `Database` sits at this boundary: it owns the async read thread pool, tracks performance statistics, and defines the interface that concrete backends (NuDB, RocksDB, etc.) must implement.

The class occupies the `xrpl::NodeStore` namespace and relies on three key collaborators: `NodeObject` (the data record), `Backend` (the pluggable storage engine), and `Scheduler` (the async task coordinator).

## Two-Layer Fetch Design

The most architecturally significant feature of `Database` is its split between a public non-virtual `fetchNodeObject()` and a private pure-virtual `fetchNodeObject()` of the same name. This is a classic Template Method pattern:

```cpp
// Public, non-virtual — adds timing, metrics, and scheduler callback
std::shared_ptr<NodeObject>
fetchNodeObject(uint256 const& hash, std::uint32_t ledgerSeq,
                FetchType fetchType = FetchType::synchronous,
                bool duplicate = false);

// Private, pure-virtual — subclass provides actual backend lookup
virtual std::shared_ptr<NodeObject>
fetchNodeObject(uint256 const& hash, std::uint32_t ledgerSeq,
                FetchReport& fetchReport, bool duplicate) = 0;
```

The public wrapper measures wall-clock duration, increments hit/miss counters atomically, accumulates byte counts, and invokes `scheduler_.onFetch()` so the scheduler can observe latency. This guarantees that no subclass can bypass metrics — the instrumentation is structural, not optional.

## Async Read Thread Pool

The constructor spawns `readThreads` detached threads immediately. Each runs `threadEntry()`, which loops waiting on `readCondVar_` for work placed into the `read_` map:

```cpp
std::map<
    uint256,
    std::vector<std::pair<std::uint32_t,
        std::function<void(std::shared_ptr<NodeObject> const&)>>>>
    read_;
```

The map key is the hash; the value is a vector of `(ledgerSeq, callback)` pairs. This structure deliberately coalesces multiple concurrent `asyncFetch()` requests for the same hash — all registered callbacks are satisfied by a single backend read. This is a meaningful optimization for scenarios like ledger acquisition where many validator nodes may simultaneously request the same objects.

Each thread extracts up to `requestBundle_` entries per mutex acquisition (default 4, configurable via `rq_bundle` in the config file). This batching strategy amortizes the cost of mutex acquisition across multiple items, reducing lock contention under high read pressure.

When the thread processes a batch, it also handles the multi-sequence case: if multiple callbacks were registered for the same hash but different ledger sequence numbers, the thread checks `isSameDB(req.first, seqn)` to determine whether those sequence numbers map to the same physical backend. If they do, it reuses the already-fetched object; otherwise it performs a second fetch. This is essential for `DatabaseRotating`, which may store different ledger ranges in separate backend files.

## Shutdown Sequencing

The destructor calls `stop()`, which sets `readStopping_` atomically, clears the pending `read_` queue, and broadcasts on `readCondVar_` to wake all threads. It then spin-waits (yielding) until `readThreads_` reaches zero, with an assertion that this completes within 30 seconds.

There is a critical ordering constraint documented in the destructor comment: **derived classes must call `stop()` in their own destructor**, not rely on the base class destructor to do it. The reason is that background threads call the pure-virtual `fetchNodeObject()` through a subclass vtable. If the subclass is destroyed first and the base destructor calls `stop()` second, a thread that woke up between those two events would invoke a dangling vtable entry — undefined behavior. Calling `stop()` in the derived destructor ensures all threads have exited before the derived class's data members are destroyed.

## Concurrency and Statistics

All counters (`storeCount_`, `storeSz_`, `fetchTotalCount_`, `fetchHitCount_`, `fetchDurationUs_`, `storeSz_`, `fetchSz_`) are `std::atomic`, allowing lock-free increment from any thread. The `read_` map and `readCondVar_` are protected together by `readLock_`, and `getCountsJson()` acquires this lock briefly to snapshot the queue depth.

The protected `storeStats()` helper enforces an invariant via `XRPL_ASSERT(count <= sz)`: the byte size of stored data must be at least as large as the item count, which rules out obvious accounting bugs when subclasses update store metrics.

## Lifecycle Constraints and Configuration

Two configuration parameters are validated strictly at construction time. `earliest_seq` sets `earliestLedgerSeq_` (default `XRP_LEDGER_EARLIEST_SEQ`, which is 32570 for the main network); zero is explicitly rejected. `rq_bundle` must be between 1 and 64 inclusive. Both are `const` after construction, making them safe to read from any thread without synchronization.

The `isSameDB()` pure virtual method exists because some implementations (specifically `DatabaseRotating`) maintain multiple physical backend files covering different ledger sequence ranges. Callers and the internal async thread pool use `isSameDB()` to avoid redundant backend lookups when sequence numbers happen to fall in the same file.

## Relationship to `DatabaseRotating`

`DatabaseRotating` is the only concrete subclass visible in the header tree. It extends `Database` with a `rotate()` operation that swaps in a new writable backend while archiving the old one. The `isSameDB()` and `isSameDB`-dependent logic in the async thread entry function exists specifically to support this rotation scheme cleanly. The base `Database` never needs to know what rotation means; it only needs to know whether two sequence numbers resolve to the same underlying store.

## Import Path

`importDatabase()` is the public API for bulk migration, but the actual work is delegated to `importInternal(Backend& dstBackend, Database& srcDB)`. This helper calls `srcDB.for_each()` to iterate all source objects, assembles them into batches of `batchWritePreallocationSize`, and commits each batch via `dstBackend.storeBatch()`. Byte statistics are accumulated through `storeStats()` after each successful batch flush. Exception safety is basic: a caught exception logs the error and returns early without aborting the entire import.