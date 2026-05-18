# `Tuning.h` — Overlay Networking Constants

This file is the single source of truth for all hard-coded threshold values that govern peer-to-peer behavior in the XRPL overlay network. It lives inside the `detail/` subdirectory alongside `PeerImp.h` and `OverlayImpl.h`, and virtually every policy decision in `PeerImp.cpp` and `OverlayImpl.cpp` traces back to a constant defined here.

## Ledger Tracking: Convergence and Divergence

`convergedLedgerLimit = 24` and `divergedLedgerLimit = 128` are the thresholds used by `PeerImp::checkTracking()` to classify a peer's ledger state. When the absolute difference between the peer's reported ledger sequence and the network's validated ledger sequence is below 24, the peer is marked `Tracking::converged`. When it exceeds 128, the peer is marked `Tracking::diverged` and a timestamp is recorded — if that diverged state persists for longer than `app_.config().MAX_DIVERGED_TIME`, the peer is disconnected. The gap between the two values (24 vs. 128) is deliberate: it creates hysteresis so a peer that is slightly behind doesn't oscillate between states every few ledgers, while a genuinely stalled peer gets a firm cutoff.

## Reply Node Limits: Soft and Hard Caps

`softMaxReplyNodes = 8192` and `hardMaxReplyNodes = 12288` protect against oversized ledger-data replies. In `processLedgerRequest()`, the outgoing loop stops appending nodes once it reaches `softMaxReplyNodes`, leaving headroom before the hard cap. Incoming `TMGetLedgerData` replies are rejected outright if they contain more than `hardMaxReplyNodes` nodes. The soft/hard split separates "stop building" from "reject as malformed" — an honest node always stops well before the hard cap, so any message exceeding it is treated as either a bug or an attack.

## Send Queue Backpressure: Three Tiers

The send queue constants implement a layered flow-control policy:

- **`targetSendQueue = 128`**: The queue size below which a peer is considered well-behaved. Each time a message is enqueued and the queue is below this target, the `large_sendq_` counter resets to zero.
- **`sendqIntervals = 4`**: Number of consecutive timer ticks (each tick is roughly one second) during which the queue must remain at or above `targetSendQueue` before the peer is forcibly disconnected via `fail("Large send queue")`. This tolerates short bursts without punishing peers for momentary slowness.
- **`dropSendQueue = 192`**: A harder cutoff. When the queue reaches this size, new query responses are refused (`send_queue_.size() >= Tuning::dropSendQueue` guards appear in at least two places in `PeerImp.cpp`). This prevents the node from doing expensive ledger lookups for a peer that cannot consume the results.
- **`sendQueueLogFreq = 64`**: Throttles debug logging: a queue size is only logged once per 64 messages enqueued, avoiding log spam during a slow peer event.

Together these create a graduated response: tolerate, log, refuse queries, then disconnect.

## Idle Peer Checking

`checkIdlePeers = 4` is used in `OverlayImpl`'s periodic timer callback as a modulo divisor: `(++timer_count_ % Tuning::checkIdlePeers) == 0`. Rather than running the idle-peer scan on every timer tick, this amortizes its cost across four ticks, reducing contention over the peer list during normal operation.

## Query Depth Cap

`maxQueryDepth = 3` limits how deeply a ledger data request may recurse through indirect queries. A node receiving a `TMGetLedger` message with `querydepth` exceeding this value rejects it with a `badData` error. This prevents an attacker from manufacturing a chain of indirect ledger requests that forces the node to fan out expensive I/O work.

## Socket Read Buffer

`readBufferBytes = 16384` is declared as `constexpr std::size_t` rather than as part of the anonymous enum, because it is used directly as the argument to `boost::asio::buffer` size calculations and `DynamicBuffer::prepare()`, which expect a `std::size_t`. Placing it in the enum would require repeated casts. The 16 KiB value matches a common TCP socket buffer alignment and is large enough to absorb most individual protocol messages in a single read.

## Design Note

All values are grouped inside the nested `Tuning` namespace within `xrpl`, with integer constants expressed as an anonymous `enum`. This is a pre-C++17 idiom that gives compile-time integer constants with internal linkage without requiring `constexpr` declarations or out-of-line definitions. Centralizing all of these thresholds in one header means that performance tuning or protocol hardening only requires changes in one place — no magic numbers are scattered across `PeerImp.cpp` or `OverlayImpl.cpp`.