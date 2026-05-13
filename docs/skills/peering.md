# Overlay Peering

P2P network using persistent TCP/IP connections. Messages serialized via Protocol Buffers. `OverlayImpl` manages connections; `PeerImp` handles per-peer logic. `PeerFinder` (sub-module under `peerfinder/`) handles peer discovery, slot accounting, and address caches.

## Key Invariants

- Connection preference order: Fixed Peers → Livecache → Bootcache
- Cluster connections and reserved (`PeerReservationTable`) connections do NOT count toward slot limits in `Counts::can_activate` — they bypass `m_in_active`/`m_out_active` caps
- Validators are forced `peerPrivate=true` by `Config::makeConfig` even without explicit `[peer_private]`; this is "soft" privacy (still accepts inbound, but asks peers not to gossip address). `wantIncoming` is derived *before* the validator key check fires, so a validator with a key still advertises inbound willingness internally.
- Protobuf message changes MUST maintain wire compatibility or risk network partitioning
- Squelching: after `MAX_SELECTED_PEERS=5` peers each cross `MAX_MESSAGE_THRESHOLD=20` messages, a random 5-peer subset becomes "Selected"; rest are muted via `TMSquelch` for a randomized window in `[MIN_UNSQUELCH_EXPIRE=300s, MAX_UNSQUELCH_EXPIRE_PEERS=3600s]`
- Reduce-relay does not activate for `WAIT_ON_BOOTUP=10min` after process start (`Slots::reduceRelayReady`)
- Handshake binds TLS session to node identity via signature of `makeSharedValue` (SHA-512 XOR of TLS finished messages, then `sha512Half`); a zero shared value (degenerate XOR) is rejected
- Wire format: 6-byte header uncompressed, 10-byte compressed; 26-bit payload size field caps messages at `maximumMessageSize = 64 MiB`
- Hop count cap: `Endpoint` constructor clamps `hops` to `maxHops+1=7`; `Logic::preprocess` drops `hops > maxHops=6` and increments surviving hops by 1 before storage
- TX reduce-relay queue is bounded by `MAX_TX_QUEUE_SIZE=10000` hashes per peer; required to stay under the 64 MiB protocol limit at high TPS
- `peersWithMessage_` (in `Slots`) is `inline static` — shared across all instantiations, not per-instance
- `Bootcache` valence is a *streak* counter: clamped to 0 before crossing sign, so a failing peer resets to 0 before going positive. Static peers receive `staticValence=32`.
- `Livecache` uses `push_front` insertion — MUST `shuffle()` before handout to prevent topology manipulation by an adversary repeatedly advertising its own address
- `SourceStrings::fetch()` silently drops malformed addresses — no error returned for bad config entries
- `Checker` destructor calls `wait()` only; must call `stop()` first explicitly before destruction to cancel pending probes

## Common Bug Patterns

