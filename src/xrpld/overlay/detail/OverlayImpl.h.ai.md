# `OverlayImpl.h` — Concrete P2P Overlay Manager

`OverlayImpl` is the central class that implements the XRPL node's peer-to-peer layer. It inherits from two bases: the abstract `Overlay` interface (used by the rest of the application to interact with connected peers) and `reduce_relay::SquelchHandler` (a callback interface the squelch/unsquelch machinery calls back into). This dual inheritance separates the public API from the implementation side of the reduce-relay protocol cleanly enough that unit tests can substitute a lighter-weight `SquelchHandler` without instantiating the full overlay.

## Peer Registration and Identity

The class tracks connected peers through two complementary maps. `m_peers` maps a `PeerFinder::Slot` (the peer-discovery layer's notion of a connection slot) to a `weak_ptr<PeerImp>`. `ids_` maps the node-local integer `Peer::id_t` to the same `weak_ptr`. Both deliberately use `weak_ptr` rather than `shared_ptr` to avoid ownership cycles — `PeerImp` objects are reference-counted throughout the system, and the overlay should not artificially extend their lifetimes. The integer ID counter `next_id_` is `std::atomic`, so IDs can be assigned without acquiring the main `mutex_`.

The lifecycle progression is: an incoming or outgoing TCP connection is admitted, a `PeerFinder::Slot` is registered in `m_peers`, and once the TLS/HTTP handshake completes, `activate()` is called to install the peer in `ids_` and assign it a stable short ID. When the peer disconnects, `onPeerDeactivate()` removes the ID entry and `remove()` removes the slot entry separately — the two-step removal matches the two-step registration.

The `for_each()` template is the standard way to visit all active peers. It acquires the mutex only long enough to copy a snapshot of `weak_ptr` handles into a local vector, then releases the lock before iterating. This is intentional: a visitor might attempt to acquire the same mutex (deadlock prevention), and `PeerImp` destruction can invalidate iterators directly on `ids_`.

## Child Lifecycle and the Timer

The nested `Child` class is a registration pattern for sub-objects whose lifetimes are tied to the overlay. Every `Child` holds a reference to `OverlayImpl&` and registers itself in the `flat_map<Child*, weak_ptr<Child>> list_`. A `boost::container::flat_map` is chosen here because the number of children is small and the contiguous storage is cache-friendly for the infrequent iteration during `stopChildren()`.

`Timer` is the only `Child` in normal operation. It inherits both `Child` and `std::enable_shared_from_this<Timer>` so it can extend its own lifetime safely inside the async callback chain. Each tick calls `on_timer()`, which drives the periodic housekeeping — `autoConnect()`, `sendEndpoints()`, `sendTxQueue()`, and `deleteIdlePeers()` — before rescheduling itself. The `stopping_` flag prevents a shutdown race where a pending async wait would otherwise rearm after `stop()` was called.

## Reduce-Relay: Slots and Squelching

The most architecturally significant feature in `OverlayImpl` is the validator message reduce-relay system. The node may receive the same `TMValidation` or `TMProposeSet` from every peer simultaneously; for a well-connected node with dozens of peers, this is severe message amplification.

`reduce_relay::Slots<UptimeClock> slots_` maintains per-validator counting state. When `updateSlotAndSquelch()` is called (by `PeerImp` after relaying a proposal or validation), it forwards the message key, validator public key, and originating peer ID into `Slots::updateSlotAndSquelch()`. The `Slots` object deduplicates calls by key/peer pair and, once a validator's messages have been observed from enough peers, randomly selects a fixed number of "selected" peers and tells the rest to stop relaying by sending `TMSquelch`. The callback for sending those squelch messages is provided by `OverlayImpl::squelch()` and `OverlayImpl::unsquelch()` — private implementations of the `SquelchHandler` interface that look up the target `PeerImp` by ID and dispatch the protocol message.

There are two overloads of `updateSlotAndSquelch()`: one taking `std::set<Peer::id_t>&&` for the broadcast case (multiple peers relayed this message simultaneously) and one taking a single `Peer::id_t` for the common single-peer case to avoid a heap allocation. When `deletePeer()` is called on peer teardown, the `Slots` container must unsquelch all peers that were previously suppressed because of the now-gone selected peer, restoring the counting state for that validator's slot.

## Traffic Accounting and Metrics Export

`TrafficCount m_traffic` classifies every inbound and outbound byte by protocol message type (transaction, proposal, validation, ledger data sub-types, squelch overhead, etc.) using `std::atomic` counters inside each category bucket — no lock is needed for the hot-path increment path.

At collection time, `collect_metrics()` (installed as a `beast::insight::Hook` on the `Collector`) copies the atomic counters into the `Stats::trafficGauges` map under `m_statsMutex`. The reason for the second mutex is that the `TrafficGauges` struct's `beast::insight::Gauge` members are not themselves atomic; they are collector-managed objects that expect single-writer access. The separation of `m_statsMutex` from the main `mutex_` keeps the stats collection path from serializing with the peer management path.

The atomic counters `jqTransOverflow_`, `peerDisconnects_`, and `peerDisconnectsCharges_` serve a similar purpose at a higher level — they are hot-path diagnostic counters that can be bumped from any thread without locking. `peerDisconnectsCharges_` specifically tracks disconnects initiated because of excessive resource consumption, giving operators visibility into whether the resource manager is actively enforcing limits.

The `metrics::TxMetrics txMetrics_` field aggregates rolling-average statistics for the transaction reduce-relay feature (selected peers per relayed tx, suppressed peers, etc.). Its `addTxMetrics()` wrapper checks `strand_.running_in_this_thread()` and posts back to the strand if called off-strand, making `TxMetrics` effectively strand-confined without adding a per-call mutex.

## HTTP Handoff and Crawl Endpoint

Incoming TCP connections share the same listener as the HTTP RPC server. `onHandoff()` routes them: `isPeerUpgrade()` checks for an HTTP Upgrade header (HTTP/1.1 GET with `Connection: upgrade`), and if present the SSL stream is handed to a new `PeerImp` for the full peer handshake. Non-upgrade requests are dispatched to `processRequest()`, which tries `processCrawl()`, `processValidatorList()`, and `processHealth()` in turn. Crawl responses (`/crawl`) return JSON composed from `getOverlayInfo()`, `getServerInfo()`, `getServerCounts()`, and `getUnlInfo()` according to the `[crawl]` config flags. The validator list endpoint (`/vl/<pubkey hex>`) allows external crawlers to fetch the UNL that this node trusts for a given validator public key.

## Manifest Caching

`getManifestsMessage()` returns a cached `shared_ptr<Message>` containing the serialized `TMManifests` payload sent to newly connected peers. The cache is validated by comparing `manifestListSeq_` against the current manifest sequence. `manifestLock_` (a plain `std::mutex`, not the recursive `mutex_` used for peer maps) protects both the message pointer and the sequence number. This avoids re-serializing the full manifest list for every new peer handshake.

## Concurrency Notes

The primary lock is `std::recursive_mutex mutex_`, which protects `m_peers`, `ids_`, and related peer-map state. The inline comment `// VFALCO use std::mutex` acknowledges this as technical debt — recursive mutexes can hide unintended reentrancy. The associated `std::condition_variable_any cond_` (which requires a `Lockable` rather than a plain `BasicLockable`, hence is compatible with the recursive mutex) is used during shutdown to wait for all children to drain. The `work_` guard (`boost::asio::executor_work_guard`) prevents the `io_context` from exiting while the overlay is running, and is released as part of `stop()` to let the context complete its work queue and exit cleanly.