# `OverlayImpl.cpp` — XRPL Overlay Network Implementation

## Role and Purpose

`OverlayImpl.cpp` contains the concrete implementation of the `Overlay` abstract interface, making it the operational heart of an XRPL node's peer-to-peer network layer. The class manages the full lifecycle of peer connections — from initial TCP acceptance and HTTP upgrade through cryptographic handshake, protocol negotiation, active message relay, and eventual teardown. It also exposes three internal HTTP endpoints (`/crawl`, `/health`, `/vl/`) that the network topology tools, monitoring systems, and validator list clients call directly.

The file runs to roughly 1,550 lines and implements `OverlayImpl`, `OverlayImpl::Child`, `OverlayImpl::Timer`, and the free functions `setup_Overlay` and `make_Overlay`.

---

## The `Child` Lifetime Mechanism

`OverlayImpl::Child` is a lightweight RAII base class for any object that lives inside the overlay but is owned independently: `Timer`, `PeerImp`, and `ConnectAttempt` all inherit from it. Its destructor calls `overlay_.remove(*this)`, which erases the child from the `list_` flat-map and, if the map becomes empty, signals `cond_` to wake the thread blocked in `stop()`.

This pattern means `stop()` can simply dispatch `stopChildren()` to the strand, then block on `cond_` until the list drains — every child cleans itself up. `stopChildren()` copies all child pointers into a local `vector<shared_ptr<Child>>` before calling `stop()` on any of them, avoiding iterator invalidation since a child's `stop()` may immediately trigger its destructor and a re-entrant call to `OverlayImpl::remove`.

---

## Per-Second Timer

`OverlayImpl::Timer` inherits from both `Child` and `enable_shared_from_this`. It schedules itself with a one-second `boost::asio::steady_timer` bound to the overlay's dedicated strand. Each `on_timer` invocation:

1. Calls `m_peerFinder->once_per_second()` for peer-discovery bookkeeping.
2. Calls `sendEndpoints()` to push freshly computed endpoint advertisements to peers.
3. Calls `autoConnect()` to open new outbound connections as suggested by PeerFinder.
4. Optionally calls `sendTxQueue()` when the TX reduce-relay feature is enabled.
5. Every four ticks (`Tuning::checkIdlePeers = 4`), calls `deleteIdlePeers()` to purge stale squelch slots.

The timer reschedules itself at the end of each successful tick. On error, it distinguishes between a deliberate `operation_aborted` cancel (silent) and a genuine ASIO error (logged). The `stopping_` flag ensures no rescheduling occurs once `stop()` has been called on the same strand — these two always run sequentially by design.

---

## Inbound Handshake: `onHandoff`

When the HTTP server routes a new TCP connection to the overlay, `onHandoff()` takes ownership of the raw TLS stream. It first passes the request through `processRequest()` to handle `/crawl`, `/health`, and `/vl/` — these return immediately without creating a peer. If the connection is not an HTTP upgrade, it is also returned unhandled.

For genuine peer upgrade requests, the method performs several gating checks in sequence:

- Resource limit: `m_resourceManager.newInboundEndpoint()` disconnects abusive IPs before any further work.
- Slot availability: `m_peerFinder->new_inbound_slot()` enforces per-IP connection limits and self-connection prevention.
- Protocol version: `negotiateProtocolVersion()` verifies the `Upgrade` header lists a mutually supported XRPL wire version.
- Security cookie: `makeSharedValue()` extracts the TLS channel binding value used to prevent man-in-the-middle attacks.
- Cryptographic handshake: `verifyHandshake()` validates the node's signature and network ID from the HTTP headers.

Only after all checks pass is a `PeerImp` created and inserted into `m_peers` (slot-keyed) and `list_`. The critical comment at the insertion site explains why `peer->run()` must be called while holding `mutex_`: without the lock, a concurrent `stop()` could drain the list and return before the new peer is running, leaving orphaned I/O.

On any failure, the slot is released via `m_peerFinder->on_closed(slot)` to keep PeerFinder's accounting accurate, and the appropriate HTTP 400 or 503 response is returned.

---

## Two-Phase Peer Registration

There are two peer registries: `m_peers` maps `PeerFinder::Slot → weak_ptr<PeerImp>`, and `ids_` maps `Peer::id_t → weak_ptr<PeerImp>`. Only `ids_` is used for broadcast and relay operations.

For inbound peers, `m_peers` is populated in `onHandoff` but `ids_` is not added until `activate()` is called after the peer completes its protocol handshake. For outbound peers handled by `ConnectAttempt`, both maps are populated together in `add_active()`. The split exists because `m_peers` is needed for PeerFinder slot management before a public key is known, while `ids_` represents the set of peers ready to receive and forward protocol messages.

