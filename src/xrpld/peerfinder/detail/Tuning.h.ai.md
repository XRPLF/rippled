# `Tuning.h` — PeerFinder Heuristic Constants

`Tuning.h` is the single source of truth for every magic number in the `PeerFinder` subsystem. Rather than scattering literal integers and durations across a dozen implementation files, all heuristically chosen values live here under the `xrpl::PeerFinder::Tuning` namespace, making them easy to find, review, and adjust together.

## Automatic Connection Policy

The first `enum` block governs how the node initiates and maintains outbound connections to the P2P overlay.

`secondsPerConnect` (10 s) is the cadence at which the connection logic wakes up and dispatches a new batch of outbound attempts. `maxConnectAttempts` (20) caps how many of those attempts can be in-flight simultaneously; `Counts::attempts_needed()` consults this value directly to avoid opening a connection storm against the rest of the network.

The outbound slot policy is a two-part rule: take the larger of `outPercent * maxPeers` (rounded) or the hard floor `minOutCount` (10). The 15% figure in `outPercent` is the key architectural choice — it keeps inbound capacity dominant so the node remains openly reachable, while still guaranteeing that even a tiny deployment (say, 10 total peers) maintains at least 10 self-initiated connections for robust topology. `PeerfinderConfig.cpp` expresses this as:

```cpp
std::max((maxPeers * Tuning::outPercent + 50) / 100, std::size_t(Tuning::minOutCount))
```

`defaultMaxPeers` (21) is the in-class initializer used by `Config`. An odd number is no accident: the 15% rule on 21 rounds to 3 outbound peers, which is a reasonable default for a light node.

`maxRedirects` (30) limits how many addresses a single redirecting peer may provide when the node's slots are full. This is a security bound — without it a malicious peer could flood the address caches by sending an unbounded redirect list.

## Fixed Connection Backoff

The `connectionBackoff` array `{1, 1, 2, 3, 5, 8, 13, 21, 34, 55}` is the Fibonacci sequence in minutes. It governs how long `Fixed` slots (operator-configured peers) must wait between retries after a connection failure. `Fixed::failure()` increments an index clamped to the array's last position, so the backoff saturates at 55 minutes rather than growing unboundedly. A successful connection resets the index to zero and the `when` time to now, so a peer that reconnects cleanly gets immediate re-consideration. The Fibonacci progression is a deliberate balance: it grows fast enough to avoid hammering an unreachable host but is bounded to prevent permanent lockout.

## Bootcache

The bootcache is persistent storage (SQLite-backed) of peer addresses that survived across restarts. Two constants control its size:

- `bootcacheSize` (1 000) is the threshold above which `Bootcache::trim()` fires.
- `bootcachePrunePercent` (10) means 10% of entries are removed per trim, bringing 1 000 entries down to 900 rather than slashing the cache aggressively.

`bootcacheCooldownTime` (60 s) is a write-coalescing guard. Frequent connection events (valence changes, insertions) call `flagForUpdate()`, but actual SQLite writes only happen if 60 seconds have elapsed since the last flush. The comment in the file notes the intent: this window should be larger than the typical lifetime of a peer that connects, dumps addresses, and disconnects — ensuring the useful addresses are captured in a single write rather than triggering multiple redundant writes per ephemeral peer.

## Livecache

The livecache is the in-memory, gossip-driven address cache populated by `mtENDPOINTS` messages.

`maxHops` (6) is the horizon depth of the gossip graph. Any endpoint arriving with `hops > 6` is silently dropped. The `Endpoint` constructor enforces this on ingress by clamping to `maxHops + 1` (a sentinel meaning "beyond horizon"), while `Logic::on_endpoints()` and the `Handouts` filters reject anything exceeding the limit. Six hops provides a network diameter large enough to reach far corners of a large P2P graph while preventing the cache from being polluted by stale, long-chain entries.

`numberOfEndpoints` (12, computed as `2 * maxHops`) is how many endpoints each `mtENDPOINTS` message should carry. `numberOfEndpointsMax` (clamped to `max(24, 64)` = 64) is the upper bound accepted from a peer; messages exceeding this are randomly trimmed by `Logic::on_endpoints()` before insertion.

`redirectEndpointCount` (10) controls how many addresses are handed to a newly connecting peer that gets redirected (i.e., when slots are full). Keeping this to 10 avoids bandwidth waste while giving the client enough alternatives.

`secondsPerMessage` (151 s) is the per-peer rate limit on how often `mtENDPOINTS` messages are sent or accepted. The comment acknowledges it is prime intentionally — a prime interval de-synchronizes the broadcast timers across nodes, preventing coordinated message floods where many peers all gossip simultaneously.

`liveCacheSecondsToLive` (30 s) is the TTL for a livecache entry. `Livecache::expire()` and `SlotImp::expire()` both use this value; note it is much shorter than `secondsPerMessage` (151 s). This asymmetry is deliberate: entries age out quickly so that a peer which drops offline does not linger in the cache, yet broadcasts are infrequent to keep control-plane traffic low. The short TTL means a node must receive a fresh `mtENDPOINTS` to keep a remote address visible.

`recentAttemptDuration` (60 s) suppresses retries to addresses that were recently attempted. `Logic::once_per_second()` expires the squelch map using this duration, ensuring an address is not hammered repeatedly within the same connection cycle.