#pragma once

#include <xrpl/basics/KeyCache.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/partitioned_unordered_map.h>
#include <xrpl/beast/hash/hash_append.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace xrpl {

/**
 * Names a subtree whose descendants are all resident, by hash and by position.
 *
 * A node's hash covers its children but neither its depth nor its path, and one
 * cache answers for every map built from a NodeFamily, so the position is what
 * makes a hit answer for the position asked about and no other. The depth is
 * kept beside the id because SHAMapNodeID masks its id to its own depth, so an
 * all-zero id belongs to the root and to every all-zero prefix alike.
 *
 * Held as two fields rather than as a SHAMapNodeID, which is a CountedObject:
 * one per entry would report the cache's population as live node IDs in the
 * get_counts RPC, and would put an atomic increment on every temporary key a
 * lookup builds.
 */
struct FullBelowKey
{
    /**
     * The hash of the subtree.
     */
    UInt256 hash;

    /**
     * The masked id of the node the subtree was completed at.
     */
    UInt256 position;

    /**
     * The depth of that node.
     */
    std::uint32_t depth{0};

    /**
     * @param subtreeHash The hash of the subtree whose descendants are all
     *        resident.
     * @param nodeID The node the subtree was completed at.
     */
    FullBelowKey(UInt256 const& subtreeHash, SHAMapNodeID const& nodeID)
        : hash(subtreeHash)
        , position(nodeID.getNodeID())
        , depth(static_cast<std::uint32_t>(nodeID.getDepth()))
    {
    }

    /**
     * The same key, named by the node above the position and the branch taken.
     *
     * Produces the fields the constructor above would for that child's own node
     * ID, so the two forms name one entry.
     *
     * @param subtreeHash The hash of the subtree that is fully resident.
     * @param parentID The node above the one the subtree was completed at.
     * @param branch The branch of that node leading to it.
     */
    FullBelowKey(UInt256 const& subtreeHash, SHAMapNodeID const& parentID, unsigned int branch)
        : hash(subtreeHash)
        , position(childNodeID(parentID.getNodeID(), parentID.getDepth(), branch))
        , depth(static_cast<std::uint32_t>(parentID.getDepth() + 1))
    {
    }

    [[nodiscard]] bool
    operator==(FullBelowKey const& other) const = default;
};

// The per-entry cost of this cache is computed from these sizes, so pin the layout that arithmetic
// assumes: the three fields sit back to back with no padding.
static_assert(
    sizeof(FullBelowKey) == (2 * sizeof(UInt256)) + sizeof(std::uint32_t),
    "FullBelowKey must stay free of padding");
static_assert(
    alignof(FullBelowKey) == alignof(std::uint32_t),
    "FullBelowKey's lack of padding rests on its alignment");

/**
 * Feed a key to a hasher, field by field.
 *
 * Written out rather than hashed in one call over the object's bytes, since
 * beast::IsUniquelyRepresented is opt-in per type and has no case for a
 * user-defined struct, whatever its layout.
 *
 * @param h The hasher to feed.
 * @param key The key to hash.
 */
template <class Hasher>
void
hash_append(Hasher& h, FullBelowKey const& key) noexcept
{
    using beast::hash_append;
    hash_append(h, key.hash, key.position, key.depth);
}

/**
 * Choose which partition of the cache's map a key lives in.
 *
 * Taken from the subtree hash, which is already uniformly distributed.
 *
 * @param key The key to place.
 * @return The value PartitionedUnorderedMap reduces by its partition count.
 */
template <>
inline std::size_t
extract(FullBelowKey const& key)
{
    return extract(key.hash);
}

namespace detail {

/**
 * Remembers which tree keys have all descendants resident.
 * This optimizes the process of acquiring a complete tree.
 */
class BasicFullBelowCache
{
private:
    using CacheType = KeyCache<FullBelowKey>;

public:
    static constexpr auto kDefaultCacheTargetSize = 0;

    using key_type = FullBelowKey;
    using ClockType = CacheType::ClockType;

    /**
     * Construct the cache.
     *
     * @param name A label for diagnostics and stats reporting.
     * @param collector The collector to use for reporting stats.
     * @param targetSize The cache target size.
     * @param targetExpirationSeconds The expiration time for items.
     */
    BasicFullBelowCache(
        std::string const& name,
        ClockType& clock,
        beast::Journal j,
        beast::insight::Collector::Ptr const& collector = beast::insight::NullCollector::make(),
        std::size_t targetSize = kDefaultCacheTargetSize,
        std::chrono::seconds expiration = std::chrono::minutes{2})
        : cache_(name, targetSize, expiration, clock, j, collector), gen_(1)
    {
    }

    /**
     * Return the clock associated with the cache.
     */
    ClockType&
    clock()
    {
        return cache_.clock();
    }

    /**
     * Return the number of elements in the cache.
     * Thread safety:
     *     Safe to call from any thread.
     */
    std::size_t
    size() const
    {
        return cache_.size();
    }

    /**
     * Remove expired cache items.
     * Thread safety:
     *     Safe to call from any thread.
     */
    void
    sweep()
    {
        cache_.sweep();
    }

    /**
     * Refresh the last access time of an item, if it exists.
     * Thread safety:
     *     Safe to call from any thread.
     * @param hash The hash of the subtree to ask about.
     * @param parentID The node above the position to ask about it at.
     * @param branch The branch of that node leading to that position.
     * @return `true` If that subtree is recorded as resident at that position.
     */
    bool
    touchIfExists(UInt256 const& hash, SHAMapNodeID const& parentID, unsigned int branch)
    {
        return cache_.touchIfExists(key_type{hash, parentID, branch});
    }

    /**
     * Insert a key into the cache.
     * If the key already exists, the last access time will still
     * be refreshed.
     * Thread safety:
     *     Safe to call from any thread.
     * @param hash The hash of the subtree whose descendants are all resident.
     * @param nodeID The node that subtree was completed at.
     */
    void
    insert(UInt256 const& hash, SHAMapNodeID const& nodeID)
    {
        cache_.insert(key_type{hash, nodeID});
    }

    /**
     * generation determines whether cached entry is valid
     */
    std::uint32_t
    getGeneration() const
    {
        return gen_;
    }

    void
    clear()
    {
        cache_.clear();
        ++gen_;
    }

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

using FullBelowCache = detail::BasicFullBelowCache;

}  // namespace xrpl
