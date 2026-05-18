# `Slot.h` — Reduce-Relay Squelch Logic for Validator Messages

## Role in the System

XRPL nodes maintain an overlay mesh where each node forwards incoming validator messages (proposals and validations) to all connected peers — a flooding gossip model. At scale this creates quadratic message overhead: every validator message is relayed O(peers × validators) times across the network. `Slot.h` implements the *reduce-relay* mechanism that cuts this down by designating a small fixed set of peers as the authoritative source for each validator's messages, then instructing all other peers to stop forwarding those messages via a timed "squelch" command.

The entire file lives in the `xrpl::reduce_relay` namespace and is a header-only template implementation consisting of three cooperating abstractions: `SquelchHandler`, `Slot`, and `Slots`.

## `SquelchHandler` — Inversion of Control for Callbacks

`SquelchHandler` is a pure abstract base with two virtual methods: `squelch()` and `unsquelch()`. `OverlayImpl` inherits from it and provides the real implementation that sends `TMSquelch` protocol messages to peers. The indirection exists explicitly for testability — the comment in the source notes it allows "on the fly changing callbacks" in unit tests. This pattern avoids coupling the selection algorithm to the network layer and lets tests inject mock handlers without modifying `Slot` logic.

## `Slot<clock_type>` — Per-Validator Peer Selection State Machine

Each `Slot` is associated with exactly one validator (by public key) and owns all state needed to decide which peers should relay that validator's messages. The constructor is `private` — only `Slots` (its `friend`) can create instances.

### Dual State Machines

There are two independent state machines running in parallel:

**`SlotState`** tracks the slot as a whole: `Counting` (actively counting messages to decide which peers to select) or `Selected` (peers have been chosen, counting is suspended).

**`PeerState`** tracks each individual peer within the slot: `Counting` (receiving and accumulating messages), `Selected` (designated relay source), or `Squelched` (instructed to stop forwarding).

### The Selection Algorithm — `update()`

The core logic lives in `update()`. On each incoming message from a validator routed through peer `id`:

1. **New peer**: Insert with `PeerState::Counting` and call `initCounting()` to reset the whole slot back to counting state. This ensures a newly-seen peer doesn't get permanently excluded from the selection pool.

2. **Expired squelch**: If the peer was `Squelched` but its expiry timestamp has passed, reset it to `Counting` and reinitiate the counting round. This is how squelch periods naturally expire without an active timer.

3. **Active counting**: Increment the peer's message count. Once it exceeds `MIN_MESSAGE_THRESHOLD` (19 messages), the peer is added to the `considered_` pool — candidates for selection. When a peer's count hits `MAX_MESSAGE_THRESHOLD + 1` (21), it increments `reachedThreshold_`.

4. **Inactivity guard**: If `lastSelected_` is more than `2 × MAX_UNSQUELCH_EXPIRE_DEFAULT` (20 minutes) in the past, the slot resets via `initCounting()` rather than proceeding. This handles the case where the node was nearly disconnected and its peer set has churned.

5. **Selection trigger**: When `reachedThreshold_` reaches `maxSelectedPeers_` (configurable, default 5), selection fires. The algorithm randomly draws peers from `considered_`, skipping any that have been idle for more than `IDLED` (8 seconds). If fewer than `maxSelectedPeers_` non-idle peers can be found, `initCounting()` resets everything and defers to the next round — it would rather wait than squelch with an incomplete picture.

6. **Squelching**: For every peer not in `selected` that isn't already `Squelched`, the handler's `squelch()` callback fires with a randomized duration, and the peer's state is set to `Squelched`. All message counts reset and the slot transitions to `SlotState::Selected`.

The two-threshold design (`MIN_MESSAGE_THRESHOLD` / `MAX_MESSAGE_THRESHOLD`) is deliberate: a peer must receive at least 19 messages to enter the candidate pool, but selection only triggers when 5 peers have reached 20. This prevents premature selection when only a few fast peers have been heard from.

### Squelch Duration — `getSquelchDuration()`

Durations are randomized in the range `[MIN_UNSQUELCH_EXPIRE, max_squelch]` where `max_squelch = min(max(600s, 10s × npeers), 3600s)`. The `npeers` parameter is the number of peers being squelched in this round. The scaling ensures that nodes with many peers assign longer squelch windows, reducing the thundering-herd effect where all squelches expire simultaneously and all peers rush to relay again at once.

### Recovery from Peer Loss — `deletePeer()` and `deleteIdlePeer()`

The system must react gracefully when a selected relay source disappears. Two mechanisms handle this:

`deletePeer(validator, id, erase)` is called when a peer disconnects. If the removed peer was `Selected`, the slot calls `unsquelch()` for every currently-`Squelched` peer, resets all state to `Counting`, and transitions back to `SlotState::Counting` — triggering a fresh selection round. The `erase` flag distinguishes a true disconnect (remove the `PeerInfo` entry) from an idle transition (keep the entry but reset its counts).

`deleteIdlePeer()` is called periodically and walks all peers. Any peer whose `lastMessage` timestamp is older than `IDLED` (8s) is treated as a silent disconnect and passed to `deletePeer(..., false)`. This catches peers that have stopped sending without a clean disconnect event.

## `Slots<clock_type>` — Container and Lifecycle Manager

`Slots` owns all per-validator `Slot` instances in a `hash_map<PublicKey, Slot<clock_type>>`. It is the only entry point callers use.

### Message Deduplication — `peersWithMessage_`

`peersWithMessage_` is a `beast::aged_unordered_map` keyed by message hash (`uint256`), storing the set of peer IDs that have forwarded each message. Before updating any slot, `addPeerMessage()` checks this map — if the (message, peer) pair has already been seen, the update is skipped entirely. Entries age out after `IDLED` (8s), matching the TTL window within which a peer is expected to forward any given message. This is declared `inline static`, meaning it is shared across all `Slots` instantiations rather than per-instance.

### Boot Delay

`reduceRelayReady()` returns false until `WAIT_ON_BOOTUP` (10 minutes) has elapsed since process start. This gives the node time to establish a representative set of peer connections before the selection algorithm begins squelching — avoiding a scenario where only the first two or three peers to connect are ever chosen.

### Idle Slot Expiry — `deleteIdlePeers()`

Beyond idle peer detection, `deleteIdlePeers()` also deletes entire slots whose `lastSelected_` is older than `MAX_UNSQUELCH_EXPIRE_DEFAULT` (600s). A validator that has stopped producing messages shouldn't leave stale state indefinitely; removing the slot frees memory and ensures a clean start if the validator resumes.

### Integration with `OverlayImpl`

`OverlayImpl` holds a `reduce_relay::Slots<UptimeClock>` member and inherits from `SquelchHandler`. When `PeerImp` receives a validation or proposal that has been relayed within the `IDLED` window, it calls `overlay_.updateSlotAndSquelch()`. The slot system then fires `squelch()` back through `OverlayImpl`, which sends `TMSquelch` messages over the wire to tell remote peers to stop forwarding. The peer-side processing of received `TMSquelch` messages is handled separately in `PeerImp`, which uses a per-peer `Squelch` object tracking which validators it has been told to suppress.