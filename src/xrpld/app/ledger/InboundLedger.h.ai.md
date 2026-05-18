# `InboundLedger.h` — Peer-driven Ledger Acquisition State Machine

`InboundLedger` represents a single in-flight attempt to obtain a complete ledger from the XRPL peer network. It exists because a rippled node frequently needs ledger data it does not yet hold locally — during catch-up after a network gap, during consensus validation, or while building historical archives. Each instance encapsulates the full acquisition lifecycle: checking the local node store, requesting data from peers, receiving and processing responses, and finally signaling completion or failure to the rest of the system.

## Inheritance and Object Model

The class inherits from three bases simultaneously:

- **`TimeoutCounter`** supplies the timed retry loop. It owns a `boost::asio` timer set to fire every `ledgerAcquireTimeout` (3 seconds). On each expiry it calls the virtual `onTimer()` hook, which `InboundLedger` overrides to escalate the query strategy. After `ledgerTimeoutRetriesMax` (6) unproductive timeouts the acquisition marks itself failed. `TimeoutCounter` also provides the `complete_` / `failed_` pair and the `recursive_mutex mtx_` used as the primary state-machine lock.

- **`enable_shared_from_this<InboundLedger>`** is required because the object is owned through shared pointers and callbacks — timer closures, job queue lambdas, and peer callbacks — all capture `shared_from_this()`. Without this, the object might be destroyed while an outstanding callback still references it.

- **`CountedObject<InboundLedger>`** provides a global instance counter for diagnostics.

## The Three-Flag State Machine

A ledger has three independently fetchable components, tracked by `mHaveHeader`, `mHaveTransactions`, and `mHaveState`:

1. **Header** (`mHaveHeader`) — the 128-byte record containing the transaction root hash and account state root hash. Nothing else can be fetched without it, because those root hashes are the entry points into the two SHAMaps.

2. **Transaction map** (`mHaveTransactions`) — the complete `SHAMap` of all transactions in the ledger.

3. **Account state map** (`mHaveState`) — the complete `SHAMap` of all ledger state objects.

When both maps are complete, `complete_` is set and `done()` is called. The acquisition is considered finished as soon as all three flags are true; partial success is not possible. Notice the code in `trigger()` fetches state data before transaction data, with an explicit comment: *"it's the most likely to be useful if we wind up abandoning this fetch."* Ledger state is bulkier and more broadly applicable, so it gets priority when network time is limited.

## Acquisition Reason and Its Effect

The `Reason` enum is more than metadata — it actively changes behavior at several key points:

- **`HISTORY`**: Expects a fetch pack to arrive shortly, so `addPeers()` does not immediately call `trigger()` on newly-added peers; `onTimer()` waits for the pack and triggers only after if still needed. On completion, `done()` calls `InboundLedgers::onLedgerFetched()` for rate tracking, but does not attempt to advance the validated ledger.

- **`CONSENSUS`**: This ledger is needed to close or validate the current round. On completion `done()` calls both `LedgerMaster::checkAccept()` and `LedgerMaster::tryAdvance()` via a dispatched job.

- **`GENERIC`**: Behaves like `CONSENSUS` for post-completion processing.

## Dual-Lock Concurrency Design

`InboundLedger` uses two distinct mutexes with carefully separated responsibilities:

- **`mtx_`** (inherited `recursive_mutex`): Guards the entire acquisition state — header flags, SHAMap progress, peer queries, and the `complete_`/`failed_` state. This lock may be held for significant durations, e.g., during `getMissingNodes()` on a large state map — though even that is released temporarily inside `trigger()` to avoid monopolizing it.

- **`mReceivedDataLock`** (`std::mutex`): Guards only `mReceivedData` and `mReceiveDispatched`. This is a plain non-recursive mutex because `gotData()` is called on network I/O threads and must return quickly without blocking on the heavy state-machine lock.

The `mReceiveDispatched` flag is the key to the batching strategy: `gotData()` queues data and returns `true` the first time (telling the caller to dispatch a job to run `runData()`). Subsequent calls return `false` — the already-dispatched job will drain the queue. `runData()` loops until the queue is empty, then clears `mReceiveDispatched`.

## Progressive Query Escalation in `onTimer()`

When no progress is detected (`wasProgress == false`):
1. `checkLocal()` is called first — the node store may have received relevant data since the last check.
2. `mByHash = true` is re-armed, allowing the by-hash request path.
3. Peers are expanded (`addPeers()` adds `peerCountAdd` = 3 more) and existing peers are re-triggered.
4. After `ledgerBecomeAggressiveThreshold` (4) timeouts, `trigger()` switches from SHAMap-node requests to `TMGetObjectByHash` bulk requests, asking all known peers simultaneously for specific missing hashes.

## `runData()` — Batched Response Processing

When peer responses arrive via `gotData()`, they are batched into `mReceivedData`. `runData()` drains this queue in a loop: it swaps the live queue into a local vector (minimizing lock hold time), then calls `processData()` for each entry. After the loop, `dataCounts.prune()` and `dataCounts.sampleN(6, ...)` select up to six of the most productive peers — those that supplied the most useful nodes — and call `trigger()` on each with `TriggerReason::reply`. This directs follow-up requests toward peers that have proven useful, rather than broadcasting to everyone.

## Initialization and Local Lookup

`init()` is called while holding the `InboundLedgers` collection lock, which it releases early via `collectionLock.unlock()` before doing any real work. This pattern limits contention: the collection only needs to be locked long enough to insert the new entry, not during the potentially slow local DB search. `tryDB()` checks the node store and fetch packs for the header, then both SHAMap roots, working entirely from local storage before any peer contact.

## Destructor Salvage

The destructor examines `mReceivedData` for any unprocessed account-state nodes (`liAS_NODE`) and routes them to `InboundLedgers::gotStaleData()`. Account state nodes are generic — they may be valid for other ledger acquisitions — so discarding them would waste data already paid for in network bandwidth.

## Relationship to `InboundLedgers`

`InboundLedgers` is the owning registry (a map from hash to `shared_ptr<InboundLedger>`). It handles deduplication — if the same hash is requested a second time while an acquisition is already in flight, `update()` is called on the existing instance to refresh its sequence number and `mLastAction` timestamp. The `touch()` / `mLastAction` mechanism enables `InboundLedgers::sweep()` to evict stale completed or failed acquisitions without disturbing active ones.