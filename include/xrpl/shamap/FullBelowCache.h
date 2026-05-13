/** @file
 *  Subtree-completeness cache for SHAMap synchronization.
 *
 *  During ledger acquisition, traversing a SHAMap to discover absent nodes
 *  is expensive: every inner node may require 16 child lookups, each of which
 *  can hit the database. `BasicFullBelowCache` short-circuits this walk by
 *  recording the hashes of inner nodes whose entire subtree is confirmed
 *  present in local storage.
 */

#pragma once

#include <xrpl/basics/KeyCache.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>

#include <atomic>
#include <string>

namespace xrpl {

namespace detail {

/** Remembers which SHAMap inner-node hashes have all descendants resident.
 *
 *  Once a subtree rooted at an inner node is confirmed complete, the node's
 *  hash is inserted here. Future sync traversals call `touchIfExists` before
 *  descending; a hit skips the entire subtree without further DB lookups.
 *
 *  A two-layer scheme avoids redundant work at different granularities:
 *  - **This cache** (inter-pass, cross-SHAMap): entries survive across
 *    multiple `getMissingNodes` calls and are shared among all SHAMaps in
 *    the same `Family`.
 *  - **Per-node `fullBelowGen_`** (intra-pass): `SHAMapInnerNode` stores the
 *    generation at which it was marked complete; `isFullBelow(gen)` returns
 *    true without a cache lookup when the generation still matches.
 *
 *  Entries expire automatically after the configured duration (default two
 *  minutes), handling the case where a previously-complete subtree is later
 *  invalidated by database eviction.
 *
 *  All public methods are thread-safe; thread-safety is inherited from the
 *  underlying `KeyCache` (`TaggedCache<uint256, int, true>`). Only `backed_`
 *  SHAMaps (those integrated with a `NodeStore`) consult this cache; in-memory
 *  maps bypass it.
 *
 *  @see SHAMapInnerNode::isFullBelow
 *  @see SHAMapInnerNode::setFullBelowGen
 *  @see Family::getFullBelowCache
 */
class BasicFullBelowCache
{
private:
    using CacheType = KeyCache;

public:
    /** Target size passed to the constructor to request an unbounded cache. */
    static constexpr auto kDEFAULT_CACHE_TARGET_SIZE = 0;

    using key_type = uint256;
    using clock_type = typename CacheType::clock_type;

    /** Construct the cache.
     *
     *  The generation counter is initialised to 1. All `SHAMapInnerNode`
     *  instances start with `fullBelowGen_ = 0`, so `isFullBelow(1)` returns
     *  false for every node until explicitly marked.
     *
     *  @param name A label used in diagnostics and stats reporting.
     *  @param clock The clock used for entry expiration.
     *  @param j Journal for internal cache logging.
     *  @param collector Collector for stats export; defaults to a no-op
     *      collector when not provided.
     *  @param targetSize Maximum number of entries to retain; 0 means
     *      unbounded (see `kDEFAULT_CACHE_TARGET_SIZE`).
     *  @param expiration Duration after which an entry is eligible for
     *      eviction by `sweep()`; defaults to two minutes.
     */
    BasicFullBelowCache(
        std::string const& name,
        clock_type& clock,
        beast::Journal j,
        beast::insight::Collector::ptr const& collector = beast::insight::NullCollector::make(),
        std::size_t targetSize = kDEFAULT_CACHE_TARGET_SIZE,
        std::chrono::seconds expiration = std::chrono::minutes{2})
        : cache_(name, targetSize, expiration, clock, j, collector), gen_(1)
    {
    }

    /** Return the clock associated with the cache. */
    clock_type&
    clock()
    {
        return cache_.clock();
    }

    /** Return the number of entries currently held in the cache.
     *
     *  @note Safe to call from any thread.
     */
    std::size_t
    size() const
    {
        return cache_.size();
    }

    /** Evict entries whose age exceeds the configured expiration duration.
     *
     *  Called periodically by `NodeFamily::sweep()` on the same housekeeping
     *  cycle as the tree-node cache sweep.
     *
     *  @note Safe to call from any thread.
     */
    void
    sweep()
    {
        cache_.sweep();
    }

