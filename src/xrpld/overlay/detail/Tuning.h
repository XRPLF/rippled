#pragma once
#include <xrpl/shamap/SHAMapInnerNode.h>

#include <cstddef>
#include <cstdint>

namespace xrpl::Tuning {

/** How many ledgers off a server can be and we will
    still consider it converged */
static constexpr std::uint32_t kConvergedLedgerLimit = 24;

/** How many ledgers off a server has to be before we
    consider it diverged */
static constexpr std::uint32_t kDivergedLedgerLimit = 128;

/** The soft cap on the number of ledger entries in a single reply. */
static constexpr auto kSoftMaxReplyNodes = 8192;

/** The hard cap on the number of ledger entries in a single reply. */
static constexpr auto kHardMaxReplyNodes = 12288;

/** How many timer intervals a sendq has to stay large before we disconnect */
static constexpr auto kSendqIntervals = 4;

/** How many messages on a send queue before we refuse queries */
static constexpr auto kDropSendQueue = 192;

/** How many messages we consider reasonable sustained on a send queue */
static constexpr auto kTargetSendQueue = 128;

/** How often to log send queue size */
static constexpr auto kSendQueueLogFreq = 64;

/** How often we check for idle peers (seconds) */
static constexpr auto kCheckIdlePeers = 4;

/** The maximum number of levels to search */
static constexpr auto kMaxQueryDepth = 3;

/** Size of buffer used to read from the socket. */
constexpr std::size_t kReadBufferBytes = 16384;

/** TMGetObjectByHash differential pricing.

    Honest peers ask for at most 8 hashes per call (the header, or up to
    4 state + 4 tx hashes from `InboundLedger::getNeededHashes()`). The
    free tier covers them at zero cost. Beyond that, each lookup is billed:
    'misses' cost much more than 'hits' because a miss does a node store seek
    while a hit is usually served from cache. On top of that, a size-band
    surcharge kicks in for larger requests so an attacker who crams a
    single message with thousands of hashes blows past
    `Resource::kDropThreshold` and gets disconnected.

    The numbers below are picked to keep three things true given
    `kDropThreshold = 25000`:

      - Honest traffic (<= 8 objects per request) is free.
      - A single all-miss request at `kHardMaxReplyNodes` (12288) costs
        more than the drop threshold, so an attacker gets dropped in one
        message.
      - A peer spamming 1024-object hit-only requests gets dropped in
        ~19 messages — fast enough to be useful, slow enough that an
        honest peer momentarily sending oversized requests has time to
        back off. */

/** How many objects a request can ask for before per-lookup billing
    begins?
    Twice the honest peak (8) so a peer that occasionally retries a hash
    never trips pricing. Same value as `SHAMapInnerNode::kBranchFactor`;
    that's a coincidence, not a requirement. */
static constexpr auto kFreeObjectsPerRequest = 16;

/** Cost of one cache-hit lookup. The unit; everything else is a
    multiple of this. */
static constexpr auto kCostPerLookupHit = 1;

/** Cost of one node-store miss, in units of `kCostPerLookupHit`.

    A miss does a node store disk seek; a hit usually comes from cache.
    The 8x ratio is an order-of-magnitude guess at the latency gap on
    SSD-backed nodes, not a measured number. The math only requires this
    to be at least 2 — any smaller and a full-miss request at the hard
    cap wouldn't trip the drop threshold. 8 leaves headroom: if
    `kDropThreshold` goes up or `kHardMaxReplyNodes` comes down, the
    drop-on-attack property still holds without a code change. */
static constexpr auto kCostPerLookupMiss = 8;

/** Size-band surcharges. Whichever band a request's size falls into,
    its surcharge is added once on top of the per-lookup cost.

    The job of the surcharge is to make crossing a band edge feel like
    a step, not a slope. With these values, the cost roughly doubles or triples at each cliff:

        n=64: costs 48 => n=65 costs 149 (~3x jump)
        n=1024: costs 1108 => n=1025 costs 2009 (~2x jump)

    The 10x step between medium and large mirrors the ~16x step
    between the band edges (64 -> 1024) so the cliff feels comparable
    at both scales.
 */
static constexpr auto kCostBandSmall = 0;
static constexpr auto kCostBandMedium = 100;
static constexpr auto kCostBandLarge = 1000;

/** How many hashes per type an honest peer asks for at a time.

    Matches the `4` passed to `neededStateHashes(4)` and
    `neededTxHashes(4)` in `InboundLedger::getNeededHashes()`. Kept here
    instead of imported from the ledger module so overlay stays
    self-contained; if that `4` ever changes, update this in lockstep or
    the band thresholds below will start charging honest peers. */
static constexpr auto kLegitHashesPerType = 4;

/** Cutoffs that decide which size band a request falls into.

    A SHAMap inner node has 16 children; an honest peer asks for 4
    hashes per type. So:

      kBandSmallMax  = 4 * 16    =   64  // one inner node's worth
      kBandMediumMax = 4 * 16^2  = 1024  // a depth-2 subtree's worth

    A request up to 64 objects is small (no surcharge); up to 1024 is
    medium; anything larger is large. The bounds are inclusive: a
    request of exactly 64 is small, 65 is medium. Anything past 1024 is
    well beyond what the honest sync path produces, so it's billed at
    the large rate to drive attack-shaped traffic over the drop
    threshold quickly. */
static constexpr auto kBandSmallMax = kLegitHashesPerType * SHAMapInnerNode::kBranchFactor;
static constexpr auto kBandMediumMax = kBandSmallMax * SHAMapInnerNode::kBranchFactor;

}  // namespace xrpl::Tuning
