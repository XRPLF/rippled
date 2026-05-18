# `Slot.h` — PeerFinder Connection Slot Interface

`Slot.h` defines the public abstract interface for a single peer-to-peer overlay connection within XRPL's `PeerFinder` subsystem. It is the read-only view of a connection that the rest of the system observes: the `Manager` creates and owns slots internally, but hands `shared_ptr<Slot>` handles outward so callers can query state without being able to mutate it through this interface.

## Role in the System

`PeerFinder` manages the full lifecycle of every TCP connection in the XRPL peer overlay, from initial socket acceptance through handshake completion, active data exchange, and teardown. `Slot` is the per-connection record that carries both the immutable socket identity (remote endpoint, inbound/outbound direction) and the mutable state that evolves as the connection progresses. The abstract interface is deliberately minimal — consumers of `Slot` only ever need to read its properties; all mutations happen through `SlotImp`, the concrete implementation in `detail/`.

## State Machine

The `State` enum encodes five ordered lifecycle phases:

- `accept` — the socket has been accepted (inbound) but no handshake has started yet.
- `connect` — an outbound connection attempt is in progress.
- `connected` — the TCP connection is established; the handshake is underway.
- `active` — the handshake completed successfully and the peer is fully participating.
- `closing` — the connection is winding down.

`Logic.h` enforces the valid state transitions: for example, `activate()` asserts the slot is in `accept` or `connected` before advancing to `active`, and `on_closed()` checks the slot is `active` before updating internal counters. The enum ordering matters because `Logic.h` also uses it in `stateString()` diagnostics.

## Connection Classification Flags

Three boolean properties classify the nature of a slot beyond its current state:

`inbound()` distinguishes who initiated the connection. This feeds the slot counters in `Counts` (which track `in_active` vs. `out_active` separately) and affects peer-privacy rules — only outbound connections are suppressed when `peerPrivate` is enabled. The two `SlotImp` constructors reflect this split: the inbound form accepts both a `local_endpoint` and `remote_endpoint`, while the outbound form only takes the remote address (the local endpoint is filled in later via `onConnected`).

`fixed()` marks connections whose remote address appears in the operator-configured fixed-peers list. Fixed connections are exempt from the normal slot budget enforced by `Counts::can_activate()`, because operators use them to guarantee connectivity to trusted cluster nodes regardless of how full the peer slots are. `Logic.h` treats a disconnected fixed outbound peer as a permanent reconnect obligation.

`reserved()` is set during `activate()` once the handshake reveals the peer's identity. A slot is reserved if the peer is in the cluster or has an explicit reservation. Like `fixed`, reserved slots are not counted against the public slot cap. Crucially, `reserved` is unknown until the handshake completes — the comment on the interface makes this timing dependency explicit, preventing consumers from relying on it pre-handshake.

## Endpoint and Identity Accessors

`remote_endpoint()` returns a concrete `beast::IP::Endpoint` — it is always known from the moment the slot is created. `local_endpoint()` is `optional` because for inbound connections the local address is populated during construction, but for outbound connections it is only filled in when `Manager::onConnected()` fires and the OS-assigned local port can be read from the socket.

`listening_port()` is separately `optional` because it reflects the port the *remote* peer advertises as its inbound listener, which is communicated during the handshake via `mtENDPOINTS`. `SlotImp` stores this as an `atomic<int32_t>` using the sentinel value `-1` for "unknown", enabling lock-free reads from multiple threads while the handshake thread writes it. The `Slot` interface exposes this as `optional<uint16_t>` — hiding the sentinel encoding entirely from consumers.

`public_key()` is `optional<PublicKey>` for the same reason: it is populated only after a successful handshake. `Logic.h` uses it as a deduplication key — if `activate()` detects a key that matches an already-active slot, it returns `Result::duplicatePeer` and the connection is rejected.

## Design: Abstract Interface over `shared_ptr`

The choice of a pure-virtual class rather than a plain struct is deliberate separation-of-concern. The `Manager` interface takes and returns `shared_ptr<Slot>` for all its event callbacks (`on_endpoints`, `on_closed`, `activate`, etc.), allowing the overlay layer to hold references to slots across asynchronous operations without coupling to `SlotImp`'s mutable API. The concrete `SlotImp` adds write-setters, the `recent_t` address-deduplication cache, and anti-flooding timestamps — none of which the caller should touch directly.