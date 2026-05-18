# `SlotImp.cpp` — Peer Connection Slot Implementation

`SlotImp` is the concrete implementation of the abstract `Slot` interface defined in `Slot.h`. Within the PeerFinder subsystem, a "slot" represents a single peer-to-peer overlay connection and carries all metadata the connection manager needs to reason about it: direction (inbound vs. outbound), current lifecycle state, remote and local endpoints, the peer's public key (once the handshake completes), and a cache of recently observed endpoints. `SlotImp.cpp` implements the two pieces of `Slot` that require non-trivial logic — the state machine and the recent-address tracker — while the header provides all the trivial accessors inline.

## Two Constructors, Two Directions

The class has dual constructors that reflect an architectural reality: inbound and outbound connections carry meaningfully different initial conditions.

The **inbound constructor** takes both `local_endpoint` and `remote_endpoint` and initializes `m_state` to `accept`. Because a freshly accepted socket has not yet been interrogated, `checked` and `canAccept` are both false — the PeerFinder does not yet know whether the remote address is publicly reachable.

The **outbound constructor** omits `local_endpoint` (it's unknown until the OS assigns a port) and initializes `m_state` to `connect`. Crucially, it sets `checked = true` and `canAccept = true`. The reasoning is straightforward: the act of successfully connecting to a remote address proves it is reachable, so no further connectivity check is needed.

`m_inbound` and `m_fixed` are `bool const` — set at construction and never changed. This is intentional: the direction of a connection is an immutable fact, and making it `const` lets the compiler and human readers alike trust that no code path reassigns it.

## State Machine and Its Invariants

`Slot::State` is an enum with five values: `accept`, `connect`, `connected`, `active`, and `closing`. The transitions are enforced by `state()` and `activate()`, both armed with `XRPL_ASSERT` guards.

The design decision to split state mutation into two methods — `state()` and `activate()` — is deliberate. `activate()` is the **sole path** to the `active` state and simultaneously records `whenAcceptEndpoints = now`. Combining these two side effects in a single method ensures the endpoint-spam throttle timestamp is always set when a slot goes live; calling `state(active)` directly would skip that assignment. The first `XRPL_ASSERT` inside `state()` enforces this: passing `active` there is a programming error.

Within `state()`, four additional assertions encode the peer state machine topology:

- No transition into initial states (`accept`, `connect`) — those are entry points only, set by the constructor.
- `connected` is only reachable from outbound slots (`!m_inbound`) currently in the `connect` state. Inbound slots jump straight from `accept` to `active` via `activate()` without passing through `connected`, because inbound acceptance does not go through the TCP connect phase.
- `closing` is forbidden while still in the `connect` state, because an in-progress outbound attempt that hasn't completed yet should be aborted, not gracefully closed.

These asserts are compile-time-silent but will abort at runtime in debug builds the moment a caller attempts an illegal transition — a hard fail rather than silent data corruption.

## `recent_t` — Per-Slot Endpoint Deduplication

The inner class `recent_t` implements a bounded, time-decaying cache of `(IP::Endpoint, hops)` pairs. Its purpose is to prevent redundant endpoint gossip: the PeerFinder should not send a peer an address it already knows about at an equal or closer hop distance.

The cache is a `beast::aged_unordered_map<beast::IP::Endpoint, std::uint32_t>`, which is a hash map that also maintains insertion/access order for LRU-style expiry. Both received endpoints (what the peer told us) and forwarded endpoints (what we sent to the peer) are recorded here.

`insert()` emplace-inserts a new entry. If the endpoint is already cached, it updates the stored hop count only when the new value is less than or equal to the existing value, and `touch()`es the entry to reset its age. The `<=` inequality is called out in a comment as significant to other logic.

`filter()` returns `true` (meaning "suppress this send") when the cached hop count for the endpoint is less than or equal to the hop count we're about to send. This matches `insert()`'s semantics: if we received an endpoint at hop 2, we filter any outbound announcement at hop 2 or higher — because the peer clearly already has better knowledge. But if we want to announce it at hop 1, we allow it through.

`expire()` calls `beast::expire(cache, Tuning::liveCacheSecondsToLive)`, which prunes all entries older than 30 seconds. This TTL aligns with the endpoint broadcast cadence: `mtENDPOINTS` messages are sent roughly every 151 seconds (`Tuning::secondsPerMessage`), but endpoint freshness decays faster so that a peer that drops off and reconnects will re-receive address updates correctly.

## `m_listening_port` and Thread Safety

The listening port is stored as `std::atomic<std::int32_t>` with a sentinel value of `-1` (`unknownPort`). The atomic wrapper exists because port discovery — triggered when the remote peer's handshake reveals which port it listens on — may race against reads from the connection management thread. The `listening_port()` accessor translates the atomic integer back to `std::optional<std::uint16_t>`, returning `nullopt` for the sentinel.

## Relationship to PeerFinder Logic

`SlotImp` is a pure data/state object; it contains no scheduling, I/O, or networking code. It is consumed by the `Logic` class, which holds `shared_ptr<SlotImp>` instances keyed by remote endpoint and drives all state transitions. The `recent_t` cache is populated during endpoint gossip processing and consulted during endpoint broadcasting, giving the Logic a compact per-connection memory of what each peer already knows — without requiring cross-slot coordination.