# `DatabaseNodeImp` — Single-Backend Node Store Implementation

`DatabaseNodeImp` is the concrete, non-rotating implementation of the `NodeStore::Database` abstract interface. It represents the standard case in the XRPL node store hierarchy: a single persistent key/value backend (such as NuDB or RocksDB) that holds all ledger node objects regardless of their ledger sequence number.

## Role in the NodeStore Hierarchy

The NodeStore subsystem exposes two concrete `Database` implementations. `DatabaseNodeImp` targets deployments where a single, stable backend suffices — full history nodes or configurations that don't need online rotation of storage. Its counterpart, `DatabaseRotatingImp`, manages two backends simultaneously (a writable store and an archive) and supports hot rotation between them. Both classes derive from `Database`, which provides the thread pool for asynchronous reads, telemetry counters, and the public `fetchNodeObject()` dispatch.

`DatabaseNodeImp` holds exactly one `std::shared_ptr<Backend> backend_`, which is asserted non-null at construction. Ownership is shared so the same backend can potentially be observed elsewhere, but the database is the authoritative lifecycle manager: `stop()` is called in the destructor to drain all pending I/O before releasing the pointer.

## Ledger Sequence Is Irrelevant Here

A notable design point across most of `DatabaseNodeImp`'s overrides is that the `std::uint32_t` ledger sequence parameter is silently ignored. When there is one backend for all data, any sequence number resolves to the same physical store, making the sequence irrelevant at this layer. `isSameDB()` expresses this explicitly — it unconditionally returns `true` for any pair of sequence numbers, documenting the invariant directly in code. `DatabaseRotatingImp` also returns `true` for its analogous reason (the two-backend rotating store still acts as one logical database), but `DatabaseNodeImp`'s version is simpler because there are no locks or backend pointers to check.

## Store and Fetch Paths

`store()` is minimal: it updates telemetry via `storeStats()`, wraps the raw blob into a `NodeObject`, and forwards it to `backend_->store()`. The move semantics on `data` ensure the potentially large payload is transferred without copying.

`fetchNodeObject()` is the private virtual that the base class calls from its public `fetchNodeObject()` dispatcher. It delegates directly to `backend_->fetch()` inside a `try/catch`, re-throwing via `Rethrow()` if an exception escapes the backend — this ensures the exception propagates faithfully up the call stack rather than being swallowed. The status code returned by the backend is checked for three cases: `ok` and `notFound` are both silent (the caller inspects the returned pointer), `dataCorrupt` logs at fatal severity, and any other unknown status code logs at warn. If a `NodeObject` was retrieved, `fetchReport.wasFound` is set to signal a cache hit to the base class telemetry machinery.

`asyncFetch()` simply forwards to `Database::asyncFetch()`. The base class manages the read-thread pool and the pending-read map; `DatabaseNodeImp` does not need any additional indirection here, unlike a rotating database that would need to determine which backend to query.

## Batch Fetching

`fetchBatch()` is a public (non-virtual) method that fetches multiple node objects by hash in one backend call. After delegating to `backend_->fetchBatch()`, it applies a defensive resize: the assertion and subsequent `results.resize(hashes.size())` guard against backends that return fewer results than requested, keeping the output vector positionally aligned with the input hash vector. Missing entries (null pointers in the result) are logged at error level. Timing is measured with `steady_clock` and reported via `updateFetchMetrics()`, though hits are reported as zero because batch fetches don't interact with any cache layer at this level.

## Delegation Pattern

Every public method on `DatabaseNodeImp` is a thin delegation to either `backend_` or a base-class helper. `getName()` and `getWriteLoad()` pass through to the backend directly. `sync()` calls `backend_->sync()`. `importDatabase()` calls `importInternal()`, a protected base-class utility that iterates the source database via `for_each()` and bulk-stores objects into the given destination backend. `for_each()` itself simply calls `backend_->for_each()`.

This strict delegation approach keeps `DatabaseNodeImp` cohesive: it owns no business logic beyond adapting the `Database` virtual interface onto the `Backend` interface. The architectural complexity lives in the `Database` base (thread pool, async dispatch, telemetry) and in the concrete `Backend` implementations (storage format, compression, file management).