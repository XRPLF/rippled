/** @file
 *  Single source of truth for all hard-coded thresholds governing peer-to-peer
 *  behavior in the XRPL overlay network.
 *
 *  Every policy decision in `PeerImp.cpp` and `OverlayImpl.cpp` that involves
 *  a numeric limit traces back to a constant defined here.  Centralizing them
 *  in one header means that performance tuning or protocol hardening only
 *  requires a change in one place.
 */

#pragma once

/** Hard-coded thresholds that govern XRPL overlay peer-to-peer behavior.
 *
 *  All integer constants are expressed as `static constexpr auto` to give
 *  compile-time values with internal linkage without requiring out-of-line
 *  definitions.  The sole exception is `kREAD_BUFFER_BYTES`, which is typed
 *  as `std::size_t` so it can be passed directly to Asio buffer APIs without
 *  a cast.
 */
namespace xrpl::Tuning {

/** Ledger-sequence difference below which a peer is considered converged.
 *
 *  Used by `PeerImp::checkTracking()`: when `|seq1 - seq2| < kCONVERGED_LEDGER_LIMIT`
 *  the peer's tracking state is set to `Tracking::converged`.  The wide gap
 *  between this value and `kDIVERGED_LEDGER_LIMIT` (24 vs. 128) creates
 *  hysteresis so a slightly-behind peer does not oscillate between states.
 *
 *  @see kDIVERGED_LEDGER_LIMIT
 */
static constexpr auto kCONVERGED_LEDGER_LIMIT = 24;

/** Ledger-sequence difference above which a peer is considered diverged.
 *
 *  Used by `PeerImp::checkTracking()`: when `|seq1 - seq2| > kDIVERGED_LEDGER_LIMIT`
 *  the peer is marked `Tracking::diverged` and a timestamp is recorded.
 *  If the peer remains diverged for longer than `Config::MAX_DIVERGED_TIME`
 *  it is disconnected.  The large gap relative to `kCONVERGED_LEDGER_LIMIT`
 *  is intentional: it gives a genuinely stalled peer a firm cutoff while
 *  avoiding spurious disconnections for momentarily slow peers.
 *
 *  @see kCONVERGED_LEDGER_LIMIT
 */
static constexpr auto kDIVERGED_LEDGER_LIMIT = 128;

/** Soft cap on the number of ledger-tree nodes included in a single reply.
 *
 *  The outgoing loop in `processLedgerRequest()` stops appending nodes once
 *  this limit is reached, leaving headroom before the hard cap.  An honest
 *  node will always stop here, so any incoming message that exceeds
 *  `kHARD_MAX_REPLY_NODES` is treated as a bug or an attack.
 *
 *  @see kHARD_MAX_REPLY_NODES
 */
static constexpr auto kSOFT_MAX_REPLY_NODES = 8192;

/** Hard cap on the number of ledger-tree nodes accepted in a single reply.
 *
 *  Incoming `TMLedgerData` messages are rejected with `badData` if
 *  `nodes_size() > kHARD_MAX_REPLY_NODES`.  Because honest senders stop at
 *  `kSOFT_MAX_REPLY_NODES`, any message that exceeds this higher bound is
 *  treated as either malformed or malicious.
 *
 *  @see kSOFT_MAX_REPLY_NODES
 */
static constexpr auto kHARD_MAX_REPLY_NODES = 12288;

/** Consecutive timer ticks with an oversized send queue before disconnecting.
 *
 *  `PeerImp::onTimer` increments `largeSendq_` each tick that the queue is
 *  at or above `kTARGET_SEND_QUEUE`.  When `largeSendq_` reaches this value
 *  the peer is forcibly disconnected via `fail("Large send queue")`.
 *  Each tick is approximately one second, so the default tolerance is ~4 s.
 *
 *  @see kTARGET_SEND_QUEUE
 */
static constexpr auto kSENDQ_INTERVALS = 4;

/** Send queue depth at which new query responses are refused.
 *
 *  When `send_queue_.size() >= kDROP_SEND_QUEUE`, `PeerImp` skips expensive
 *  ledger lookups and declines to enqueue new query replies.  This prevents
 *  wasting I/O resources on a peer that cannot consume results fast enough.
 *
 *  @see kTARGET_SEND_QUEUE
 *  @see kSENDQ_INTERVALS
 */
static constexpr auto kDROP_SEND_QUEUE = 192;

/** Send queue depth below which a peer is considered well-behaved.
 *
 *  Each time a message is enqueued and the queue is below this threshold,
 *  the `largeSendq_` counter is reset to zero.  This tolerates short bursts
 *  without triggering the disconnect logic in `onTimer`.
 *
 *  @see kSENDQ_INTERVALS
 *  @see kDROP_SEND_QUEUE
 */
static constexpr auto kTARGET_SEND_QUEUE = 128;

/** Modulo divisor for throttling send-queue debug log entries.
 *
 *  When the queue exceeds `kTARGET_SEND_QUEUE`, a debug log entry is written
 *  only when `sendqSize % kSEND_QUEUE_LOG_FREQ == 0`.  Suppresses log spam
 *  during a slow-peer event without hiding the condition entirely.
 */
static constexpr auto kSEND_QUEUE_LOG_FREQ = 64;

/** Timer-tick modulo for the idle-peer scan in `OverlayImpl`.
 *
 *  `OverlayImpl`'s periodic callback runs `deleteIdlePeers()` only when
 *  `++timer_count_ % kCHECK_IDLE_PEERS == 0`, amortizing the cost of
 *  scanning the peer list across four one-second ticks.
 */
static constexpr auto kCHECK_IDLE_PEERS = 4;

/** Maximum recursion depth accepted in a `TMGetLedger` request.
 *
 *  Incoming requests with `querydepth > kMAX_QUERY_DEPTH` are rejected with
 *  `badData`.  This prevents an attacker from chaining indirect ledger
 *  requests that fan out expensive I/O work across the node.
 */
static constexpr auto kMAX_QUERY_DEPTH = 3;

/** Size of the socket read buffer used by `PeerImp`.
 *
 *  Passed directly to `readBuffer_.prepare()` and as the minimum argument to
 *  `stream_.async_read_some()`.  Declared as `std::size_t` rather than as
 *  part of an anonymous enum to avoid repeated casts at Asio call sites.
 *  16 KiB aligns with common TCP socket buffer sizes and is large enough to
 *  absorb most individual protocol messages in a single read.
 */
std::size_t constexpr kREAD_BUFFER_BYTES = 16384;

}  // namespace xrpl::Tuning