- PeerFinder slot exhaustion: if `inPeers`/`outPeers` is reached, new connections silently fail; check `Counts::can_activate` and `attempts_needed`
- `HashRouter::shouldRelay` prevents duplicate relay; bypassing it causes message storms (`OverlayImpl::relay` enforces this)
- `ConnectAttempt::processResponse` on HTTP 503 parses `peer-ips` JSON array for redirect; malformed entries are validated as endpoints before being passed to `peerFinder().onRedirects`
- `PeerImp::close` must run on the strand; calling from wrong thread causes race conditions on socket and timer state
- Destructor chain: `~PeerImp` → `deletePeer` → `onPeerDeactivate` → `on_closed` → `remove`; interrupting this chain leaks slots
- `~ConnectAttempt` releases the PeerFinder slot via `on_closed(slot_)` only if `slot_ != nullptr`; on successful promotion to `PeerImp`, `slot_` is moved out and must be left null
- `tryAsyncShutdown()` must defer SSL shutdown until `!readPending_ && !writePending_`; calling `async_shutdown` while async I/O is in flight is undefined behavior
- `dynamic_pointer_cast<SlotImp>` is required wherever `Manager` API takes `shared_ptr<Slot>` but `Logic` needs `SlotImp`
- A compressed message from a peer that did NOT negotiate compression is a hard `protocol_error` in `invokeProtocolMessage` (prevents CPU forcing attack)
- Self-squelch attempt (peer sends `TMSquelch` for our own validation key) is silently dropped in `PeerImp::onMessage(TMSquelch)` — never trust a peer to silence us
- `Cluster::for_each` callback must NOT call `Cluster::update` — same non-recursive mutex, deadlock
- `ZeroCopyOutputStream` destructor MUST flush trailing `commit_` — protobuf doesn't guarantee terminal `BackUp` or `Next` call; missing flush silently drops bytes
- `Bootcache` erase-then-reinsert pattern in `on_success`/`on_failure`: bimap values are logically const after insert, so valence updates require erase + reinsert
- `SlotImp::state(active)` is forbidden — must use `activate()` which also sets `whenAcceptEndpoints`; bypassing this leaves the flood-control timestamp unset
- `SourceStrings::fetch()` has an idempotent retry loop quirk: if `from_string()` fails, it retries the same string (no-op); effective behavior is just "drop invalid entries"

## Connection Lifecycle

### Outbound (`ConnectAttempt`)

1. `OverlayImpl::connect` → resource check → `peerFinder().new_outbound_slot()` → create `ConnectAttempt`
2. Five-phase chain: `async_connect` → TLS `async_handshake` → HTTP write → HTTP read → `processResponse`
3. Dual-timer scheme: global 25s ceiling (`connectTimeout`) + per-step timers (8/8/3/3/2s); both share `onTimer` callback distinguishing by expiry comparison. Global timer armed once (guarded by epoch-check), step timer reset at each phase.
4. `ioPending_` flag prevents starting SSL shutdown while another async op is pending on the stream
5. On HTTP 101: `verifyHandshake` → create `PeerImp` → move `slot_` and `stream_ptr_` into peer → `overlay_.add_active(peer)`
6. On HTTP 503 with JSON `peer-ips`: forward to `peerFinder().onRedirects`
7. `verify_none` on TLS — security comes from node-key signature over `makeSharedValue`, not cert chain

### Inbound (`OverlayImpl::onHandoff`)

1. HTTP server hands off TLS stream + upgrade request
2. Sequential gates: `processRequest` (for `/crawl`, `/health`, `/vl/`) → resource limit → `new_inbound_slot` → `negotiateProtocolVersion` → `makeSharedValue` → `verifyHandshake`
3. Create `PeerImp`, insert into `m_peers` (slot-keyed); `peer->run()` MUST be called while holding `mutex_` (race vs `stop()` draining list)
4. `m_peers` populated here, but `ids_` only after `activate()` post-protocol-handshake

## Two-Phase Peer Registration

- `m_peers`: `PeerFinder::Slot → weak_ptr<PeerImp>` — populated at handshake start, used for slot management
- `ids_`: `Peer::id_t → weak_ptr<PeerImp>` — populated at `activate()` after protocol handshake; used for broadcast and relay
- Outbound peers (via `ConnectAttempt`) populate both maps together in `add_active`

## Review Checklist

