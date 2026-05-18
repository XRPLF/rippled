# `InboundLedgers.cpp` — Ledger Acquisition Manager

This file contains the sole concrete implementation of the `InboundLedgers` interface, hidden behind a factory function. Its job is to manage the *collection* of in-flight ledger acquisitions: creating them, deduplicating requests, routing incoming peer data to the right acquisition, and eventually evicting stale ones. Individual acquisition mechanics live in `InboundLedger`; this file is the registry and coordinator that sits above it.

## Architecture: Factory-Hidden Implementation

`InboundLedgersImp` is defined entirely within this `.cpp` file — it is never exposed in a header. Only `make_InboundLedgers()` is visible to the rest of the application, returning a `std::unique_ptr<InboundLedgers>`. This is a deliberate encapsulation: callers depend only on the abstract interface, and the entire implementation can change without touching consumers. The same pattern appears throughout the XRPL codebase (e.g., `PeerSetBuilderImpl`).

## Acquiring a Ledger: Synchronous vs. Async

`acquire()` is the workhorse. It wraps its logic in a `doAcquire` lambda measured by `perf::measureDurationAndLog` — any call exceeding 500 ms is logged as a warning, since ledger acquisition is expected to be fast (it either finds an existing entry or schedules network work and returns immediately). The real work inside the lock is minimal: look up the hash in `mLedgers`, and if missing, construct a new `InboundLedger` and call `init()` on it while still holding the lock. The return value is non-null only if the ledger is already `isComplete()`, so in practice `acquire()` usually returns `{}` and the actual ledger arrives later through async callbacks.

`acquireAsync()` adds an important layer: a `pendingAcquires_` set guarded by its own `acquiresMutex_`. When called from a job queue thread, this prevents two concurrent jobs from both entering `acquire()` for the same hash simultaneously. The pattern is careful: the outer lock is taken, the hash is inserted, then `scope_unlock` temporarily releases `acquiresMutex_` for the actual `acquire()` call, and finally the hash is erased unconditionally after — even if `acquire()` throws an exception. This prevents the `pendingAcquires_` set from leaking hash entries when exceptions propagate.

The two locks — `acquiresMutex_` (non-recursive `std::mutex`) and `mLock` (recursive `std::recursive_mutex`) — guard orthogonal state and are never held simultaneously in a way that creates deadlock risk. `acquiresMutex_` exists solely to deduplicate async calls and is released before `mLock` is ever taken.

## Reason Filtering at Bootstrap

When `app_.getOPs().isNeedNetworkLedger()` is true, the node is still bootstrapping and only allows acquisitions with `Reason::GENERIC` or `Reason::CONSENSUS`. History backfill (`Reason::HISTORY`) is silently suppressed. This prevents a newly-joined node from flooding peers with history requests before it has established its current position in the network — a bandwidth protection heuristic noted in the source with the candid comment "probably not the right rule."

## Routing Incoming Peer Data

`gotLedgerData()` is called when a `TMLedgerData` protocol message arrives from a peer. It looks up the ledger hash in `mLedgers` via `find()`. If found, it hands the data to `InboundLedger::gotData()`, and if that call returns `true` (meaning processing hasn't already been dispatched), it enqueues a `jtLEDGER_DATA` job to call `InboundLedger::runData()`. This decoupling matters: peer message handlers run on network threads and must return quickly; the actual node processing happens on the job queue.

If the ledger is no longer tracked (acquisition was completed or swept), and the incoming packet contains account-state nodes (`liAS_NODE`), the method still dispatches a job for `gotStaleData()`. This is a bandwidth-recovery optimization: the node already paid network cost to receive these nodes, so they are deserialized from wire format, re-serialized with the canonical prefix format, and stored in `LedgerMaster`'s fetch pack for future use. Transaction nodes are discarded because they are less reusable across ledger boundaries.

## Failure Tracking and Reacquisition Throttling

`mRecentFailures` is a `beast::aged_map<uint256, uint32_t>` — a time-aware associative container. Failed acquisition hashes are recorded with `logFailure()` and expire after `kReacquireInterval` (5 minutes). Before any `isFailure()` check, `beast::expire()` sweeps stale entries, making the cache self-maintaining without a dedicated cleanup job. The same expiry call appears at the end of `sweep()` to keep failures pruned in sync with the ledger map.

## Fetch Rate Measurement

The `fetchRate_` member is a `DecayWindow<30, clock_type>` — a 30-second half-life exponential decay tracker. `onLedgerFetched()` increments it, and `fetchRate()` returns its current rate scaled to per-minute by multiplying by 60 (`60 * value / 30`). This rate is only meaningful for history ledger fetches; the comment in the header makes clear that callers should only invoke `onLedgerFetched()` for `Reason::HISTORY` acquisitions. The `fetchRateMutex_` is a separate non-recursive mutex dedicated solely to `fetchRate_`, avoiding contention with the main `mLock` during the rate query path.

## Sweeping Stale Acquisitions

`sweep()` runs periodically to remove acquisitions that have been idle for more than one minute. The design is deliberate about lock scope: it holds `mLock` only long enough to collect stale `InboundLedger` shared pointers into a local `stuffToSweep` vector, then drops the lock before the vector goes out of scope. This means `InboundLedger` destructors run without `mLock` held, avoiding potential deadlocks if those destructors attempt any re-entrant operations. The comment `// shouldn't cause the actual final delete` notes that the `stuffToSweep` vector maintains a reference, preventing premature destruction during the erase.

## Shutdown Safety

`stop()` acquires `mLock`, sets `stopping_ = true`, and clears both `mLedgers` and `mRecentFailures`. Any `acquire()` call racing with `stop()` will see `stopping_` inside the same lock scope and return `{}`. This provides a clean shutdown fence: once `stop()` returns, no new `InboundLedger` objects will be created through this manager.