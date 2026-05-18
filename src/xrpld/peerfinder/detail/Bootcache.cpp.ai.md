# `Bootcache.cpp` — Persistent Bootstrap Address Cache

## Role in the System

When an XRPL node starts cold — with no active peers and no idea who to connect to — it needs an address book. `Bootcache` is that book. It sits inside the `PeerFinder` subsystem and maintains a ranked list of IP endpoints that the node has previously encountered or been statically configured with. The cache is loaded from a SQLite-backed `Store` at startup, kept in memory during the session, and flushed back to disk when entries change.

Unlike the `Livecache` (which holds short-lived gossip addresses), the `Bootcache` is meant to survive restarts and encode long-term connection history so the node can immediately prioritize endpoints it has successfully reached in the past.

## The Bimap Data Structure

The central design choice in this file is the use of a Boost `bimap` with two differently-typed views:

- **Left side** (`unordered_set_of`): maps each `beast::IP::Endpoint` to a hash bucket for O(1) lookup by address. This is needed when recording connection outcomes — `on_success` and `on_failure` must find an endpoint instantly.
- **Right side** (`multiset_of`): sorts the `Entry` objects (wrapping `int valence`) for ordered traversal. `Entry::operator<` is deliberately inverted: it returns `true` when `lhs.valence() > rhs.valence()`, so the right-side multiset stores entries with higher valence at `begin()` and lower valence at `end()`.

This dual-keyed structure lets `Logic.h` iterate `bootcache_.begin()` to `bootcache_.end()` and get endpoints ranked from most reliable to least, while still being able to look up any specific endpoint in O(1) when a connection attempt finishes. The `const_iterator` exposed publicly is a `boost::transform_iterator` that strips the `Entry` and yields only the `beast::IP::Endpoint`, since callers only need addresses, not scores.

## Valence: A Streak Counter, Not a Lifetime Score

Valence is a signed integer where positive means consecutive successes and negative means consecutive failures. The `on_success` and `on_failure` implementations enforce a clamping behavior that makes valence behave as a streak counter rather than a cumulative tally.

In `on_success`, before incrementing, the valence is clamped to a minimum of zero:
```
entry.valence() = std::max(entry.valence(), 0);
++entry.valence();
```
This means a peer that has failed five times in a row (valence = -5) but then succeeds once resets its failure streak before recording the success — it goes to valence=1, not valence=-4. The same asymmetric logic applies in `on_failure` with a `std::min(entry.valence(), 0)`. This keeps valence as an honest signal of recent behavior rather than a permanent reputation.

Since `boost::bimap` values are immutable once inserted (mutating a key would break the sorted invariant on the right side), updating valence requires a careful erase-then-reinsert sequence, which both `on_success` and `on_failure` perform.

## Two Insertion Paths

`insert()` adds a dynamically learned endpoint (valence = 0) and is idempotent — if the address is already present it returns `false` without any modification. This is the path taken when `Logic` receives gossiped addresses from peers.

`insertStatic()` handles statically configured bootstrap nodes (from the configuration file). These receive `staticValence = 32`, which places them near the top of the sorted order so the node preferentially connects to its own trusted seeds first. If the same endpoint already exists with a lower valence, `insertStatic` explicitly erases and reinserts it to enforce the minimum: a manually configured address is never deprioritized by historical failures.

## Pruning on Overflow

The cache has a soft limit of 1000 entries (`Tuning::bootcacheSize`). Whenever an insert would push the count over this threshold, `prune()` is called immediately after the insert. It removes 10% of entries (`Tuning::bootcachePrunePercent`) from the end of the right-side map — the lowest-valence entries — using a manual forward-iterator loop that walks backward from `m_map.right.end()`. The comment in the code is honest: Boost bimap doesn't handle erasing via reverse iterators cleanly, so the loop manually decrements a forward iterator before calling `erase`.

## Deferred Write-Back with Cooldown

Every mutation that should survive a restart — inserts, static inserts, and connection outcome records — calls `flagForUpdate()`. Rather than writing to the database immediately, this sets `m_needsUpdate = true` and then calls `checkUpdate()`, which only flushes to the `Store` if the cooldown timer (`Tuning::bootcacheCooldownTime = 60s`) has elapsed.

This batching design is important because connection events can arrive in rapid bursts (multiple peers connecting or failing in quick succession), and each would otherwise trigger a full serialization of the entire cache to disk. The 60-second cooldown ensures at most one database write per minute under heavy load. The timer resets after each successful write, so write frequency is self-limiting.

The destructor unconditionally calls `update()`, bypassing the cooldown check. This RAII flush ensures the cache isn't silently lost when the process shuts down cleanly between two cooldown windows — the final state of the cache is always persisted.

## Interaction with `Logic`

`Logic.h` owns a `Bootcache` instance directly (not by pointer) and drives the lifecycle: `load()` at startup, `periodicActivity()` (which delegates to `checkUpdate()`) on each maintenance tick, `insertStatic()` for configured seeds, `insert()` for gossiped addresses, `on_success()`/`on_failure()` for connection outcomes, and iteration to fill outbound connection candidates. The `onWrite()` method feeds the current cache state into the `beast::PropertyStream` diagnostic framework, making valence scores visible in monitoring output.