- Verify resource manager checks on both inbound and outbound connections
- New protocol messages: update protobuf definitions AND verify wire compatibility; add LZ4 eligibility list in `Message::compress()` if bulk
- Squelch changes: test with high peer counts; incorrect squelch logic can silence validators
- Header parsing changes (`ProtocolMessage.h`): the high-bit format guard (`*iter & 0x80`) and reserved-bit checks (`*iter & 0x0C == 0`) MUST remain
- Adding a new compression `Algorithm` enum value: must have high bit set, low nibble zero (so it's extractable via `*iter & 0xF0`); update `Compression.h` dispatch switches or the `UNREACHABLE` guard fires
- Strand discipline: any new method touching socket/queue state must guard with `if (!strand_.running_in_this_thread()) return post(strand_, ...)`
- `ZeroCopyOutputStream` use: always ensure the object goes out of scope (destructor flush) before the caller reads from the streambuf
- Bootcache changes: remember valence updates require erase-then-reinsert (bimap), and the cooldown (60s) batches writes

## Key Patterns

### Strand Execution
```cpp
// REQUIRED: socket operations must run on the strand
if (!strand_.running_in_this_thread())
    return post(strand_, std::bind(
        &PeerImp::close, shared_from_this()));
// Calling socket ops from wrong thread causes races on state
```

### Duplicate Relay Prevention
```cpp
// REQUIRED: check HashRouter before relaying
if (!hashRouter_.shouldRelay(hash))
    return;  // already relayed — suppress duplicate
overlay_.relay(message, hash);
```

### Shared Lazy Compression
```cpp
// Message::getBuffer(Compressed::On) — compresses once, shared across N peers
std::call_once(once_flag_, &Message::compress, this);
// Eligible types only (mtTRANSACTION, mtLEDGER_DATA, mtVALIDATOR_LIST, ...);
// Latency-sensitive types (mtPING, mtVALIDATION, mtPROPOSE_LEDGER) excluded.
// Falls back to uncompressed if savings < 4 bytes (compressed header overhead).
// Messages <= 70 bytes are never compressed.
```

### Resource Charging Batches
```cpp
// PeerImp::onMessageBegin resets fee_; onMessageEnd applies charge once per
// message via charge(). Handlers escalate via fee_.update() (monotonic).
```

### Exception-Based Handshake Failures
```cpp
// verifyHandshake() throws std::runtime_error on any check failure;
// callers (ConnectAttempt, PeerImp::doAccept) wrap in try/catch and tear down.
```

### Traffic Categorization Double-Call
```cpp
// categorize() called once at Message construction (outbound, inbound=false).
// addCount() called twice per message: once for category, once for 'total'.
// 'unknown' is NOT rolled into 'total'.
```

## Reduce-Relay (Squelch) Architecture

Two halves, decoupled:

- **Upstream (`Slot`/`Slots` in `OverlayImpl`)**: counts inbound validator messages per peer, selects 5 sources, calls `SquelchHandler::squelch()` (implemented by `OverlayImpl`) which sends `TMSquelch` over the wire. Uses `UptimeClock`.
- **Downstream (`Squelch` in `PeerImp`)**: receives `TMSquelch`, stores expiry in `hash_map<PublicKey, time_point>`. `PeerImp::send()` calls `expireSquelch(validator)` before transmitting any validator-keyed message; `false` return → drop, count under `TrafficCount::squelch_suppressed`.

All `OverlayImpl::updateSlotAndSquelch` calls are dispatched to `strand_` because `Slots<UptimeClock>` is not thread-safe.

Squelch expiry is lazy: no background timer. `expireSquelch` removes stale entries on next send. Out-of-bounds durations in incoming `TMSquelch` trigger `feeInvalidData` and `removeSquelch` (defensive clear).

### Slot Selection Algorithm (`Slot<clock_type>::update`)

Two-threshold design: peers enter the *considered pool* at `MIN_MESSAGE_THRESHOLD=19` messages; selection fires when `MAX_SELECTED_PEERS=5` peers individually reach `MAX_MESSAGE_THRESHOLD=20`. The one-message gap lets the system confirm a peer has continued sending before committing it as a candidate. If fewer than 5 non-idle peers are available at selection time, `initCounting()` resets and defers — never squelches with incomplete picture.

Inactivity (`IDLED=8s`): idle selected peer → unsquelch all + revert to `Counting`. Slots whose `lastSelected_` is older than `MAX_UNSQUELCH_EXPIRE_DEFAULT=600s` are deleted by `deleteIdlePeers()`.

Squelch duration scaled by peer count: `min(max(600s, 10s × npeers), 3600s)`.

## TX Reduce-Relay

When `txReduceRelayEnabled_` (negotiated via `FEATURE_TXRR`):
- Full transactions go to a quota of peers (computed from `TX_REDUCE_RELAY_MIN_PEERS` and `TX_RELAY_PERCENTAGE`)
- Remaining peers get hash announcements via `addTxQueue` → batched `TMHaveTransactions` flushed by periodic `sendTxQueue`
- Peers without the feature always get full message (back-compat)
- Peer list is shuffled with `default_prng()` to avoid systematic bias
- `MAX_TX_QUEUE_SIZE=10000` cap; `doTransactions` rejects requests exceeding this as malformed

## Tracking State

`tracking_` (atomic `Tracking` enum): `unknown`, `converged`, `diverged`. Thresholds from `Tuning.h`:
- `convergedLedgerLimit=24` — within this many ledgers of validated index
- `divergedLedgerLimit=128` — beyond this, mark diverged and start the `MAX_DIVERGED_TIME` countdown

Hysteresis (24 vs 128) prevents oscillation on slightly-behind peers.

## Send Queue Backpressure (`Tuning.h`)

Three tiers:
- `targetSendQueue=128` — below this, peer is healthy; resets `large_sendq_` counter
- `sendqIntervals=4` — consecutive 1-second ticks at-or-above target before disconnect
- `dropSendQueue=192` — refuse new query responses (don't do expensive lookups for stuck peer)
- `sendQueueLogFreq=64` — log every 64th enqueue when queue is large (throttle log spam)

Other key tuning constants:
- `softMaxReplyNodes=8192`/`hardMaxReplyNodes=12288` — soft/hard caps for `TMLedgerData` node counts
- `maxQueryDepth=3` — recursion limit for `TMGetLedger`; deeper queries rejected as `badData`
- `checkIdlePeers=4` — modulo for timer-driven idle peer scan
- `readBufferBytes=16384` — `constexpr size_t` for socket read buffer (separate from enum for type reasons)

## PeerFinder Sub-Module

Implements peer address discovery, slot accounting, and reachability checks. Owned by `OverlayImpl` via `make_Manager()`. Hidden behind `Manager` abstract interface; concrete `ManagerImp` lives in `detail/PeerfinderManager.cpp`.

### Components

- **`Logic<Checker>`**: central decision engine. Holds `slots_`, `connectedAddresses_` (multiset for IP limit), `keys_` (dedup public keys), `fixed_`, `livecache_`, `bootcache_`. Guarded by `std::recursive_mutex lock_`. Recursive mutex needed because `on_closed()` calls `remove()` independently.
- **`Livecache`**: ~30s TTL gossip cache (`Tuning::liveCacheSecondsToLive`). `beast::aged_map` + `boost::intrusive::list` per hop bucket (size `maxHops+2=9`, indices 0–8). MUST `shuffle()` before handout — `push_front` insertion is exploitable otherwise.
- **`Bootcache`**: persistent (SQLite via `StoreSqdb`). Bimap (`unordered_set_of` by endpoint, `multiset_of` by valence) for O(1) update and ranked iteration. Valence is a streak counter (clamped to 0 before crossing sign). `staticValence=32` for `[ips]`/`[ips_fixed]`. Throttled writes: 60s cooldown via `flagForUpdate`/`checkUpdate`; destructor force-flushes. Pruning: remove bottom 10% when over 1000 entries.
- **`Checker<Protocol>`**: async TCP probe for verifying peer's advertised listening port. Self-managing `async_op` via `shared_ptr` capture in handler; `~Checker` calls `wait()` (not `stop()` — must call `stop()` first explicitly).
- **`Counts`**: pure bookkeeping, no own mutex (relies on `Logic::lock_`). All updates funnel through private `adjust(slot, ±1)`. Fixed/reserved bypass active caps in `can_activate`. `isConnectedToNetwork()` returns `true` only when `m_out_max == 0` (pure listener mode).
- **`SlotImp`**: concrete slot state. Two constructors: inbound takes both endpoints, sets `checked=false,canAccept=false`; outbound only takes remote, sets `checked=true,canAccept=true` since TCP connect itself proves reachability. State machine enforced by `XRPL_ASSERT` in `state()` and `activate()`. `m_listening_port` is `std::atomic<int32_t>` with `-1` sentinel.
- **`Fixed`**: per-fixed-peer backoff. Fibonacci sequence in minutes: `{1,1,2,3,5,8,13,21,34,55}`, clamped to last index. `failure()` advances; `success()` resets.
- **`Source`**: abstract; only concrete is `SourceStrings` (config `[ips]`). `cancel()` is a no-op for all current (synchronous) implementations but exists as an extension point for future async sources.

### Autoconnect Tier Order

`Logic::autoconnect()` strictly returns at first non-empty tier:
1. Fixed peers (via `get_fixed`, respecting `Fixed::when()` backoff)
2. Livecache (shuffled, reverse hop order — far peers first for topological diversity)
3. (Bootcache refill placeholder for DNS)
4. Bootcache fallback

`m_squelches` aged set (60s TTL, `Tuning::recentAttemptDuration`) suppresses rapid retries to same address across calls.

### Endpoint Gossip

- **Receiving (`on_endpoints` + `preprocess`)**: rate-limited via per-slot `whenAcceptEndpoints` (`Tuning::secondsPerMessage=151s`, a prime to desync nodes). Random sample-down if oversized. `hops==0` entry's IP replaced with sender's socket address (peer doesn't know own public IP). All surviving hops incremented by 1 before livecache insert. First-hop entries trigger `Checker::async_connect` for reachability test.
- **Sending (`buildEndpointsForPeers`)**: shuffle slots, use `SlotHandouts` per peer, run `handout()` algorithm. Self-advertisement uses zero-address IPv6 sentinel — receiver substitutes socket's remote address.
- **`Handouts` algorithm**: round-robin across multiple targets to ensure fair distribution. `move_back` after each acceptance rotates endpoints. Per-target dedup via `SlotImp::recent_t` (aged map; `filter()` uses `<=` hop comparison; `try_insert` writes both received and sent into recent — pessimistic update).

### `recent_t` Filter Semantics

`insert()` updates cached hop count only if new value ≤ existing. `filter()` suppresses sends when cached hop ≤ sending hop. The `<=` boundary is intentional — sending at a strictly lower hop than the peer knows is still useful; matching or higher is redundant.

## TLS Channel-Binding (Non-Standard)

`makeSharedValue` derives a 256-bit value from TLS finished messages:
```
sha512Half(SHA512(my_finished) XOR SHA512(peer_finished))
```
Rejects degenerate zero-XOR case. Non-standard (see OpenSSL #5509, XRPLF/rippled #2413). TLS cert verification is explicitly disabled (`verify_none`) — security comes from binding node-public-key signature to this shared value via `Session-Signature` HTTP header. MITM produces different finished values → signature mismatch → rejection.

## Handshake HTTP Headers

Built by `buildHandshake`, verified by `verifyHandshake`. Verify order is layered (cheap → expensive):
1. `Network-ID` mismatch
2. `Network-Time` ±20s tolerance
3. `Public-Key` parse, self-connection check
4. `Session-Signature` cryptographic verify
5. `Local-IP`/`Remote-IP` cross-check (NAT diagnostics)

Feature negotiation via `X-Protocol-Ctl`: `compr=lz4`, `vprr=1`, `txrr=1`, `ledgerreplay=1`. Responder echoes back only features locally configured AND requested (AND-gate). Initiator unconditionally advertises all locally supported features.

## ZeroCopy I/O Adapters

`ZeroCopyInputStream<Buffers>` wraps `ConstBufferSequence` for protobuf parsing without intermediate copy. `BackUp`/`Skip` support sub-buffer granularity via tracked `pos_` within current buffer. Empty buffer sequence is safe (null `pos_` initialized in constructor).

`ZeroCopyOutputStream<Streambuf>` uses deferred commit pattern: `commit_` tracks bytes promised but not yet committed. Destructor MUST flush trailing `commit_` — protobuf doesn't guarantee terminal `BackUp` or `Next` call. `BackUp(n)` asserts `n <= commit_` and prevents double-commit.

## Traffic Categorization

`TrafficCount::categorize()` is called once at `Message` construction (outbound) and per inbound message. Two-stage: static `unordered_map<MessageType, category>` for simple types, then `dynamic_cast` for protobuf inspection of `TMLedgerData`/`TMGetLedger` (`requestcookie` distinguishes forwarded vs originated) and `TMGetObjectByHash` (`query()` flag determines get/share). `unknown` is NOT rolled into `total`. `squelch_suppressed` records bytes NOT transmitted due to squelch; `squelch_ignored` records bytes from peers ignoring squelch.

## Compression Eligibility (`Message::compress`)

Skip if ≤70 bytes. Whitelist of eligible types: `mtMANIFESTS`, `mtENDPOINTS`, `mtTRANSACTION`, `mtGET_LEDGER`, `mtLEDGER_DATA`, `mtGET_OBJECTS`, `mtVALIDATOR_LIST`, `mtVALIDATOR_LIST_COLLECTION`, `mtREPLAY_DELTA_RESPONSE`, `mtTRANSACTIONS`. Excludes high-frequency control messages (`mtPING`, `mtVALIDATION`, `mtPROPOSE_LEDGER`, `mtSTATUS_CHANGE`). If compressed size doesn't beat uncompressed minus 4-byte header overhead, fall back to uncompressed (`bufferCompressed_` cleared, `getBuffer()` returns uncompressed).

## HTTP Endpoints (served by `OverlayImpl::processRequest`)

- `/crawl` — JSON topology, gated by bitmask config (`CrawlOptions::Overlay|ServerInfo|ServerCounts|Unl`)
- `/health` — three-tier status (200/503/500) — HTTP status encodes result so LBs need no JSON parsing
- `/vl/<key>` or `/vl/<version>/<key>` — signed validator list

## Concurrency Notes

- `OverlayImpl::mutex_` is `std::recursive_mutex` (acknowledged tech debt: `// VFALCO use std::mutex`). Recursion stems from `run()` triggering callbacks back into overlay.
- `cond_` is `condition_variable_any` (needs Lockable, not BasicLockable) for shutdown drain
- `work_` (`executor_work_guard` as `std::optional`) keeps `io_context` alive; `reset()` during `stop()` lets queue drain
- Strand vs mutex: peer registry mutations use `mutex_`; timer/squelch/tx-metrics work uses strand
- `OverlayImpl::Child` registration: destructor auto-removes from `list_`; `stopChildren()` copies pointers before iterating to avoid invalidation
- `PeerImp` field locks: `recentLock_` (ledger state, latency), `nameMutex_` (`shared_mutex` for `name_`); strand-confined fields need no lock
- `TxMetrics` has its own `std::mutex`; writers additionally serialize via overlay strand; RPC readers call `json()` directly without going through strand
- `Cluster::mutex_` is non-recursive — `for_each` callback must not call `update()`
- `PeerReservationTable`: `list()` deliberately releases lock before `std::sort` to minimize hold time (snapshot sort pattern)

## Cluster Registry

`Cluster` owns a `std::set<ClusterNode, Comparator>`. The `Comparator` is `is_transparent` — enables `find(PublicKey)` without constructing a dummy `ClusterNode`. `update()` enforces monotonic time (rejects stale reports), preserves names across nameless gossip updates, and uses erase+`emplace_hint` for O(1) amortized reinsert. `load()` is fail-fast on malformed lines (returns `false`) but tolerates duplicates with a warning (first entry wins).

## PeerSet (Data Acquisition)

`PeerSet` / `PeerSetImpl` manages the working set of peers queried for a single in-flight data acquisition (ledger, tx-set, etc.). Uses scored peer selection (`Peer::getScore(hasItem)`) sorted descending. `peers_` set of `Peer::id_t` acts as exclusion list — same peer is never re-added across retries. `DummyPeerSet` via `make_DummyPeerSet()` is the null-object used when `loadOldLedger()` needs `InboundLedger` without live peers.

## Key Files

### Overlay core
- `src/xrpld/overlay/Overlay.h` / `detail/OverlayImpl.{h,cpp}` — main manager
- `src/xrpld/overlay/Peer.h` / `detail/PeerImp.{h,cpp}` — per-peer logic
- `src/xrpld/overlay/Message.h` / `detail/Message.cpp` — wire envelope, lazy compression
- `src/xrpld/overlay/detail/ConnectAttempt.{h,cpp}` — outbound connection state machine
- `src/xrpld/overlay/detail/Handshake.{h,cpp}` — handshake crypto, feature negotiation
- `src/xrpld/overlay/detail/ProtocolMessage.h` — wire framing, dispatch
- `src/xrpld/overlay/detail/ProtocolVersion.{h,cpp}` — `XRPL/x.y` negotiation
- `src/xrpld/overlay/detail/ZeroCopyStream.h` — protobuf/Asio buffer adapters
- `src/xrpld/overlay/detail/Tuning.h` — all overlay magic numbers
- `src/xrpld/overlay/make_Overlay.h` — factory + `setup_Overlay` (parses `[overlay]`, `[crawl]`, `[vl]`, `[network_id]`)
- `src/xrpld/overlay/predicates.h` — composable peer-selection/dispatch functors for `Overlay::foreach`

### Reduce-relay
- `src/xrpld/overlay/Slot.h` — per-validator state machine + selection algorithm
- `src/xrpld/overlay/Squelch.h` — per-peer suppression enforcement
- `src/xrpld/overlay/ReduceRelayCommon.h` — all reduce-relay constants

### Telemetry
- `src/xrpld/overlay/detail/TrafficCount.{h,cpp}` — per-category byte/message counters
- `src/xrpld/overlay/detail/TxMetrics.{h,cpp}` — rolling averages for tx reduce-relay

### Cluster
- `src/xrpld/overlay/Cluster.h` / `ClusterNode.h` / `detail/Cluster.cpp` — trusted-node registry with heterogeneous lookup

### PeerSet (data acquisition)
- `src/xrpld/overlay/PeerSet.h` / `detail/PeerSet.cpp` — scored peer selection for InboundLedger etc.

### Reservations
- `src/xrpld/overlay/detail/PeerReservationTable.cpp` — persistent allowlist via SQLite

### Compression
- `src/xrpld/overlay/Compression.h` — `Algorithm` enum, dispatch wrappers, wire-format constants

### PeerFinder
- `src/xrpld/peerfinder/PeerfinderManager.h` / `Slot.h` / `make_Manager.h` — public interface
- `src/xrpld/peerfinder/detail/Logic.h` — central decision engine
- `src/xrpld/peerfinder/detail/Livecache.h` / `Bootcache.{h,cpp}` — address caches
- `src/xrpld/peerfinder/detail/Checker.h` — async reachability prober
- `src/xrpld/peerfinder/detail/SlotImp.{h,cpp}` — slot state machine
- `src/xrpld/peerfinder/detail/Counts.h` — slot bookkeeping
- `src/xrpld/peerfinder/detail/Fixed.h` — Fibonacci backoff
- `src/xrpld/peerfinder/detail/Handouts.h` — fair distribution algorithm
- `src/xrpld/peerfinder/detail/StoreSqdb.h` — SQLite persistence (schema v4, migration handles `DROP COLUMN` via table rename)
- `src/xrpld/peerfinder/detail/Source.h` / `SourceStrings.{h,cpp}` — abstract + static-string address source
- `src/xrpld/peerfinder/detail/Tuning.h` — peerfinder magic numbers
- `src/xrpld/peerfinder/detail/iosformat.h` — `leftw` stream manipulator for log alignment
