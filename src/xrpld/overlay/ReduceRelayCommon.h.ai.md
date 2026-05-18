# `ReduceRelayCommon.h` — Shared Constants for Reduce-Relay Gossip Optimization

This header is the single source of truth for all tuning parameters that govern the XRPL reduce-relay feature. It contains nothing but a set of `constexpr` constants nested inside `xrpl::reduce_relay`, making it a pure configuration manifest rather than an implementation file. Every component of the reduce-relay subsystem — `Slot.h`, `Squelch.h`, and `PeerImp.cpp` — imports this header to share the same numeric policy.

## Why Reduce-Relay Exists

Prior to this optimization, every validator message (proposals and validations) was flooded to all connected peers, a classic gossip broadcast. As peer counts grow, this creates O(n) redundant relay work per message. The reduce-relay feature, described in the [XRPL blog post](https://xrpl.org/blog/2021/message-routing-optimizations-pt-1-proposal-validation-relaying.html) referenced in the file, replaces flooding with a selective scheme: for each validator, only a small number of "selected" peers are allowed to relay its messages. All other peers are *squelched* — they receive a `TMSquelch` protocol message telling them to stop forwarding that validator's messages for a bounded time window.

## Squelch Duration Constants

The squelch window is intentionally randomized between `MIN_UNSQUELCH_EXPIRE` (300 s) and a computed upper bound, rather than fixed. This prevents the thundering-herd effect where all squelches expire simultaneously, which would cause every formerly-squelched peer to resume relaying in the same second and flood the network anew.

The upper bound of the window is computed as:

```
max_squelch = min(
    max(MAX_UNSQUELCH_EXPIRE_DEFAULT, SQUELCH_PER_PEER * number_of_peers),
    MAX_UNSQUELCH_EXPIRE_PEERS
)
```

`MAX_UNSQUELCH_EXPIRE_DEFAULT` (600 s) is the baseline ceiling. `SQUELCH_PER_PEER` (10 s/peer) scales the ceiling with the number of peers being squelched, so a node with more overlay connections grants longer squelch windows to avoid premature re-flooding. `MAX_UNSQUELCH_EXPIRE_PEERS` (3600 s) caps the absolute maximum at one hour, preventing pathologically long suppression on very large peer sets. `Squelch<clock_type>::addSquelch()` enforces these bounds as a validity check when processing incoming `TMSquelch` messages from upstream peers.

## Peer Selection Thresholds

`MIN_MESSAGE_THRESHOLD` (19) and `MAX_MESSAGE_THRESHOLD` (20) define a two-stage counting gate inside `Slot`. A peer is added to the *considered pool* once it surpasses `MIN_MESSAGE_THRESHOLD` messages. Only when `MAX_SELECTED_PEERS` (5) members of the considered pool each individually cross `MAX_MESSAGE_THRESHOLD` does a selection round fire. The one-message gap between thresholds is intentional: it lets the system observe that a peer has *continued* to send after crossing the first threshold before committing it to the candidate set. The selection round then randomly picks exactly `MAX_SELECTED_PEERS` peers from the considered pool and squelches the rest.

`MAX_SELECTED_PEERS = 5` is the cap on how many relay sources are permitted per validator. This is a direct tradeoff between redundancy (resilience if selected peers drop or idle) and bandwidth reduction. Fewer than five would save more bandwidth but risk message loss if selected peers disconnect.

## Idle Detection

`IDLED` (8 s) is the no-message threshold for declaring a peer inactive. `Slot::deleteIdlePeer()` checks every tracked peer and if the gap since its last message exceeds `IDLED`, it is treated as if it disconnected. If one of the *selected* peers idles, all squelched peers are immediately unsquelched and the slot reverts to `Counting` state to re-run peer selection. This constant is also reused by `Slots` as the expiry duration for the `peersWithMessage_` aged map, which deduplicates messages seen within the same idle window.

## Boot-Up Delay

`WAIT_ON_BOOTUP` (10 minutes) prevents the reduce-relay machinery from activating immediately after the server starts. `Slots::reduceRelayReady()` returns false until this much time has elapsed since epoch. The rationale is that a freshly starting node has not yet established a stable peer set; squelching peers prematurely during this churn phase could silence relay paths before a healthy selection is possible.

## Transaction Queue Cap

`MAX_TX_QUEUE_SIZE` (10,000) limits the number of transaction hashes that can accumulate in a peer's outbound `TMTransactions` batch. `PeerImp::addTxQueue()` flushes the queue whenever this limit is hit, and `doTransactions()` rejects incoming requests with more than this many hashes as malformed. The comment in the source makes the motivation explicit: at high TPS, unbounded accumulation could push `TMTransactions` payloads past the 64 MB protocol message size limit, so this constant is sized to stay safely below that ceiling.