# TransactionAcquire.cpp

## Role in the System

During XRPL consensus, every validating node must hold the same candidate transaction set — a `SHAMap` identified by a 256-bit hash — before it can vote. When a node learns about a transaction set it doesn't yet possess, `TransactionAcquire` is the object that orchestrates its retrieval from the peer network. It knows nothing about ledger accounting or consensus logic; its sole job is to reconstruct a complete, valid `SHAMap` by fetching missing tree nodes from connected peers, then hand the finished map off to `InboundTransactions::giveSet()`.

## Inheritance and Ownership

`TransactionAcquire` inherits from `TimeoutCounter`, which wires an Boost.ASIO steady timer to the job queue. Every `TX_ACQUIRE_TIMEOUT` (250 ms) the job queue fires `onTimer()`, letting the object either retry failed peer requests or declare the acquisition permanently failed. The class also inherits `enable_shared_from_this` so that it can safely produce `weak_ptr<TimeoutCounter>` for the timer callback — avoiding a use-after-free if the object is destroyed between the timer firing and the callback running.

The `PeerSet` is owned exclusively via `unique_ptr` inside the object. This is the overlay abstraction that knows how to select peers and send `TMGetLedger` protobuf messages to them; `TransactionAcquire` never manages peer connections directly.

## SHAMap Construction Strategy

The acquired map is created in the constructor as an in-memory, `setUnbacked()` `SHAMap` of type `TRANSACTION`. Marking it unbacked is crucial: it tells the map not to write newly received nodes into the node database (the SQLite or RocksDB store), because this is a transient consensus artifact, not a persisted ledger object.

The acquisition proceeds in two phases, enforced by the `mHaveRoot` flag:

1. **Root phase.** Until the root node is received, `trigger()` sends a `TMGetLedger` message with `liTS_CANDIDATE` type, requesting the root node explicitly (`SHAMapNodeID().getRawString()` encodes the root). A `querydepth` of 3 hints to the responding peer that the requester probably needs the entire subtree, enabling bulk delivery.

2. **Node-fill phase.** Once the root is present, `trigger()` calls `mMap->getMissingNodes(256, &sf)` to enumerate up to 256 missing interior or leaf nodes and requests them all in a single message. This repeats every timeout cycle or whenever new data arrives, until `getMissingNodes` returns an empty list.

This two-phase design is necessary because a `SHAMap` cannot enumerate missing descendants without a root node. Requesting the root first gives the map enough structure to traverse its gaps.

## Data Ingestion: `takeNodes()`

`takeNodes()` is called by the network layer when a peer responds with node data. It holds the object's `recursive_mutex` for its entire duration, preventing concurrent timer events from interfering with the map being modified.

For each incoming `(SHAMapNodeID, Slice)` pair, the method routes to either `addRootNode()` or `addKnownNode()`, updating `mHaveRoot` on the first successful root addition. `ConsensusTransSetSF` is passed as the sync filter: it consults the application's `TempNodeCache` on `getNode()` (so locally cached nodes don't need re-fetching) and populates that cache on `gotNode()` (so future fetches can be served locally). If any node is rejected by the map, the method returns `SHAMapAddNode::invalid()`, signalling a misbehaving peer to the caller.

After processing the batch, `takeNodes()` calls `trigger(peer)` on the same peer to immediately ask for the next wave of missing nodes. This keeps the pipeline full rather than waiting for the next 250 ms timeout.

## Timeout and Retry Policy

`onTimer()` implements a two-threshold policy controlled by the constants `NORM_TIMEOUTS = 4` and `MAX_TIMEOUTS = 20`:

- Below 4 timeouts, only `addPeers(1)` is called — one additional peer is recruited on each cycle, progressively widening the query surface without flooding the network immediately.
- At or after 4 timeouts, `trigger(nullptr)` is also called, broadcasting the request to all peers in the current set rather than targeting a specific one.
- After 20 timeouts (~5 seconds of 250 ms intervals) with no completion, `failed_` is set and `done()` is called.

When `timeouts_ >= NORM_TIMEOUTS`, the outbound `TMGetLedger` messages also set `qtINDIRECT`, instructing the receiving peer to relay the request to peers it knows that have the data. This is an escalation from direct peer-to-peer retrieval to a gossip-style fan-out.

## `done()` and the Lock Constraint

A key comment in `done()` explains a subtle constraint: `done()` is called while still holding the `PeerSet` lock, so it cannot perform "real work" directly. Instead, if acquisition succeeded, it makes the map immutable (`setImmutable()`) and schedules a `jtTXN_DATA` job on the application job queue to call `InboundTransactions::giveSet()`. The captures are by value — `hash` and `map` as `shared_ptr` — ensuring the data remains valid even if the `TransactionAcquire` object is destroyed before the job runs. The comment explicitly acknowledges that job queue rejection during shutdown is acceptable: `giveSet()` is only needed to update in-progress consensus state.

## `stillNeed()`: Reset Without Restart

`stillNeed()` is called when consensus determines it still requires a transaction set that was previously being acquired but may have timed out. Rather than destroying and recreating the `TransactionAcquire`, this method clamps `timeouts_` back down to `NORM_TIMEOUTS` and clears `failed_`, effectively granting the acquisition more time without losing any nodes already collected in `mMap`. This avoids redundant network traffic and is safe because the map was `setUnbacked()` — no database state needs to be rolled back.

## Concurrency Summary

The `recursive_mutex mtx_` (inherited from `TimeoutCounter`) protects `mHaveRoot`, `mMap`, `complete_`, `failed_`, and `timeouts_` against concurrent access by the timer job and the network I/O thread calling `takeNodes()`. The `pmDowncast()` override returning a `weak_ptr<TimeoutCounter>` is the mechanism by which the timer avoids holding a strong reference — if consensus discards the `TransactionAcquire` before the timer fires, the weak pointer expires and the callback silently no-ops.