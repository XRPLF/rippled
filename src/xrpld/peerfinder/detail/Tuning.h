/** @file
 *  Single source of truth for every magic number in the PeerFinder subsystem.
 *
 *  All heuristically chosen values — connection policy, backoff schedules,
 *  cache sizes, and gossip rate limits — live here under
 *  `xrpl::PeerFinder::Tuning` so they can be reviewed and adjusted together
 *  rather than being scattered across a dozen implementation files.
 */

#pragma once

#include <array>

/** Heuristically tuned constants for the PeerFinder subsystem. */
/** @{ */
namespace xrpl::PeerFinder::Tuning {

// --- Automatic Connection Policy ---

/** Interval between batches of outbound connection attempts, in seconds.
 *
 *  `Logic::once_per_second` accumulates elapsed time and fires a new
 *  `autoconnect()` batch whenever this threshold is crossed.
 */
static constexpr auto kSECONDS_PER_CONNECT = 10;

/** Maximum number of simultaneous outbound connection attempts.
 *
 *  `Counts::attempts_needed()` consults this directly; once this many
 *  attempts are in-flight no new ones are dispatched, preventing a
 *  connection storm against the rest of the network.
 */
static constexpr auto kMAX_CONNECT_ATTEMPTS = 20;

/** Target percentage of total peer slots that are outbound.
 *
 *  The actual outbound count is `max(kMIN_OUT_COUNT, round(maxPeers *
 *  kOUT_PERCENT / 100))`, keeping inbound capacity dominant so the node
 *  remains openly reachable while still guaranteeing a minimum number of
 *  self-initiated connections.
 *
 *  @see kMIN_OUT_COUNT
 */
static constexpr auto kOUT_PERCENT = 15;

/** Hard floor on the number of outbound connections.
 *
 *  Applied after the percentage calculation so that even a minimal
 *  deployment maintains at least this many self-initiated connections.
 *  Enforced in `PeerfinderConfig.cpp` outside `Logic` so unit tests can
 *  override slot counts freely.
 *
 *  @see kOUT_PERCENT
 */
static constexpr auto kMIN_OUT_COUNT = 10;

/** Default value for `Config::maxPeers`.
 *
 *  21 is odd by design: applying the 15% rule yields 3 outbound peers,
 *  a reasonable default for a light node.
 */
static constexpr auto kDEFAULT_MAX_PEERS = 21;

/** Maximum number of redirect addresses accepted from a single peer.
 *
 *  When a connecting peer's slots are full it may respond with a list of
 *  alternative addresses. This cap prevents a malicious peer from flooding
 *  the address caches by supplying an arbitrarily large redirect list.
 */
static constexpr auto kMAX_REDIRECTS = 30;

// --- Fixed Connection Backoff ---

/** Fibonacci retry-backoff schedule (in minutes) for fixed peers.
 *
 *  `Fixed::failure()` advances an index clamped to the last position,
 *  so backoff saturates at 55 minutes rather than growing unboundedly.
 *  A successful reconnect resets the index to zero (immediate
 *  re-consideration). The Fibonacci progression grows fast enough to
 *  avoid hammering an unreachable host while keeping the worst-case
 *  delay bounded.
 */
static std::array<int, 10> const kCONNECTION_BACKOFF{{1, 1, 2, 3, 5, 8, 13, 21, 34, 55}};

// --- Bootcache ---

/** Entry count threshold above which `Bootcache::trim()` fires.
 *
 *  When the cache exceeds this size the lowest-valence entries are pruned
 *  by `kBOOTCACHE_PRUNE_PERCENT` percent.
 *
 *  @see kBOOTCACHE_PRUNE_PERCENT
 */
static constexpr auto kBOOTCACHE_SIZE = 1000;

/** Percentage of entries removed per trim pass.
 *
 *  Applied to the current cache size: 1000 entries trims to 900 rather
 *  than slashing the cache aggressively.
 *
 *  @see kBOOTCACHE_SIZE
 */
static constexpr auto kBOOTCACHE_PRUNE_PERCENT = 10;

/** Write-coalescing guard for SQLite bootcache updates.
 *
 *  `flagForUpdate()` is called on every valence change, but the actual
 *  SQLite write only occurs if this much time has elapsed since the last
 *  flush. The window is intentionally larger than the typical lifetime of
 *  a transient peer that connects, advertises addresses, and disconnects,
 *  ensuring useful addresses are captured in a single write rather than
 *  triggering multiple redundant flushes.
 */
static std::chrono::seconds const kBOOTCACHE_COOLDOWN_TIME(60);

// --- Livecache ---

/** Gossip horizon: endpoints arriving with hops greater than this are dropped.
 *
 *  The `Endpoint` constructor clamps to `kMAX_HOPS + 1` (7) as a sentinel;
 *  `Logic::on_endpoints()` and the `Handouts` filters then reject anything
 *  above the limit. Six hops provides a network diameter large enough to
 *  reach far corners of a large P2P graph while preventing stale long-chain
 *  entries from polluting the cache.
 */
std::uint32_t constexpr kMAX_HOPS = 6;

/** Number of endpoints to include in each outbound `mtENDPOINTS` message.
 *
 *  Derived as `2 * kMAX_HOPS` (12). `SlotHandouts` stops accepting entries
 *  once this count is reached.
 */
std::uint32_t constexpr kNUMBER_OF_ENDPOINTS = 2 * kMAX_HOPS;

/** Maximum number of endpoints accepted from an inbound `mtENDPOINTS` message.
 *
 *  `Logic::on_endpoints()` randomly trims oversized lists to this bound
 *  before insertion. Clamped to at least 64 to give headroom above the
 *  nominal 12-endpoint send size.
 */
std::uint32_t constexpr kNUMBER_OF_ENDPOINTS_MAX =
    std::max<decltype(kNUMBER_OF_ENDPOINTS)>(kNUMBER_OF_ENDPOINTS * 2, 64);

/** Number of alternative addresses handed to a redirected peer.
 *
 *  When a new connection is rejected because slots are full, this many
 *  livecache addresses are included in the redirect response. Kept small
 *  to avoid bandwidth waste while still providing enough alternatives for
 *  the client to find an open slot.
 */
std::uint32_t constexpr kREDIRECT_ENDPOINT_COUNT = 10;

/** Minimum interval between `mtENDPOINTS` sends or accepts per peer.
 *
 *  A prime value is intentional: it de-synchronizes gossip timers across
 *  nodes, preventing coordinated broadcast floods where many peers all
 *  gossip at the same wall-clock moment.
 *
 *  @note Much longer than `kLIVE_CACHE_SECONDS_TO_LIVE` (30 s) by design —
 *      the asymmetry keeps control-plane traffic low while still allowing
 *      fast cache expiry for peers that drop offline.
 */
std::chrono::seconds constexpr kSECONDS_PER_MESSAGE(151);

/** Time-to-live for a livecache entry.
 *
 *  `Livecache::expire()` and `SlotImp::expire()` both use this value.
 *  Intentionally much shorter than `kSECONDS_PER_MESSAGE` so that a peer
 *  which goes offline does not linger in the cache; a node must receive a
 *  fresh `mtENDPOINTS` message to keep a remote address visible.
 */
std::chrono::seconds constexpr kLIVE_CACHE_SECONDS_TO_LIVE(30);

/** Suppression window for recently attempted outbound addresses.
 *
 *  `Logic::once_per_second()` expires the squelch map using this duration,
 *  preventing the same address from being hammered repeatedly within a
 *  single connection cycle. Port is ignored for comparison purposes.
 */
std::chrono::seconds constexpr kRECENT_ATTEMPT_DURATION(60);

}  // namespace xrpl::PeerFinder::Tuning
/** @} */