    /** Test whether a hash is cached and, if so, reset its expiration timer.
     *
     *  This is the hot path in `SHAMap::getMissingNodes` and
     *  `SHAMap::addKnownNode`: before descending into a child subtree the
     *  caller checks the cache; a `true` return means the entire subtree is
     *  locally complete and can be skipped, returning
     *  `SHAMapAddNode::duplicate()`.
     *
     *  @param key Hash of the inner node whose subtree completeness is queried.
     *  @return `true` if the hash is present in the cache (subtree complete);
     *      `false` if absent or expired.
     *  @note Safe to call from any thread.
     */
    bool
    touchIfExists(key_type const& key)
    {
        return cache_.touchIfExists(key);
    }

    /** Record that the subtree rooted at `key` is fully present locally.
     *
     *  Called after a complete depth-first traversal of a subtree confirms no
     *  missing nodes. Subsequent calls to `touchIfExists` with the same hash
     *  will return `true` until the entry expires or `clear()`/`reset()` is
     *  called.
     *
     *  If the key is already present its expiration timer is refreshed.
     *
     *  @param key Hash of the inner node whose subtree has been verified
     *      complete.
     *  @note Safe to call from any thread.
     */
    void
    insert(key_type const& key)
    {
        cache_.insert(key);
    }

    /** Return the current generation counter.
     *
     *  The generation is threaded through a `getMissingNodes` traversal and
     *  compared against `SHAMapInnerNode::fullBelowGen_` via
     *  `isFullBelow(generation)`. A match means the node was marked complete
     *  during the current or a still-valid prior pass and its subtree can be
     *  skipped without a cache lookup.
     *
     *  The generation is incremented by `clear()` and reset to 1 by `reset()`,
     *  both of which globally invalidate all in-memory per-node markers at
     *  zero cost — no tree walk is needed to clear them.
     *
     *  @return The current generation value.
     *  @note Safe to call from any thread; `gen_` is `std::atomic`.
     */
    std::uint32_t
    getGeneration(void) const
    {
        return gen_;
    }

    /** Purge all cache entries and invalidate all in-memory per-node markers.
     *
     *  Increments the generation counter so that every `SHAMapInnerNode`
     *  whose `fullBelowGen_` was set to the old generation will return
     *  `false` from `isFullBelow` on the next traversal, forcing a fresh
     *  descent. No tree walk is required; the stale markers are invalidated
     *  implicitly by the generation mismatch.
     *
     *  Called by `NodeFamily::reset()` when the family is torn down and
     *  rebuilt (e.g., after missing-node recovery or between ledger replays).
     *
     *  @note Safe to call from any thread.
     *  @see reset()
     */
    void
    clear()
    {
        cache_.clear();
        ++gen_;
    }

    /** Purge all cache entries and reset the generation counter to 1.
     *
     *  Semantically equivalent to `clear()` but sets `m_gen = 1` instead of
     *  incrementing it. Used at initial construction (via the member
     *  initialiser) and on full application restart, where returning to a
     *  canonical baseline generation is preferable to retaining a growing
     *  counter.
     *
     *  Any `SHAMapInnerNode` carrying `fullBelowGen_ > 1` will not match the
     *  reset-to-1 state, correctly marking every in-memory marker stale.
     *  Those nodes are expected to be recreated fresh after a hard reset.
     *
     *  @note Safe to call from any thread.
     *  @see clear()
     */
    void
    reset()
    {
        cache_.clear();
        gen_ = 1;
    }

private:
    CacheType cache_;
    std::atomic<std::uint32_t> gen_;
};

}  // namespace detail

/** Thread-safe cache recording which SHAMap subtrees are fully present locally.
 *
 *  Public alias for `detail::BasicFullBelowCache`. Used throughout the codebase
 *  via `Family::getFullBelowCache()` to short-circuit expensive tree traversals
 *  during ledger synchronization.
 */
using FullBelowCache = detail::BasicFullBelowCache;

}  // namespace xrpl
