# `LedgerPersistence.h` — Ledger Save and Load Interface

This header declares the five free functions that form the persistence boundary between the in-memory `Ledger` representation and the relational (SQLite) database in the XRPL node. It is intentionally thin — pure declarations in the `xrpl` namespace — with all logic in `detail/LedgerPersistence.cpp`. Callers that need to save a validated ledger or reconstruct one from stored header information use only this interface.

## Why This Exists as a Separate File

The ledger subsystem separates construction, validation, persistence, and history into distinct modules. `LedgerPersistence.h` specifically owns the handoff point where a fully-validated, immutable `Ledger` transitions to durable storage and the reverse path where a stored `LedgerHeader` is promoted back to a live in-memory ledger. Keeping this in its own translation unit means the heavy relational-database and job-queue dependencies stay out of the headers that only need to work with in-memory ledgers.

## Saving: `pendSaveValidated` and its Internal Pipeline

`pendSaveValidated` is the most architecturally significant function in the file. It accepts a `shared_ptr<Ledger const>`, meaning the ledger must already be immutable before any save begins — an assertion in the implementation enforces this invariant.

The function implements a three-layer duplicate-suppression scheme before any work touches the database:

1. **Hash-router deduplication.** `HashRouter::setFlags(hash, SAVED)` is called first. The hash router is normally used for P2P message deduplication, but here it serves as a lightweight, per-hash guard. If the flag was already present, either the save completed or is in progress; the function can return early unless the caller explicitly needs synchronous completion.

2. **`PendingSaves` coordination.** `PendingSaves` tracks in-flight saves by sequence number with a mutex-protected map of `LedgerIndex → bool` (false = scheduled, true = in progress). `shouldWork()` either registers the sequence and returns true, detects that another thread already dispatched it, or — if the caller specified `isSynchronous` and the work is actively running — blocks on a condition variable until the other thread calls `finishWork()`. This ensures that a synchronous caller never returns until the database write actually completes.

3. **Job queue dispatch.** If the caller does not need synchronous completion and the job queue accepts the work, the save is dispatched as either `jtPUBLEDGER` (current validated ledger, higher latency limits) or `jtPUBOLDLEDGER` (historical, with a 2-thread concurrency cap and 10-second latency budget). The distinction matters because current-ledger publishing drives the live RPC feed and must not be throttled as aggressively as background catchup work. If the job queue refuses (e.g., it is shutting down), the call falls back to an immediate synchronous save in the caller's thread.

The internal static `saveValidatedLedger` function brackets the actual `RelationalDatabase::saveValidatedLedger()` call with `PendingSaves::startWork()` / `finishWork()`. `startWork` returns false if another thread raced in and started work first, allowing a clean short-circuit before any database I/O.

## Loading: `loadLedgerHelper`, `loadByIndex`, `loadByHash`

`loadLedgerHelper` is the common primitive. It constructs a `Ledger` from a `LedgerHeader` struct obtained from the database and a `NodeFamily` for the underlying node store. The `acquire` flag controls whether the `Ledger` constructor should attempt to fetch missing SHAMap nodes from peers if they are not in local storage. If the Ledger constructor sets the `loaded` boolean to false — indicating the state map could not be retrieved — the returned `shared_ptr` is reset to nullptr. All callers must check for null returns.

`loadByIndex` and `loadByHash` follow the same two-step pattern: query the relational database for the `LedgerHeader`, delegate to `loadLedgerHelper`, then call the internal `finishLoadByIndexOrHash`. That finisher validates a fee-entry invariant (the `fees` keylet must exist for any ledger at or above the `XRP_LEDGER_EARLIEST_FEES` sequence), then calls `setImmutable()` and `setFull()` on the ledger, making it safe to hand to callers. `loadByHash` adds a debug assertion that the loaded ledger's hash matches what was requested — a sanity check against database inconsistency.

The `rules` and `fees` parameters passed to every load function serve as bootstrap defaults fed into the `Ledger` constructor. The actual values may be overwritten when the ledger's state map is read, but during startup or in edge cases where the state is inaccessible, these defaults prevent an uninitialized object. In practice, callers derive them from the node config: `Rules{config_->features}` and `config_->FEES.toFees()`.

## `getLatestLedger` — Startup Restart Point

`getLatestLedger` returns a three-element tuple `(ledger, seq, hash)` representing the highest-sequence ledger stored in the database. If the database is empty, all three elements are default-initialized (nullptr, 0, zero hash). The primary caller is `Application.cpp` during startup, where it determines where to resume processing after a restart. Unlike the index/hash loaders it does not call `finishLoadByIndexOrHash`, delegating that finalization to the `loadLedgerHelper` call itself, and the ledger is returned still allowing further mutation at the startup path.

## Dependency Injection via `ServiceRegistry`

All five functions take `ServiceRegistry&` rather than accepting individual service references. The `ServiceRegistry` virtual interface exposes `getRelationalDatabase()`, `getPendingSaves()`, `getHashRouter()`, `getJobQueue()`, and `getNodeFamily()`. This keeps the persistence functions decoupled from concrete application types and makes the functions testable by injecting a mock registry. The `Application` class implements `ServiceRegistry`, so production call sites simply pass `app_` or `*this`.