# `HashRouter.cpp` — Broadcast Suppression and Relay Coordination

`HashRouter` is the broadcast-deduplication layer for the XRPL peer-to-peer overlay. Every time a peer sends the local node a transaction, validation, or ledger object, the node must decide three things: have I already seen this? Should I bother processing it again? And if I'm relaying it outward, which peers already have it? `HashRouter` answers all three questions by maintaining a time-bounded map from 256-bit hashes to routing state for each observed object.

## The Suppression Map and Lazy Expiration

The backing store is a `beast::aged_unordered_map<uint256, Entry>` called `suppressionMap_`. Every entry carries its own insertion timestamp, and the map supports a `touch()` operation that refreshes that timestamp on access — making it behave as an LRU-style expiry cache. The hash used is `hardened_hash<strong_hash>`, preventing algorithmic complexity attacks against the hash table from untrusted peer data.

Expiration is *lazy*: old entries are only evicted when `emplace()` needs to insert a new key and calls `beast::expire(suppressionMap_, setup_.holdTime)` first. The `expire()` utility (in `aged_container_utility.h`) walks the container's chronological iterator in insertion-time order, erasing entries older than the configured `holdTime` (default 300 s). This means a burst of new hashes can flush stale entries in bulk, but a quiet node retains its map indefinitely — an acceptable trade-off given that entries are cheap and holdTime is already bounded.

The `touch()` call on cache hit extends an existing entry's lifetime by resetting its timestamp. The unit tests confirm this explicitly: accessing a key before its expiry deadline prevents it from being evicted even when the clock has otherwise passed its original insertion time. This means frequently-seen hashes never silently disappear while they're actively being observed.

## The `emplace()` Pivot

All seven public methods funnel through the private `emplace(key)`. It is not separately synchronized — all callers already hold `mutex_` before invoking it. The method returns a `std::pair<Entry&, bool>` where the boolean signals whether the entry was *created* (true) or already existed (false). This creation flag is the primary suppression signal: the first arrival of a hash gets `true`; every subsequent arrival, regardless of which peer sent it, gets `false`.

This asymmetry is intentional. The goal is not to track uniqueness per-peer but to deduplicate floods. When `addSuppressionPeer()` returns `false`, the caller at `PeerImp` knows the hash is a duplicate and can skip processing it entirely. The *peer identity* is stored inside the entry so the relay step can exclude peers that already have the object — not to gate whether processing occurs.

## Entry State: Flags, Peers, and Two Timestamps

The `Entry` inner class carries four distinct pieces of state:

- **`flags_`** — a `HashRouterFlags` bitmask accumulating status bits. The public bits (`BAD`, `SAVED`, `HELD`, `TRUSTED`) signal why a transaction is in a particular processing state. The six `PRIVATE` bits are owned by `apply.cpp` for internal transaction-application bookkeeping. `setFlags()` in `HashRouter` is idempotent: it checks whether the bits are already set and returns `false` without modification if so, avoiding spurious re-processing signals to callers.

- **`peers_`** — a `std::set<PeerShortID>` of every peer short-ID that has delivered this hash. Peer 0 is silently ignored (`addPeer` skips zero IDs) since zero is the local node's sentinel. When `shouldRelay()` fires, it calls `releasePeerSet()` which `std::move`s the set out and resets it, so subsequent peer arrivals accumulate a fresh exclusion list for the next relay window.

- **`relayed_`** — an `optional<Stopwatch::time_point>` recording when this hash was last relayed outward. `shouldRelay()` enforces a `relayTime` cooldown (default 30 s); if the timestamp is set and hasn't aged out, it returns an empty optional signaling "do not relay." When the cooldown expires, it stamps the new time, returns the accumulated peer set, and resets that set — so relaying is self-throttling and always provides a fresh exclusion list.

- **`processed_`** — an `optional<Stopwatch::time_point>` used by `shouldProcess()` to enforce a caller-supplied cooldown (`tx_interval`) independent of the relay timer. This prevents the same transaction ID from being re-submitted to the job queue more frequently than the ledger close rate allows.

## The Relay Protocol

The `shouldRelay()` flow is subtly different from suppression. The return type is `optional<set<PeerShortID>>`. A *seated* optional means "yes, relay — and skip these peers." An *empty* optional means "do not relay at all." An empty-but-seated set means "relay to everyone." Callers in `OverlayImpl` use this peer set as the exclusion list for broadcast, so peers that already delivered the item don't receive it back.

`addSuppressionPeerWithStatus()` exposes both dimensions at once: whether the entry is new *and* whether it has already been relayed (the optional relay timestamp). This is used in `PeerImp` for squelch-aware relay: if a message has already been relayed, the node can skip re-relaying even if the entry was just created under race conditions.

## Thread Safety

The mutex is declared `mutable` so `getFlags()` — logically const — can still lock. Every public method acquires a `std::lock_guard<std::mutex>` before touching `suppressionMap_` or `Entry` state. Because `emplace()` is private and only called under the lock, there is no recursive locking concern. The design is simple and correct for a high-contention hot path: a single coarse lock over the entire table, justified by the expected low lock-hold time of hash-table lookups and small entry updates.