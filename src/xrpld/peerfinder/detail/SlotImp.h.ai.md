# `SlotImp.h` — Concrete Peer Connection Slot

`SlotImp` is the concrete implementation of the abstract `Slot` interface in the `xrpl::PeerFinder` subsystem. Where `Slot` defines the read-only query surface exposed to the rest of the codebase, `SlotImp` carries the mutable state, the state-machine logic, and the per-peer bookkeeping that the `Logic` class relies on to manage the node's peer topology.

## Role in PeerFinder

`Logic` maintains a `std::map<beast::IP::Endpoint, std::shared_ptr<SlotImp>>` as its primary slot registry. When a new TCP connection is accepted or an outbound attempt is initiated, `Logic::new_inbound_slot()` or `Logic::new_outbound_slot()` constructs a `SlotImp` and inserts it there. All subsequent lifecycle calls — `onConnected`, `activate`, `on_endpoints`, `on_closed` — take a `SlotImp::ptr` and mutate it directly. `SlotImp::ptr` is simply `std::shared_ptr<SlotImp>`, which allows `Logic` to pass slots around internally while the `Manager` API exposes the weaker `std::shared_ptr<Slot>` to callers outside the detail namespace.

## Construction and Direction

Two constructors model the two fundamentally different connection origins:

- The **inbound** constructor takes both local and remote endpoints, sets `m_inbound = true`, and initialises state to `accept`. Both `checked` and `canAccept` start `false` because reachability of the peer's advertised address hasn't been verified yet.
- The **outbound** constructor takes only the remote endpoint, sets `m_inbound = false`, and initialises state to `connect`. Critically, `checked` and `canAccept` are set `true` immediately — a successful outbound TCP connection is itself proof of reachability, so no separate connectivity probe is needed.

`m_fixed` and `m_inbound` are `bool const`, cementing the connection's origin permanently. The `fixed` flag marks connections to explicitly configured peers (cluster members, validator peers) that should be maintained unconditionally.

## State Machine

The `Slot::State` enum defines five states: `accept → active` (inbound path) and `connect → connected → active` (outbound path), with `closing` reachable from most intermediate states. The `state(State)` setter enforces the invariants via `XRPL_ASSERT`:

- `active` can only be reached through `activate()`, never through the generic setter.
- You cannot transition back to initial states (`accept`, `connect`).
- `connected` is only valid for an outbound slot currently in `connect`.
- `closing` cannot be set on a slot still in `connect` — you cannot gracefully close what hasn't connected yet.

`activate()` handles the single promotion to `active`, and it also stamps `whenAcceptEndpoints = now`. This timestamp gates how soon the node will accept the next `mtENDPOINTS` message from this peer — a flood-control mechanism against endpoint spam.

## Listening Port: Atomic with Sentinel

`m_listening_port` is declared `std::atomic<std::int32_t>` with a sentinel value `unknownPort = -1`. The `listening_port()` getter returns `std::nullopt` if the value is still `-1`, otherwise casts to `std::uint16_t` for the result. Using an `int32_t` rather than `uint16_t` avoids the unsigned representation ambiguity for "not yet known" while remaining lock-free. The port is set after handshake when the remote peer advertises it, potentially from a different thread from whoever reads it.

## `recent_t` — Per-Peer Address Deduplication

The nested `recent_t` class solves a specific gossip protocol problem: when the node relays peer endpoint lists to its connections, it should avoid echoing addresses back to the peer that originally reported them, and avoid repeating addresses already relayed recently. `recent_t` wraps a `beast::aged_unordered_map<beast::IP::Endpoint, std::uint32_t>` where the mapped value is the lowest hop count seen for that endpoint.

`insert()` updates the cache when an endpoint arrives or is sent. If the endpoint is already present, it only updates the hop count (and refreshes the age) when the new hop count is **less than or equal** to the existing one — preserving the closest known distance.

`filter()` returns `true` (meaning "suppress this address") when the peer has already seen the endpoint at an equal or smaller hop count. The `<=` condition is intentional and explicitly noted in both places: sending an endpoint at a *higher* hop count than the peer already knows is useful, but sending it at the same or shorter distance is not.

`expire()` is called periodically via `SlotImp::expire()` and trims entries older than `Tuning::liveCacheSecondsToLive` (30 seconds), matching the live-cache TTL so the per-slot filter stays synchronised with the broader endpoint cache lifecycle.

## Deprecated Public Members

`checked`, `canAccept`, `connectivityCheckInProgress`, and `whenAcceptEndpoints` are marked DEPRECATED but remain as raw public data members. They predate a planned refactor of the connectivity-check mechanism and are still read and written directly by `Logic`. Their persistence is a pragmatic concession — removing them requires restructuring the checker workflow, which is tracked separately from the core slot abstraction.