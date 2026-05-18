# `Logic.h` — PeerFinder Connection Strategy Engine

## Role and Purpose

`Logic<Checker>` is the central decision-making class of the XRPL PeerFinder subsystem. Every policy question the network layer cannot answer on its own flows through here: which addresses to attempt connections to, which incoming connections to accept or reject, what gossip endpoint information to share with each peer, and how to record success or failure. The class is deliberately isolated from the actual I/O machinery by being templated on a `Checker` type, which allows unit tests to inject a mock connectivity tester while production code supplies the real async TCP prober.

## Data Model

Six data structures together capture the complete peer topology state at any instant:

`slots_` is the master table — a `std::map<beast::IP::Endpoint, shared_ptr<SlotImp>>` keyed by remote endpoint. Every live connection, inbound or outbound, regardless of state, has an entry here. The map is the single source of truth for "are we connected to this address?"

`connectedAddresses_` is a parallel `std::multiset<beast::IP::Address>` (port-stripped) used exclusively to enforce `Config::ipLimit` — the maximum number of connections from a single IP address. Tracking addresses separately from the full endpoint is intentional: two connections from the same host on different ports count against the same limit.

`keys_` is a `std::set<PublicKey>` that prevents duplicate connections to the same cryptographic identity, even if two connection attempts arrive from different remote ports.

`fixed_` maps configured always-on peer endpoints to `Fixed` metadata objects that carry Fibonacci backoff state. When a fixed peer's outbound connection fails, `Fixed::failure()` advances the backoff index through a pre-computed array `{1, 1, 2, 3, 5, 8, 13, 21, 34, 55}` minutes, ensuring reconnect attempts back off gracefully without requiring external timer infrastructure.

`livecache_` is a short-lived (30-second TTL) gossip cache populated from `mtENDPOINTS` messages received from active peers. It is organised internally by hop count, enabling the handout algorithm to prefer topologically nearby peers.

`bootcache_` is a persistent address store backed by the injected `Store` interface (an SQLite database in production). When the livecache is empty and no outbound attempts are in flight, `autoconnect()` falls back to bootcache entries. The bootcache is loaded at startup and updated incrementally.

## Slot Lifecycle

Slots transition through a state machine with distinct paths for inbound and outbound connections:

For **outbound** connections, `new_outbound_slot()` creates a `SlotImp` in `Slot::connect` state (counted as an in-flight attempt by `Counts`). When the TCP handshake completes, `onConnected()` advances the slot to `Slot::connected` and performs self-connect detection — it checks whether the newly discovered local endpoint already exists as a *remote* endpoint in `slots_`, which would indicate a loopback connection to ourselves. The `activate()` method then handles the XRPL handshake: it checks for duplicate public keys, determines whether the peer's slot type (fixed, reserved, or ordinary) gives it capacity bypass rights, and if capacity allows, moves the slot to `Slot::active`.

For **inbound** connections, `new_inbound_slot()` applies per-IP limits and duplicate checks before allocating a slot in `Slot::accept` state. The slot then waits for `activate()` to be called after the XRPL handshake completes. If `counts_.can_activate()` returns false because all inbound slots are filled, the slot receives `Result::full` and the caller is expected to redirect or close.

`on_closed()` performs cleanup: removes from `slots_`, `keys_`, and `connectedAddresses_`, updates `Counts`, and records fixed-slot failures when a fixed peer closes before reaching `active` state.

## Outbound Connection Strategy (`autoconnect`)

`autoconnect()` implements a strict four-tier priority order, returning early at the first tier that produces results:

1. **Fixed peers**: If fewer fixed connections are active than configured, `get_fixed()` scans `fixed_` for entries whose backoff `when()` has elapsed and that are not already in the squelch set or the current slot table. Fixed peers are returned in preference to everything else.

2. **Livecache**: The livecache hops list is shuffled and passed to the `handout()` algorithm via a `ConnectHandouts` receiver. The reverse-order iteration (from highest hops to lowest) means addresses furthest from the broadcasting peer are preferred — they carry more topological diversity.

3. **Bootcache refill**: Commented placeholder for DNS-based address resolution when both the livecache is empty and no attempts are in flight.

4. **Bootcache fallback**: Iterates the bootcache directly until the `ConnectHandouts` receiver is full.

Between tiers, if attempts are in flight but a tier produced no new candidates, `autoconnect()` returns an empty list and waits — avoiding the thundering-herd problem of launching redundant connection attempts.

The `m_squelches` aged set persists across calls to `autoconnect()` with a 60-second TTL, preventing rapid reconnection attempts to the same address.

## Endpoint Gossip: Receiving (`on_endpoints` + `preprocess`)

When an active peer sends an `mtENDPOINTS` message, `on_endpoints()` first enforces a per-slot rate limit (`whenAcceptEndpoints`) of one accepted message per `Tuning::secondsPerMessage` (151 seconds, chosen as a prime deliberately). Oversized messages are randomly sampled down to `Tuning::numberOfEndpointsMax`.

`preprocess()` then cleans the list:
- Entries exceeding `Tuning::maxHops` (6) are silently discarded.
- Exactly one `hops == 0` entry is allowed; it announces the sender's own listening port. Its IP is replaced with the sender's actual socket address since the sender doesn't know its own public IP.
- Non-public and unspecified addresses are dropped.
- Duplicates within the list are dropped.
- All surviving hop counts are incremented by one before storage, so that when we retransmit these entries, the hop count reflects our own distance to the origin.

For first-hop entries (now stored at `hops == 1`), if the slot has not yet been connectivity-tested, `on_endpoints()` triggers `m_checker.async_connect()` to verify the peer's claimed listening port is actually reachable. The slot is marked with `connectivityCheckInProgress` to prevent duplicate checks. Only peers that pass this test are admitted to the livecache.

## Endpoint Gossip: Broadcasting (`buildEndpointsForPeers`)

`buildEndpointsForPeers()` is called periodically and assembles the endpoint lists to broadcast to each active peer. It shuffles the active slot list (to vary broadcast order across cycles) and creates a `SlotHandouts` receiver for each active peer. The `handout()` template function then distributes livecache entries fairly across all receivers in a round-robin pass by hop level.

Self-advertisement uses a notable trick: rather than including the node's own public IP (which may not be known), an endpoint with `hops == 0` and the all-zeros IPv6 address is injected. Recipients that receive a zero-address entry at hops 0 are specified to use the TCP socket's remote address instead. This cleanly sidesteps the "what is my own public IP" problem without requiring STUN or similar external discovery.

Each peer's `recent` cache (an aged map in `SlotImp`) prevents the same endpoint from being sent to the same peer until the cache entry expires, limiting redundant gossip.

## Concurrency

All methods acquire `std::recursive_mutex lock_`. The recursive variant is required because `on_closed()` calls `remove()`, which is also independently callable and must be lockable. `checkComplete()` explicitly checks for `boost::asio::error::operation_aborted` before acquiring the lock, allowing the async Checker callback to safely no-op when the operation was cancelled during shutdown.

The `stopping_` flag and `fetchSource_` handle the shutdown race: `stop()` sets `stopping_` and cancels any in-progress source fetch while holding the lock; `fetch()` checks `stopping_` both before starting and after completing the synchronous source fetch to avoid processing results during teardown.

## Observability

`onWrite()` serialises the complete internal state — bootcache size, fixed count, per-slot details, aggregated `Counts`, `Config`, livecache stats, and bootcache stats — into a `beast::PropertyStream::Map`. This feeds the administrative `peers` RPC endpoint and server-side diagnostics without requiring a separate query path.