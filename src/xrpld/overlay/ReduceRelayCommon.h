/** @file
 *  Shared tuning constants for the XRPL reduce-relay gossip optimisation.
 *
 *  All components of the reduce-relay subsystem — `Slot.h`, `Squelch.h`,
 *  and `PeerImp.cpp` — import this header so that numeric policy is defined
 *  in one place. The feature replaces full-flood validator message gossip
 *  with a selective scheme: a small set of "selected" peers relays each
 *  validator's messages while the remainder are squelched via `TMSquelch`
 *  for a bounded, randomised window.
 *
 *  @see https://xrpl.org/blog/2021/message-routing-optimizations-pt-1-proposal-validation-relaying.html
 */

#pragma once

#include <chrono>

namespace xrpl::reduce_relay {

// --- Squelch duration bounds ---

/** Minimum duration of a squelch window.
 *
 *  A squelched peer is instructed to suppress relay for at least this long.
 *  `Squelch<clock_type>::addSquelch()` rejects any incoming `TMSquelch`
 *  whose duration falls below this bound as out-of-range.
 */
static constexpr auto kMIN_UNSQUELCH_EXPIRE = std::chrono::seconds{300};

/** Baseline ceiling for the randomised squelch window.
 *
 *  When the number of squelched peers is small (fewer than 60), the upper
 *  bound of the squelch duration is clamped to this value.  Also used by
 *  `Slot` to detect a stale selection round: if the time since the last
 *  selection exceeds `2 * kMAX_UNSQUELCH_EXPIRE_DEFAULT`, the slot resets
 *  to `Counting` state to re-run peer selection.
 */
static constexpr auto kMAX_UNSQUELCH_EXPIRE_DEFAULT = std::chrono::seconds{600};

/** Per-peer scaling factor for the squelch window upper bound.
 *
 *  The computed ceiling is
 *  `max(kMAX_UNSQUELCH_EXPIRE_DEFAULT, kSQUELCH_PER_PEER * number_of_peers)`,
 *  capped at `kMAX_UNSQUELCH_EXPIRE_PEERS`.  A larger peer set therefore
 *  receives a proportionally longer squelch, reducing the chance that many
 *  peers unsquelch simultaneously and cause a relay burst.
 */
static constexpr auto kSQUELCH_PER_PEER = std::chrono::seconds(10);

/** Absolute maximum squelch duration regardless of peer count.
 *
 *  Caps the per-peer-scaled ceiling so that no peer is ever silenced for
 *  more than one hour.  `Squelch<clock_type>::addSquelch()` rejects any
 *  incoming `TMSquelch` whose duration exceeds this bound.
 */
static constexpr auto kMAX_UNSQUELCH_EXPIRE_PEERS = std::chrono::seconds{3600};

// --- Idle detection ---

/** Inactivity threshold after which a peer is considered idle.
 *
 *  If no message from a tracked peer has been observed within this window,
 *  `Slot::deleteIdlePeer()` treats the peer as disconnected.  When an
 *  *elected* peer idles, all squelched peers are immediately unsquelched
 *  and the slot reverts to `Counting` state to restart peer selection.
 *
 *  Also used by `Slots` as the expiry duration for the `peersWithMessage_`
 *  aged map, which deduplicates messages seen within the same idle window,
 *  and by `PeerImp` to suppress duplicate relay of recently forwarded
 *  validator messages.
 */
static constexpr auto kIDLED = std::chrono::seconds{8};

// --- Peer selection thresholds ---

/** First-stage message count for admitting a peer to the candidate pool.
 *
 *  A peer must surpass this count before it is added to the *considered*
 *  pool inside `Slot`.  Only peers in the considered pool are eligible to
 *  be selected as relay sources.  The one-message gap between this value
 *  and `kMAX_MESSAGE_THRESHOLD` confirms that the peer has continued
 *  sending before it is committed as a candidate.
 *
 *  @see kMAX_MESSAGE_THRESHOLD
 */
static constexpr uint16_t kMIN_MESSAGE_THRESHOLD = 19;

/** Second-stage message count that triggers a selection round.
 *
 *  Once `kMAX_SELECTED_PEERS` distinct peers each individually reach this
 *  count, `Slot` fires a selection round: `kMAX_SELECTED_PEERS` peers are
 *  randomly chosen as relay sources and the rest are squelched.
 *
 *  @see kMIN_MESSAGE_THRESHOLD
 */
static constexpr uint16_t kMAX_MESSAGE_THRESHOLD = 20;

/** Maximum number of peers selected as relay sources per validator.
 *
 *  Keeping multiple sources provides redundancy if a selected peer
 *  disconnects or idles; keeping the number small limits redundant
 *  bandwidth.  Five is the chosen balance between resilience and
 *  bandwidth reduction.
 */
static constexpr uint16_t kMAX_SELECTED_PEERS = 5;

// --- Boot-up guard ---

/** Delay before reduce-relay becomes active after process start.
 *
 *  `Slots::reduceRelayReady()` returns `false` until this much time has
 *  elapsed since the process epoch.  A freshly started node has not yet
 *  established a stable peer set; activating squelching during this churn
 *  phase could silence relay paths before a healthy selection is possible.
 */
static constexpr auto kWAIT_ON_BOOTUP = std::chrono::minutes{10};

// --- TX reduce-relay queue cap ---

/** Maximum number of transaction hashes buffered per peer for batch relay.
 *
 *  `PeerImp::addTxQueue()` flushes the outbound `TMTransactions` batch when
 *  this limit is reached.  `PeerImp::doTransactions()` rejects incoming
 *  requests that exceed this count as malformed.  The cap is sized so that
 *  the resulting `TMTransactions` payload stays well within the 64 MiB
 *  protocol message size limit even at high transaction throughput.
 */
static constexpr std::size_t kMAX_TX_QUEUE_SIZE = 10000;

}  // namespace xrpl::reduce_relay