---

## Message Relay and Broadcast

Proposals and validations have two dispatch paths. `broadcast()` sends directly to all active peers with no deduplication. `relay()` consults `HashRouter::shouldRelay()` first: if the message has already been relayed (hash seen), it returns an empty set and nothing is sent. If it should be relayed, the set of peers that already sent it is excluded from forwarding, which the caller uses to track which peers to skip in future rounds.

Transaction relay (`relay(uint256, optional<TMTransaction>, set<id_t>)`) is more involved. Pseudo-transactions are never relayed. If `TX_REDUCE_RELAY_ENABLE` is off, the transaction goes to all peers not in `toSkip`. When reduce-relay is active and the peer count exceeds a threshold, a quota of enabled peers is computed from `TX_REDUCE_RELAY_MIN_PEERS` and `TX_RELAY_PERCENTAGE`. Peers with the feature disabled always receive the full message for backward compatibility. Peers above the quota receive only the hash via `addTxQueue()`, which is later served by periodic `sendTxQueue()` pulls. The peer list is shuffled with `default_prng()` before selection to avoid systematic bias.

---

## Squelch System

The squelch system addresses validator message flooding. When many peers independently relay the same validator's messages, `updateSlotAndSquelch()` selects a subset of "selected" source peers and sends `TMSquelch` messages instructing the remaining peers to stop forwarding that validator's messages for a specified duration.

All squelch logic is routed through the overlay strand via `post(strand_, ...)`. This is non-negotiable: `Slots<UptimeClock>` is not thread-safe, and multiple relaying peers on different threads would otherwise race. The overloads accept either a full set of peers (batch update) or a single peer ID, but both funnel into `slots_.updateSlotAndSquelch()`. The `squelch()` and `unsquelch()` methods look up the peer by short ID and send the serialized `makeSquelchMessage()` protobuf directly.

---

## HTTP Endpoints

`processCrawl()` serves `/crawl` with a JSON document whose content is governed by a bitmask (`CrawlOptions::Overlay | ServerInfo | ServerCounts | Unl`). Peer entries include public key, direction, uptime, and optionally IP/port if the peer's crawl flag permits.

`processHealth()` implements a three-tier classification (healthy / warning / critical) based on ledger age, peer count, server state, amendment status, and load factor. Crucially, the HTTP status code itself encodes the result — 200, 503, or 500 respectively — so a load balancer can gate on status without parsing JSON.

`processValidatorList()` serves `/vl/<key>` and optionally `/vl/<version>/<key>`, returning a signed validator list blob fetched from `ValidatorList`. A 404 is returned if the key is not recognized.

---

## Bootstrap and PeerFinder Integration

On `start()`, `PeerFinder::Config` is built from the application config, then `m_peerFinder` is configured and started. Bootstrap IPs come from `[ips]` in the config, falling back to `[ips_fixed]`, and if both are empty, to four hardcoded well-known nodes operated by Ripple Labs, ISRDC, XRPL Kuwait, and XRPL Commons. All resolution is asynchronous via `m_resolver.resolve()`, which populates PeerFinder's fallback list once DNS results arrive. Fixed peers (`[ips_fixed]`) are registered separately as always-reconnect entries.

`autoConnect()` and `sendEndpoints()` are thin wrappers that delegate to PeerFinder and then dispatch the resulting actions to individual peers. This keeps topology policy inside PeerFinder and mechanism inside `OverlayImpl`.

---

## Concurrency Notes

The `mutex_` is declared as `std::recursive_mutex` — a known technical debt acknowledged by a `// VFALCO use std::mutex` comment. The recursion arises from paths where `onHandoff` or `add_active` calls `peer->run()` while holding the lock, and `run()` may trigger I/O that eventually calls back into the overlay. The overlay strand handles timer and squelch work independently; the mutex guards the peer registries accessed from multiple application threads. The `work_` optional executor guard keeps the `io_context` alive until `stopChildren()` sets it to `std::nullopt`.

## `setup_Overlay` and `make_Overlay`

`setup_Overlay()` parses the `[overlay]`, `[crawl]`, `[vl]`, and `[network_id]` config sections into an `Overlay::Setup` struct, creating an SSL context and validating all fields (rejecting private IPs as `public_ip`, rejecting negative IP limits). It maps the symbolic network names `main`, `testnet`, and `devnet` to their numeric IDs 0, 1, and 2. `make_Overlay()` is a factory function that constructs an `OverlayImpl` and returns it as `unique_ptr<Overlay>`, keeping the concrete type out of translation units that only need the interface.