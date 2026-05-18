# `PeerfinderManager.h` — PeerFinder Public Interface

This header is the sole public-facing contract for the PeerFinder subsystem. Everything else in `src/xrpld/peerfinder/` is internal implementation detail. It defines the three foundational types that the rest of the XRPL daemon interacts with: the `Config` settings struct, the `Endpoint` gossip record, and the `Manager` abstract interface that orchestrates all peer discovery and slot management.

## Why PeerFinder Exists

When an XRPL node starts up it must bootstrap into the peer-to-peer overlay: discover live peer addresses, establish outbound connections, accept inbound connections up to a quota, redirect full-capacity callers to other peers, and continuously maintain healthy connectivity. PeerFinder is the self-contained module that handles all of this. It is deliberately isolated from socket I/O — the `Manager` interface uses plain `beast::IP::Endpoint` and `boost::asio::ip::tcp::endpoint` values, and the caller drives the actual network calls based on what the manager says to do.

## `Config` — Operational Parameters

`Config` is a plain struct with an important set of derived computations baked in through three methods.

`calcOutPeers()` implements the fractional outbound peer policy: `outPeers = max(maxPeers * 15% + 0.5, 10)`. The 15% comes from `Tuning::outPercent` and the hard floor of 10 comes from `Tuning::minOutCount`. Keeping outbound connections to roughly 15% of total capacity means the majority of slots are reserved for inbound connections, which is essential for the overlay to remain open to new participants.

`applyTuning()` enforces a subtle per-IP admission limit. When `ipLimit` is zero (unset), it computes a value of 2 plus up to 3 extra slots scaled by how much `inPeers` exceeds the default 21-peer maximum. The critical constraint that follows is `ipLimit = max(1, min(ipLimit, inPeers / 2))` — no single IP address may consume more than half the inbound slots.

`makeConfig()` is the factory that bridges the server-level `xrpl::Config` to `PeerFinder::Config`. Two notable policy decisions are embedded here. First, validators automatically get `peerPrivate = true` regardless of the operator's explicit `PEER_PRIVATE` setting, because a validator's IP address should stay hidden to reduce its attack surface. Second, `autoConnect` is suppressed for standalone mode (used in testing) and for private peers where autonomous connections would defeat the privacy goal. The method supports two configuration modes: a legacy `PEERS_MAX`-based mode where in/out split is derived by percentage, and a newer explicit `PEERS_IN_MAX`/`PEERS_OUT_MAX` mode where both halves are set directly.

## `Endpoint` — Gossip Address Record

`Endpoint` pairs a `beast::IP::Endpoint` address with a `hops` count. The hop count is the distance from the originating peer in the gossip chain: a peer advertising itself sends endpoints with `hops = 0`; each relay node increments the count. Endpoints with `hops > Tuning::maxHops` (6) are dropped. The `operator<` overloads ordering by address only, which allows the set to be deduplicated without caring about which relay path delivered an entry.

This hop metadata is non-trivial architecturally. The Livecache deliberately cycles through all available hop depths when selecting addresses to hand out, ensuring each connected peer eventually sees addresses from the farthest corners of the overlay. This breadth-first horizon expansion is how PeerFinder drives the overlay toward lower diameter and higher connectivity without any central coordination.

## `Result` — Slot Admission Outcomes

The `Result` enum enumerates the five outcomes that can happen when the manager evaluates a new connection: `success`, `full` (slot quota exhausted), `inboundDisabled` (node not accepting inbound at all), `duplicatePeer` (already connected to this remote), and `ipLimitExceeded` (per-IP cap reached). The accompanying `to_string()` is a `string_view`-returning `noexcept` function — deliberately avoiding heap allocation since this is called in logging hot paths during busy connection activity.

## `Manager` — The Core Interface

`Manager` extends `beast::PropertyStream::Source` so its internal state can be streamed into the node's diagnostic property tree without coupling to any specific monitoring backend. The implementation is `ManagerImp` in `detail/PeerfinderManager.cpp`, instantiated by `make_Manager()` in `make_Manager.h`.

The interface methods fall into three groups.

**Bootstrapping setup** (`addFixedPeer`, `addFallbackStrings`) is called during startup to register known-good addresses. Fixed peers bypass connection limits entirely and are prioritized by the connection strategy — the node tries to establish all fixed connections before using the Livecache or Bootcache.

**Slot lifecycle events** mirror the TCP connection state machine. `new_inbound_slot` and `new_outbound_slot` allocate a `Slot` when a socket is first seen, returning both the slot and an initial `Result`. The caller then drives the slot through `onConnected` (once TCP connect succeeds, providing the resolved local endpoint), `activate` (after the cryptographic handshake, when the peer's `PublicKey` is known), and finally `on_closed` or `on_failure`. The split between `onConnected` and `activate` is deliberate: a TCP connection succeeding does not yet prove the remote is a legitimate XRPL peer.

**Periodic and gossip operations** (`on_endpoints`, `buildEndpointsForPeers`, `redirect`, `autoconnect`, `once_per_second`) form the steady-state maintenance loop. `once_per_second` is the master tick; the caller is expected to invoke it on a one-second timer. `buildEndpointsForPeers` returns the batch of endpoint lists that should be broadcast to each connected peer as `mtENDPOINTS` messages. `redirect` provides addresses to hand to a peer being rejected because the node is full. `onRedirects` processes addresses received when this node was redirected by a busy remote.

The abstract clock alias `clock_type = beast::abstract_clock<std::chrono::steady_clock>` is injected rather than taken from `std::chrono::steady_clock` directly. This makes the entire time-dependent logic (Livecache TTLs, boot cache cooldown, connection retry backoff) testable with a controlled fake clock in unit tests without modifying any production code paths.