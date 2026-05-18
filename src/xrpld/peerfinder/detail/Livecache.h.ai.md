# `Livecache.h` — Short-Lived Peer Endpoint Cache

## Role in the System

`Livecache` stores the transient stream of peer endpoint advertisements that flow through the XRPL gossip protocol. When a peer sends an `mtENDPOINTS` message, the addresses it contains are inserted into this cache. The cache's defining characteristic is deliberate ephemerality: entries expire after just 30 seconds (`Tuning::liveCacheSecondsToLive`). The reasoning is direct — peers only advertise themselves when they have open connection slots, so a stale advertisement is worse than no advertisement at all.

This contrasts sharply with `Bootcache`, the sibling component that persists addresses to disk across restarts. `Livecache` makes no attempt to verify connectivity of its entries and is explicitly unsuitable for bootstrapping. It is the hot, real-time view of who has open slots right now, while `Bootcache` is the cold, verified record of who has historically been reachable.

## Data Structure Design

The cache uses two cooperating data structures. Primary storage is a `beast::aged_map<beast::IP::Endpoint, Element>` — a time-ordered associative container that supports efficient chronological scanning for expiration. Secondary indexing uses arrays of `boost::intrusive::list` partitioned by hop count.

The `Element` type is the key to making these two structures work together without extra allocation. It inherits from `boost::intrusive::list_base_hook<>`, embedding the list linkage directly in the struct. This means each `Element` can simultaneously be the value in the aged map *and* a node in one of the hop-count lists with no heap allocation for list membership. When `hops.remove(e)` or `hops.insert(e)` is called, it operates directly on the `Element` reference retrieved from the map.

## Hop-Count Partitioning

The `hops_t` nested class owns an array of `list_type` of size `1 + Tuning::maxHops + 1` (9 entries for `maxHops = 6`). Each slot holds all endpoints known to be that many hops away. Index 0 represents the local node itself. Indices 1 through `maxHops` are normal relayable endpoints. Index `maxHops + 1` is a special holding area: when the caller receives an endpoint message from a peer that was already at `maxHops`, it increments the hop count before calling `insert()`, landing the address in this overflow bucket. These addresses are used for outgoing connection attempts and redirect responses but are never propagated further — distributing them would push them past the hop limit.

A parallel `Histogram` array maintains counts per hop bucket, supporting the `histogram()` diagnostic string output.

## Insert Policy: Prefer the Shortest Path

The `insert()` method implements a keep-best-hops policy. Three cases arise when inserting an `Endpoint`:

1. **New address**: inserted into the aged map, added to the hop list at its reported hop count.
2. **Duplicate at a higher hop count**: silently dropped. If the same peer is known closer, the more distant advertisement carries no new information.
3. **Duplicate at a lower or equal hop count**: the entry's timestamp is refreshed via `m_cache.touch()`, and if the new hop count is actually lower, `hops.reinsert()` moves the element to the correct bucket. Refreshing ensures the TTL is extended for an address still being actively advertised.

The `XRPL_ASSERT` on entry enforces the invariant that no address exceeds `maxHops + 1`, which must be upheld by the caller before calling `insert()`.

## Security: Mandatory Shuffle Before Handout

The `hops_t::insert()` method always places new elements at the front of the list with `push_front`. The code comment is explicit: *"This has security implications without a shuffle."* A malicious peer could repeatedly advertise its own address to ensure it appears at the head of every hop bucket, biasing which addresses other nodes connect to or relay.

The defence is that `Logic` calls `hops.shuffle()` before every handout — for `redirect()`, `autoconnect()`, and `buildEndpointsForPeers()`. The shuffle copies each intrusive list into a temporary `std::vector` of references, randomizes with `default_prng()`, then rebuilds the list. After shuffling, the insertion-order bias is neutralized.

After an endpoint is handed out, `Hop::move_back()` moves it to the tail of its bucket list. This provides fairness under repeated handouts: endpoints that were just given out recede to the back, giving other entries a turn at the front.

## Iterator Architecture

Two levels of `boost::transform_iterator` compose the public interface. At the inner level, `Hop<IsConst>::Transform` converts `Element` references into `Endpoint` const references, so callers never see the intrusive hook machinery. At the outer level, `hops_t::Transform<IsConst>` converts `list_type` references into `Hop<IsConst>` view objects, so iterating over `hops` yields a sequence of hop-bucket views.

The `beast::maybe_const<IsConst, T>` utility enables the single `Hop<bool>` template to serve both const and mutable contexts from a shared implementation. The `move_back()` method on `Hop` requires a `const_cast` to mutate the list through what is always a const iterator — safe in practice because the underlying `Element` objects are always non-const; the constness is an artifact of the iterator type, not the storage.

## Expiration

`expire()` scans `m_cache.chronological` from oldest to newest, stopping as soon as it finds an entry newer than `Tuning::liveCacheSecondsToLive`. For each expired entry, `hops.remove(e)` unlinks the element from its bucket list before erasing it from the map. `Logic::once_per_second()` drives this call.

## Template Allocator Parameter

`Livecache<Allocator>` is parameterized on an allocator, defaulting to `std::allocator<char>`. In production the default is used. The allocator is threaded through to the `aged_map` constructor, enabling the unit test infrastructure to inject a custom allocator for deterministic memory tracking.