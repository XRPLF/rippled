# `LedgerPersistence.cpp` — Validated Ledger Save and Load

This file implements the persistence bridge between the in-memory `Ledger` object and the node's relational database. It covers two symmetrical concerns: persisting a newly-validated ledger to durable storage, and reconstructing a `Ledger` from stored header records when the node needs historical data. All services are reached through the `ServiceRegistry` abstract interface rather than the concrete `Application` class, which keeps the code testable and decoupled from lifecycle management.

## Save Path: `pendSaveValidated` → `saveValidatedLedger`

The public entry point `pendSaveValidated()` orchestrates the save of a fully-validated ledger. Because ledger validation can produce the same ledger from multiple code paths (consensus, ledger acquisition, replay), it uses two independent deduplication layers before touching the database.

**Layer 1 — hash-level dedup via `HashRouter`**: The first gate calls `registry.getHashRouter().setFlags(ledger->header().hash, HashRouterFlags::SAVED)`. `HashRouter` is an aged unordered map used primarily for P2P message routing; marking a hash as `SAVED` here acts as a lightweight, time-bounded "have I recently handled this exact ledger?" check. If the flag was already set, the function short-circuits with a "Double pend save" log. The nuance is that if the caller requested a synchronous save *and* a save is actually still in flight, it skips this early return and continues — the need for guaranteed completion overrides the dedup.

**Layer 2 — sequence-level concurrency via `PendingSaves`**: After the hash check, `PendingSaves::shouldWork(seq, isSynchronous)` is called. This mutex-protected map tracks every sequence currently being persisted. If no entry exists yet, the method inserts the sequence and returns `true` (proceed). If an entry exists and the caller is asynchronous, it returns `false` (already dispatched, nothing to do). Crucially, if the entry exists but the save is already in progress (`it->second == true`) and the caller is synchronous, `shouldWork` blocks on a `std::condition_variable` until `finishWork` wakes it — this is how a synchronous caller waits for an in-flight background save to complete before returning.

Once both gates pass, `pendSaveValidated` asserts that the ledger is immutable (`XRPL_ASSERT(ledger->isImmutable())`). A mutable ledger being persisted would be a serious protocol error, so this is a hard assertion rather than a soft error return.

The function then prefers to enqueue the actual I/O via the `JobQueue`. Current-ledger saves use `jtPUBLEDGER` and historical backfills use `jtPUBOLDLEDGER` — different job types allow the scheduler to prioritize live consensus work over historical filling. If the `JobQueue` rejects the job (e.g. the queue is at capacity or the system is shutting down), execution falls through to a direct synchronous call.

**Inside `saveValidatedLedger`**: This static function performs the actual write. It calls `PendingSaves::startWork(seq)` as a final concurrency guard at the point of actual database access: `startWork` atomically checks if work is underway and marks it so, returning `false` if another thread already claimed it. This is distinct from `shouldWork`: `shouldWork` manages the *decision* to proceed, while `startWork` manages the *claim* to do the work. After `db.saveValidatedLedger()` completes, `PendingSaves::finishWork(seq)` removes the entry and broadcasts on the condition variable, waking any threads blocked in `shouldWork`.

## Load Path: `loadByIndex`, `loadByHash`, `getLatestLedger`

The three public load functions share a common two-step pattern:

1. Query `RelationalDatabase` for a `LedgerHeader` record using the requested key (sequence number, hash, or newest available).
2. Delegate to `loadLedgerHelper`, then seal the result via `finishLoadByIndexOrHash`.

`loadLedgerHelper` constructs a `Ledger` object by passing the `LedgerHeader` along with `rules`, `fees`, a `NodeFamily` (for the underlying SHAMap storage), and a journal. The `Ledger` constructor signals success or failure through a by-reference `bool loaded` flag — if the object could not be fully constructed from local node storage, `loaded` is left false and the helper immediately resets the `shared_ptr` to null. The `acquire` parameter controls whether the `Ledger` constructor should attempt to fetch missing SHAMap nodes from peers.

`finishLoadByIndexOrHash` finalises a successfully loaded ledger in three ways. It calls `setImmutable()` to prevent further modification (consistent with the save-path assertion), then `setFull()` to mark the object as completely loaded. Between these it also asserts a protocol invariant: any ledger at or beyond `XRP_LEDGER_EARLIEST_FEES` must contain a fee-settings object (`keylet::fees()`). This guards against corrupt or truncated database records slipping into the in-memory ledger cache as apparently-valid objects.

`loadByHash` adds one more assertion after loading: the reconstructed ledger's hash must match the requested hash. This catches any database inconsistency where a stored record is indexed under the wrong hash.

## Design Observations

The separation of `shouldWork` and `startWork` in `PendingSaves` reflects a deliberate two-phase concurrency model: the outer function (`pendSaveValidated`) decides *whether* to start, and the inner function (`saveValidatedLedger`) atomically claims the work slot. This prevents a TOCTOU race where two asynchronous jobs both pass `shouldWork` but only one should do the actual write.

The `ServiceRegistry` abstraction is used consistently — no raw `Application&` references appear here. This is part of an ongoing migration in the rippled codebase away from passing `Application` everywhere; `LedgerPersistence.cpp` is already fully migrated to the service-locator model, which makes the persistence logic independently mockable and testable.