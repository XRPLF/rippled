# `HashRouter.h` — Peer-Message Deduplication and Relay Control

`HashRouter` solves a fundamental problem in any gossip-style P2P network: when many peers send the same message (identified by its hash), the node must decide whether to process it again, re-broadcast it, and to whom. This file defines the routing table that enforces those decisions in the XRPL overlay.

## Role in the System

The hash router sits at the intersection of the peer message handler (`PeerImp`) and the transaction-validity engine (`apply.cpp`). Every incoming transaction, proposal, or validation is fingerprinted by hash and registered here. The router answers three distinct questions:

1. **Have I seen this hash before, and from which peer?** (`addSuppression*` family)
2. **Should I re-broadcast this item right now, and to whom?** (`shouldRelay`)
3. **Should I process this item right now?** (`shouldProcess`)

## Data Structures

The backbone is `suppressionMap_`, a `beast::aged_unordered_map` keyed on `uint256` (the message hash). The map uses a `Stopwatch` clock, which lets the real clock be substituted in tests. Each value is a private `Entry` holding four things: a `HashRouterFlags` bitfield, a `std::set<PeerShortID>` of peers that sent this hash, and two `std::optional<Stopwatch::time_point>` timestamps — one for the last relay event, one for the last processing event.

Expiration is lazy. `emplace()` — the private bottleneck method called by every public API — first looks up the key. If found, it *touches* the entry (resetting its TTL in the aged map) and returns the existing entry. If not found, it calls `expire()` on the map to evict all entries older than `holdTime` (default 300 seconds), then inserts a fresh entry. Touching on every access means frequently-seen hashes live longer than rarely-seen ones, which naturally keeps hot messages in memory while cold ones age out.

## Flag Design: Public vs. Private

`HashRouterFlags` is a strongly-typed `uint16_t` bitfield split into two tiers. The public flags (`BAD`, `SAVED`, `HELD`, `TRUSTED`) carry semantic meaning used widely across the application. The private flags (`PRIVATE1`–`PRIVATE6`) are opaque at the header level but are given concrete meanings by individual subsystems. In `apply.cpp` they are aliased as `SF_SIGBAD`, `SF_SIGGOOD`, `SF_LOCALBAD`, and `SF_LOCALGOOD`, acting as a per-transaction signature verification cache: once the router records that a transaction's signature was checked, subsequent encounters skip the expensive cryptographic work. This pattern avoids coupling the router to transaction semantics while still providing a shared cache reachable from any code path that holds a `HashRouter` reference.

The full set of bitwise operators (`|`, `|=`, `&`, `&=`) and the `any()` predicate are defined as `constexpr` free functions rather than member operators — a deliberate choice that keeps the enum scoped and type-safe while still supporting natural flag composition.

## Relay Suppression

`shouldRelay(uint256 const& key)` returns `std::optional<std::set<PeerShortID>>`. The unseated optional signals "do not relay at all" — the item was broadcast recently (within `relayTime`, default 30 seconds). The seated optional contains the exclusion set: all peers that already sent us this hash. The broadcaster can skip those peers, avoiding redundant sends back to the originators. Crucially, each call to `shouldRelay` that grants permission also atomically moves the peer set out of the entry via `releasePeerSet()`, resetting it for the next relay window. New peers that send the same hash after the relay will accumulate in the fresh set, so the next relay window will again exclude the right set.

The `addSuppressionPeerWithStatus` variant adds a peer and simultaneously returns whether the entry was newly created and what the last relay timestamp was. `PeerImp` uses this for the reduce-relay mechanism: if a duplicate proposal or validation arrives within `reduce_relay::IDLED` seconds of the last relay, the slot/squelch system is updated — letting the network reduce redundant re-broadcasts from overly verbose peers.

## Processing Rate-Limiting

`shouldProcess` enforces a separate time gate on transaction application, distinct from relay. In `PeerImp`, the interval is hard-coded to 10 seconds: if the same transaction arrives again within that window, the router blocks re-processing and lets the caller check the flags to decide whether to penalize the peer (e.g., `BAD` flag means the peer is sending known-bad transactions, warranting a resource fee). The entry's `processed_` timestamp is set on first call and checked on subsequent ones.

## Concurrency Model

A single `mutable std::mutex` serializes all operations. This coarse-grained approach is intentional: `emplace()` itself is not thread-safe (it calls `expire()` and modifies the map), and entries are returned by reference, so callers holding the lock through the public API must not be interrupted. The tradeoff is that all peers sharing the same application object contend on one lock. Given that `HashRouter` operations are short — a hash lookup plus a few flag checks — this is acceptable. The virtual destructor on `HashRouter` allows future subclassing, though none exists in the current codebase.

## Configuration Constraints

`setup_HashRouter.cpp` enforces invariants on the configurable parameters: `holdTime ≥ 12s` (roughly three ledger closes, ensuring an entry outlives a single consensus round), `relayTime ≥ 8s` (two ledger closes), and `relayTime ≤ holdTime` (an entry must outlive the relay window). Violating any of these throws at startup. The defaults (300s hold, 30s relay) are described as requiring network-wide coordination to change, since they affect how many duplicate messages nodes will suppress across the entire network.

## Naming Note

The "suppression" terminology throughout the API (`addSuppression`, `suppressionMap_`) is an acknowledged historical artifact — a `VFALCO TODO` comment in the header flags it for renaming to something more semantically accurate. The router doesn't suppress messages in a filtering sense; it tracks them to decide routing and processing eligibility.