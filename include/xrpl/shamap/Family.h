#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/shamap/FullBelowCache.h>
#include <xrpl/shamap/TreeNodeCache.h>

#include <cstdint>

namespace xrpl {

/** Abstract collaborator bundle for SHAMap I/O, caching, and recovery.
 *
 *  Every `SHAMap` holds a `Family&` reference and routes all storage and
 *  caching decisions through it. The interface bundles four external
 *  collaborators — a persistent `NodeStore::Database`, two in-memory caches,
 *  and a logging journal — into a single coherent dependency with a shared
 *  lifetime, ensuring they are always consistent with each other.
 *
 *  The abstraction decouples the pure Merkle-radix-tree logic from
 *  application infrastructure, allowing different implementations for
 *  production (`NodeFamily`) and unit tests (lighter-weight in-memory
 *  variants).
 *
 *  @note `Family` instances are non-copyable and non-movable by design.
 *      `SHAMap` stores a `Family&` reference (not a pointer), and multiple
 *      maps may share the same `Family`. Allowing move would dangle those
 *      references. Stable addresses are guaranteed for the object's full
 *      lifetime.
 *
 *  @see NodeFamily
 */
class Family
{
public:
    Family(Family const&) = delete;
    Family(Family&&) = delete;

    Family&
    operator=(Family const&) = delete;

    Family&
    operator=(Family&&) = delete;

    explicit Family() = default;
    virtual ~Family() = default;

    /** Return the persistent node-store database backing this family. */
    virtual NodeStore::Database&
    db() = 0;

    /** Return the persistent node-store database backing this family. */
    [[nodiscard]] virtual NodeStore::Database const&
    db() const = 0;

    /** Return the journal used for diagnostic logging within this family. */
    virtual beast::Journal const&
    journal() = 0;

    /** Return the FullBelowCache for this family.
     *
     *  An entry in this cache means every descendant of the keyed tree node
     *  is already stored locally. During traversal or sync, a cache hit on a
     *  node's hash allows the entire subtree beneath it to be skipped.
     *
     *  The cache is generation-stamped: `clear()` increments the generation,
     *  invalidating all in-memory per-node markers without a per-entry purge.
     *  `reset()` additionally resets the generation back to 1 for full
     *  rebuild scenarios.
     */
    virtual std::shared_ptr<FullBelowCache>
    getFullBelowCache() = 0;

    /** Return the TreeNodeCache for this family.
     *
     *  Holds deserialized `SHAMapTreeNode` objects keyed by hash. Nodes
     *  read from the `NodeStore::Database` are placed here after
     *  deserialization; subsequent lookups by the same hash retrieve the
     *  already-decoded object, avoiding redundant disk reads.
     *
     *  Uses `SharedWeakUnionPtr` internally so that nodes can be evicted
     *  from the cache while live `SHAMap` trees that hold strong pointers
     *  to them continue to operate normally.
     */
    virtual std::shared_ptr<TreeNodeCache>
    getTreeNodeCache() = 0;

    /** Expire stale entries from both caches.
     *
     *  Called periodically by the application's maintenance loop to
     *  prevent unbounded memory growth. This is a routine background
     *  operation and does not invalidate the cache generation.
     */
    virtual void
    sweep() = 0;

    /** Trigger peer acquisition of a ledger identified by sequence number.
     *
     *  Called when a tree traversal reaches a node hash that is absent from
     *  both the cache and the local database, signalling an incomplete
     *  ledger. Implementations should forward to the inbound-ledger
     *  acquisition pipeline.
     *
     *  @note Implementations typically maintain a high-water `maxSeq_`
     *      under a mutex to suppress redundant acquisition requests when
     *      many concurrent SHAMap operations discover the same missing node.
     *
     *  @param refNum Sequence number of the ledger that owns the missing node.
     *  @param nodeHash Hash of the missing node, used for diagnostic logging.
     */
    virtual void
    missingNodeAcquireBySeq(std::uint32_t refNum, uint256 const& nodeHash) = 0;

    /** Trigger peer acquisition of a ledger identified by its hash.
     *
     *  Alternative to `missingNodeAcquireBySeq` for callers — typically
     *  sync flows — that have the ledger hash but not the sequence number
     *  as the primary identifier.
     *
     *  @param refHash Hash of the ledger that owns the missing node.
     *  @param refNum  Sequence number of the same ledger, for logging and
     *      deduplication.
     */
    virtual void
    missingNodeAcquireByHash(uint256 const& refHash, std::uint32_t refNum) = 0;

    /** Tear down all cache state and reset the FullBelowCache generation.
     *
     *  Used when the family's data is being rebuilt from scratch (e.g.,
     *  after a database wipe or during ledger-replaying scenarios). More
     *  destructive than `sweep()`: all cached data is discarded and the
     *  FullBelowCache generation is reset to 1, invalidating every
     *  in-memory per-node marker that references a prior generation.
     */
    virtual void
    reset() = 0;
};

}  // namespace xrpl
