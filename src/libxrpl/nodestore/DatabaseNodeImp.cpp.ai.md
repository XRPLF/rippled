# `DatabaseNodeImp.cpp` — Single-Backend NodeStore Implementation

## Role in the System

`DatabaseNodeImp` is the concrete, single-backend implementation of the abstract `Database` class in the XRPL NodeStore subsystem. The NodeStore is the persistence layer for every ledger object in the XRP Ledger: account states, transactions, and all other ledger nodes are serialized as `NodeObject` values keyed by their 256-bit SHA-512 hash. This file provides the four core operations — `store`, `asyncFetch`, `fetchNodeObject`, and `fetchBatch` — that sit between the higher-level ledger machinery and a pluggable `Backend` (NuDB, RocksDB, in-memory, etc.).

The counterpart class `DatabaseRotatingImp` handles a two-backend rotation scheme used for the historical shard store. `DatabaseNodeImp` is the simpler, single-backend path used for the primary node database. The architectural split is clean: routing logic by ledger sequence only appears in the rotating variant; here `isSameDB()` always returns `true` and the `ledgerSeq` parameters in `store()` and `fetchNodeObject()` are intentionally unnamed and ignored.

## Store Path

`store()` begins by calling `storeStats()` with a count of 1 and the blob's byte size before the data is moved. This ordering matters: because `data` is moved into `NodeObject::createObject()`, the size must be captured first. Once the `NodeObject` is constructed from the type, moved blob, and hash, it is handed to `backend_->store()`. The function signature accepts a `std::uint32_t` ledger sequence that is discarded — the single backend receives everything regardless of which ledger an object belongs to.

## Single-Object Fetch

`fetchNodeObject()` is the private virtual override called by the base class's thread pool machinery. It wraps `backend_->fetch()` in a try-catch: if the backend throws any `std::exception`, a fatal log entry is emitted with the hash and exception message before `Rethrow()` re-raises the exception to propagate it up through the calling thread. This makes backend I/O failures loudly visible while still letting the crash propagate correctly rather than swallowing exceptions.

The `Status` returned by `backend_->fetch()` is inspected in a switch. Both `ok` and `notFound` are silent — a missing node is a normal condition during ledger history traversal. `dataCorrupt` gets a `fatal`-level journal entry, reflecting that corruption in the node store is a ledger integrity failure requiring operator attention. Any unknown status codes produce a `warn` log but do not abort. Only if the output `nodeObject` pointer was actually populated is `fetchReport.wasFound` set to `true`, which feeds the base class's hit-count metric.

## Batch Fetch

`fetchBatch()` is a public method that bypasses the async read queue and calls `backend_->fetchBatch()` directly. The result is a `pair<vector<shared_ptr<NodeObject>>, Status>` from which only `.first` is used — the batch-level status is not checked separately.

After the call, an `XRPL_ASSERT` enforces that the backend returned either exactly as many results as input hashes or an empty vector. This guards against a buggy backend returning a partial batch that would silently misalign position-keyed results. Following the assertion, `results.resize(hashes.size())` normalizes an empty-vector response (which some backends return when no objects are found) into a correctly sized vector of nullptrs, preserving the positional contract: `results[i]` corresponds to `hashes[i]`. Any null slot is then logged at `error` level, making cache misses or missing history visible in diagnostics without throwing.

The entire batch fetch is bracketed by `steady_clock` timestamps, and the microsecond duration is fed to `updateFetchMetrics()` alongside the fetch count. This gives the base class's monitoring layer accurate latency data for operator dashboards.

## Async Fetch

`asyncFetch()` simply delegates to `Database::asyncFetch()`. The base class owns a thread pool whose workers dequeue hash-lookup requests and call the private `fetchNodeObject()` override. `DatabaseNodeImp` has nothing to add here — no per-backend scheduling considerations — so the delegation is unconditional.

## Construction and Invariants

The constructor immediately asserts `backend_` is non-null via `XRPL_ASSERT`, treating a null backend as a programming error. This is appropriate: a `DatabaseNodeImp` without a backend can serve no purpose, and failing loudly at construction time is preferable to a null-dereference deep in a fetch path. The destructor calls `stop()` (inherited from `Database`) to drain the async read queue before the backend is destroyed, preventing use-after-free in the reader threads.

All `shared_ptr` usage is consistent with XRPL's general pattern: `NodeObject` instances are reference-counted because the same object may be simultaneously referenced by an in-flight read callback, the ledger cache, and a pending write — no single owner exists at the call site level.