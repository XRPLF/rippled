# `Overlay.h` — Abstract Peer Network Interface

`Overlay.h` defines the `Overlay` abstract class, the single point of authority for everything related to the XRPL peer-to-peer mesh. Every other subsystem that needs to talk to, query, or broadcast across peers does so through this interface. The concrete implementation lives in `detail/OverlayImpl.h` and is created exclusively via the `make_Overlay()` factory declared in `make_Overlay.h`, keeping the full implementation hidden from callers.

## Position in the Architecture

The class inherits from `beast::PropertyStream::Source`, registering itself under the name `"peers"` so that the server's diagnostics tree can walk into it without any additional wiring. The constructor comment acknowledges this inheritance is an "unfortunate problem with the API" — `PropertyStream::Source` demands the name at construction time, forcing the otherwise default-constructible base to be explicit. This is a rare place where a design constraint from a utility library bleeds into the abstraction boundary.

The `Setup` struct collects all configuration that must be known at construction time: a shared SSL context (shared because multiple peer connections reuse the same TLS settings), the server's public IP address, a per-IP connection limit (`ipLimit`), crawl options controlling what the `/crawl` endpoint exposes, an optional `networkID`, and a `vlEnabled` flag that controls Validator List propagation. Separating this into a value type rather than passing parameters through `make_Overlay` keeps the factory signature stable as configuration grows.

## Connection Lifecycle

`onHandoff()` is the entry point for inbound connections. The HTTP server layer receives a raw TLS stream and an HTTP upgrade request, and calls this method to decide whether the request belongs to the overlay. The method receives ownership of the `ssl_stream` via `unique_ptr` and returns a `Handoff` indicating whether it accepted the connection; if it declines, the caller handles it as a regular HTTP request. The asymmetric design — accepting an owning pointer rather than a reference — ensures the stream cannot be used by the caller after a successful handoff.

Outbound connections are initiated by `connect()`, which is fire-and-forget: it schedules an asynchronous connection attempt and returns immediately. The caller has no way to observe the result through this interface; success or failure is handled inside the overlay and reflected in the peer count.

## Peer Discovery and Access

`size()` returns only peers that have completed the peer protocol handshake, not those mid-negotiation. This distinction matters for capacity planning: `limit()` returns the configured maximum, and the gap between `limit()` and `size()` represents available slots.

`getActivePeers()` takes a snapshot of the current peer list at the moment of the call, returning a `std::vector<std::shared_ptr<Peer>>`. The snapshot design is deliberate: it avoids holding an internal lock across the caller's iteration, trading consistency (a peer might disconnect between the snapshot and a subsequent call) for safety and simplicity. The `foreach()` template is built directly on top of `getActivePeers()` and exists purely as a convenience wrapper; it is non-virtual because the snapshot semantics are defined entirely by `getActivePeers()`.

`findPeerByShortID()` and `findPeerByPublicKey()` allow callers to retrieve a specific peer. The short ID (`Peer::id_t`, a `uint32_t`) is an ephemeral connection-local identifier that is stable only for the lifetime of the connection. The public key lookup is used by the consensus and validation layers, which operate in terms of validator identity rather than connection identity.

## Broadcast vs. Relay

The interface distinguishes two dissemination modes with different semantics:

`broadcast()` sends a `TMProposeSet` or `TMValidation` message to every active peer without any deduplication logic. This is used when the node itself originates the message.

`relay()` is for forwarding a message received from a peer. It takes the serialized message, a `uint256` deduplication key, and the validator's public key. It returns the set of `Peer::id_t` values that already sent the node this exact message — in other words, the peers that already have it and do not need it forwarded back. The returned set enables the caller to track which peers contributed to coverage without re-broadcasting to them. This is the squelch/reduce-relay mechanism described in the [XRPL blog post on message routing optimizations](https://xrpl.org/blog/2021/message-routing-optimizations-pt-1-proposal-validation-relaying.html): rather than flooding every peer unconditionally, the overlay selects a small number of "source" peers per validator and temporarily squelches the rest.

Transaction relay uses a third overload with a different signature: it takes the transaction hash, an `optional<reference_wrapper<TMTransaction>>` for the full message (which may be absent if only the hash is being queued), and a `toSkip` set of peers that have already seen it. When the tx reduce-relay feature is active, the overlay randomly selects a subset of peers to receive the full message immediately and queues the hash for the remainder, to be flushed later via `Peer::sendTxQueue()`.

## Telemetry Counters

The increment/get pairs for `jqTransOverflow`, `peerDisconnect`, and `peerDisconnectCharges` are deliberately split rather than combined into a single atomic fetch-and-increment. This allows monitoring code to read the current total without side effects, while producers only call the increment variant. The `peerDisconnectCharges` counter specifically tracks disconnections triggered by the resource charging system — cases where a peer was consuming excessive resources rather than disconnecting due to normal network events.

## Network Partitioning

`networkID()` returns an optional `uint32_t` identifying which network the node belongs to (0 = mainnet, 1 = testnet, 2 = devnet). During the peer handshake, both sides exchange their network ID, and a mismatch causes the connection to be rejected. This is the primary mechanism preventing stale or misconfigured testnet nodes from accidentally joining mainnet and consuming relay bandwidth.

## `Promote` Enum

The `Promote` enum (`automatic`, `never`, `always`) controls whether an incoming connection is eligible for promotion from an outbound-only slot to a full bidirectional peer slot. It is defined on `Overlay` rather than on `Peer` because the promotion decision is a policy of the overlay as a whole, not of the individual connection.