# `src/xrpld/overlay/Peer.h` — Abstract Peer Interface

## Role in the System

`Peer.h` defines the pure abstract interface that represents a single authenticated, handshake-complete peer connection in the XRPL overlay network. It sits at the heart of the overlay subsystem: `Overlay` manages a collection of these objects, `predicates.h` filters and iterates over them, and the concrete implementation `PeerImp` (in `detail/`) provides the actual async I/O machinery. The entire rest of the codebase never needs to include `PeerImp.h` — they work exclusively through this interface.

This separation is intentional. `PeerImp` is a substantial class with a complex multi-stage SSL shutdown state machine, boost circular buffers, shared mutexes, and protocol-buffer handling. Exposing a clean interface here means calling code can route messages, query ledger state, and apply resource charges without being coupled to that complexity.

## Ownership and Identity

The `using ptr = std::shared_ptr<Peer>` alias makes shared ownership the canonical handle for a peer. This is important because a peer's lifetime is inherently uncertain — it can disconnect at any moment — and multiple subsystems (the overlay, consensus, transaction routing) hold references concurrently.

The companion `using id_t = std::uint32_t` establishes a separate, stable numeric identifier. The comment is explicit: this integer *can be stored in tables* and outlives the peer object itself. When a subsystem stores a `Peer::id_t` rather than a `Peer::ptr`, it avoids accidentally extending the peer's lifetime while still having a handle it can later use with `Overlay::findPeerByShortID()` to check whether the peer is still live. `predicates.h`'s `peer_in_set` relies on exactly this: it holds a `std::set<Peer::id_t>` to express "relay to these peers and no others," which is safe across disconnect events in a way that a set of raw pointers would not be.

## Protocol Feature Negotiation

The `ProtocolFeature` enum enumerates opt-in capabilities that a peer may or may not have negotiated during the handshake:

- `ValidatorListPropagation` and `ValidatorList2Propagation` — whether the peer can receive newer validator list formats
- `LedgerReplay` — support for targeted ledger replay requests

`supportsFeature(ProtocolFeature)` lets the overlay consult per-peer capability before sending a message type the peer can't handle. This is a backward-compatibility mechanism: the same `Peer` interface serves peers running different protocol versions.

## Transaction Reduce-Relay

Three methods — `addTxQueue()`, `sendTxQueue()`, and `removeTxQueue()` — form the transaction reduce-relay interface. Rather than broadcasting a full transaction to every peer the moment it arrives (a flooding approach), the reduce-relay optimization accumulates transaction hashes in a per-peer queue and ships them in a single batch. `ReduceRelayCommon.h` defines the policy constants: a maximum queue size of 10,000 hashes, and timing thresholds for selecting which peers receive full transactions versus just hash notifications. The `txReduceRelayEnabled()` predicate gates all of this per-peer so the feature degrades gracefully when talking to older peers that don't support it. `compressionEnabled()` serves the same gating role for LZ4 payload compression.

## Resource Charging

`charge(Resource::Charge const& fee, std::string const& context)` adjusts a peer's load balance score when it imposes cost on the local node — for example, by sending malformed messages, triggering expensive validation, or causing job queue overflows. The `context` string (added to aid diagnostics) identifies what triggered the charge, which flows into `Overlay`'s `incPeerDisconnectCharges()` accounting when a peer is finally disconnected for excessive consumption. This is the load-shedding mechanism that protects the node from abusive peers.

## Ledger Synchronization Queries

The ledger-related methods reflect the state the peer has advertised about its own ledger history:

- `getClosedLedgerHash()` / `hasLedger()` / `ledgerRange()` — expose what the peer claims to have, used when requesting missing ledger data from the network
- `hasTxSet()` — checks whether this peer has a specific transaction set (SHAMap), relevant during consensus
- `cycleStatus()` / `hasRange()` — support the peer tracking system that monitors whether a peer is following the current ledger

## Validator List Sequence Tracking

`publisherListSequence()` and `setPublisherListSequence()` maintain a per-peer, per-publisher sequence number. The squelch system in `Slot.h` uses this to ensure that once a peer has propagated a given validator list version, it isn't unnecessarily re-sent. The keying by `PublicKey` allows tracking across multiple independent validator list publishers simultaneously.

## Relationship to `predicates.h`

`predicates.h` demonstrates how this interface is consumed in practice. The `send_always`, `send_if_pred`, and `send_if_not_pred` function objects wrap `peer->send(msg)` and combine with predicates like `peer_in_cluster` (testing `peer->cluster()`) and `peer_in_set` (testing `peer->id()`). The `Overlay::foreach()` template ties this together, iterating a snapshot of active peers and applying the visitor. This pattern keeps broadcast logic compositional and testable without binding it to `PeerImp`.