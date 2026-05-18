# `Bootcache.h` — PeerFinder Bootstrap Address Cache

## Role in the System

`Bootcache` is the persistent address cache that XRPL's `PeerFinder` subsystem consults whenever the node needs to establish new outbound connections but lacks live peer data. It is the "cold start" solution: when a node is freshly launched or has lost all its peers, it pulls ranked IP endpoints from this cache to begin reconnecting. The cache sits inside `Logic`, alongside the `Livecache` (which holds freshly gossiped, time-expiring addresses), and is loaded from and flushed to a persistent `Store` (backed by an SQLite database in production).

## The Valence Reputation Score

Each cached endpoint carries an integer *valence* that acts as a lightweight reputation score. The sign encodes the trend direction: a positive valence records the number of *consecutive* successful connection handshakes, and a negative valence records consecutive failures. The key behavioral detail is that the streak resets to zero before crossing sign boundaries. In `on_success()`, the code does:

```cpp
entry.valence() = std::max(entry.valence(), 0);
++entry.valence();
```

A previously failing peer (negative valence) is not rewarded immediately with a high positive score — it resets to 0 first, then increments to 1. The same clamping happens in `on_failure()` in reverse. This prevents a long history of successes from shielding a peer that suddenly starts refusing connections: the first failure drives it back toward 0 and then negative territory, quickly demoting it in connection priority.

Static addresses (from `[validators]` or hand-configured peers) receive a valence of `staticValence = 32` via `insertStatic()`. If an address is already present with a lower valence, the old entry is replaced. This ensures permanently trusted peers start at the front of the connection queue and survive pruning.

## The Bimap Data Structure

The central data member is a `boost::bimap`:

```cpp
using left_t  = boost::bimaps::unordered_set_of<beast::IP::Endpoint, ...>;
using right_t = boost::bimaps::multiset_of<Entry, xrpl::less<Entry>>;
using map_type = boost::bimap<left_t, right_t>;
```

The left side is an unordered set of `IP::Endpoint`, providing O(1) lookup by address. The right side is a multiset sorted by `Entry::operator<`, which sorts in *decreasing* valence order (higher valence sorts before lower valence). This dual-indexed structure makes two otherwise conflicting operations cheap: "does this endpoint already exist?" (left side, hash lookup) and "iterate endpoints from most reliable to least" (right side, sorted traversal). There is no standard container that satisfies both requirements simultaneously.

Because `boost::bimap` treats both sides as keys, neither can be mutated in place. Valence updates in `on_success()` and `on_failure()` therefore follow an erase-then-reinsert pattern, which preserves the sorted invariant on the right side.

## Iterator Projection via `Transform`

The public `const_iterator` type is a `boost::transform_iterator` wrapping the right-side (valence-sorted) iterator, using the `Transform` functor to project each bimap entry back to its `beast::IP::Endpoint`. Callers — specifically `Logic::connectGrab()`, which iterates `bootcache_.begin()` to `bootcache_.end()` — see a sequence of endpoints ranked from highest to lowest valence without needing to know anything about the bimap internals or the `Entry` wrapper type.

## Throttled Persistence

Writes to the backing `Store` are debounced with a 60-second cooldown (`Tuning::bootcacheCooldownTime`). The flag `m_needsUpdate` tracks whether any mutation has occurred since the last save. `flagForUpdate()` sets this flag and immediately calls `checkUpdate()`, which only proceeds with the write if `m_clock.now()` has passed `m_whenUpdate`. After a write, the cooldown timer is reset. This pattern avoids hammering the database on rapid connection bursts (e.g., when a node reconnects to many peers in quick succession after a disconnect).

`periodicActivity()` is called from the `Logic` event loop on a timer and simply delegates to `checkUpdate()`, giving the cache a regular opportunity to flush deferred writes. The destructor calls `update()` unconditionally, bypassing the cooldown check, so the final state is always persisted on clean shutdown regardless of whether the cooldown has elapsed.

## Pruning Policy

When `size()` exceeds `Tuning::bootcacheSize` (1000 entries), `prune()` removes the lowest-valence entries until the cache is back under the limit. It trims `bootcachePrunePercent` (10%) of the current size, iterating the right-side multiset in reverse — from the least-reputable endpoint backward. Pruning is triggered on every insert path (`insert`, `insertStatic`, `on_success`, `on_failure`) and after `load()`, so the cache never grows unboundedly even under a flood of incoming address redirects (`Tuning::maxRedirects` bounds per-connection redirects at the `Logic` layer, but the cache-level pruning acts as a final backstop).

## Relationship to `Store`

`Store` is a pure-virtual interface with two methods: `load(callback)`, which fires the callback for each persisted `(endpoint, valence)` pair, and `save(vector<Entry>)`. `Bootcache::load()` clears the in-memory map first and then rebuilds it entirely from the store, calling `prune()` afterward to trim any stale overgrowth. The concrete implementation in production is `StoreSqdb`, which persists to an sqdb/SQLite file on disk. The abstract interface means `Bootcache` can be unit-tested with a trivial in-memory store without touching the filesystem.