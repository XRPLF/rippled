#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/hardened_hash.h>

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace xrpl {

/** An inner-node position in a SHAMap that needs hash recomputation.

    Plan 7's deferred-rebuild algorithm walks bottom-up, recomputing
    each affected inner node's hash from its children. `AffectedNode`
    identifies one such node by:
      * depth:  0 = root, 1..63 = inner nodes, 64 = leaf (not included
                in the plan output — only inner nodes are rebuilt)
      * prefix: the leaf-key's first `depth*4` bits, with the remainder
                zeroed. Two nodes at the same depth with the same
                prefix are the same node.
*/
struct AffectedNode
{
    int depth;
    uint256 prefix;

    [[nodiscard]] bool
    operator==(AffectedNode const& other) const noexcept
    {
        return depth == other.depth && prefix == other.prefix;
    }
};

/** Plan which inner nodes need rebuild for a given set of leaf changes.

    Returns the union of ancestor paths of all `modifiedKeys`, sorted
    by depth descending so a bottom-up rebuild can iterate the result
    and find each level's nodes before the level above.

    The returned plan contains only INNER nodes (depths 0..63). The
    leaves themselves are at depth 64 and are not in the plan — they
    are the modifications, not nodes to be rebuilt.

    Complexity: O(K * 64) where K is `modifiedKeys.size()`. For real
    workloads (~thousands of modifications), this is microseconds.
*/
[[nodiscard]] std::vector<AffectedNode>
planDeferredRebuild(std::vector<uint256> const& modifiedKeys);

/** Compute the hash of a SHAMap inner node from its 16 children.

    Byte-identical to `SHAMapInnerNode::updateHash()`:

        sha512_half(
            HashPrefix::InnerNode (4 bytes, big-endian) ||
            child[0]  (32 bytes) ||
            child[1]  (32 bytes) ||
            ...
            child[15] (32 bytes))

    This is the elementary operation of plan-7's bottom-up rebuild:
    given the (already-computed) 16 child hashes of an inner node,
    produce that node's hash. Empty branches are passed as zero
    uint256 — the same convention SHAMap uses.

    Pure function; no SHAMap state, no allocation beyond a stack buffer.
*/
[[nodiscard]] uint256
computeInnerNodeHash(std::array<uint256, 16> const& childHashes);

/** Hash combiner for AffectedNode keys in unordered_map. */
struct AffectedNodeHash
{
    [[nodiscard]] std::size_t
    operator()(AffectedNode const& n) const noexcept
    {
        // Mix depth into the high bits of a hash of prefix.
        std::size_t h = 0;
        for (auto b : n.prefix)
            h = h * 31 + b;
        return h ^ (static_cast<std::size_t>(n.depth) << 56);
    }
};

/** Map of recomputed inner-node hashes keyed by (depth, prefix). */
using RebuildResult =
    std::unordered_map<AffectedNode, uint256, AffectedNodeHash>;

/** Walk a depth-descending plan and compute each affected node's new hash.

    For each AffectedNode in the plan (deepest first), collect its 16
    child hashes:
      * If a child position is itself in the plan (and already
        computed, since we walk deepest-first), use the computed hash.
      * Otherwise, fall back to `getOriginalChildHash` — the callback
        is expected to walk the parent SHAMap to find the hash at that
        position.

    Then `computeInnerNodeHash` over the 16 children yields the
    affected node's new hash. The result map contains every entry in
    the plan keyed by its (depth, prefix).

    @param plan
        Depth-descending plan from `planDeferredRebuild`.
    @param getOriginalChildHash
        Callable `uint256(int depth, uint256 const& prefix)` returning
        the hash at the given position in the parent SHAMap. May
        return uint256{} for absent positions.

    @note Pure function; safe to call concurrently with disjoint plans.
*/
template <typename GetChildHashFn>
[[nodiscard]] RebuildResult
executeRebuildPlan(
    std::vector<AffectedNode> const& plan,
    GetChildHashFn getOriginalChildHash);

namespace detail {

// Forwarder for template instantiation; declared here, defined in .cpp.
[[nodiscard]] RebuildResult
executeRebuildPlanImpl(
    std::vector<AffectedNode> const& plan,
    std::function<uint256(int, uint256 const&)> getOriginalChildHash);

}  // namespace detail

template <typename GetChildHashFn>
[[nodiscard]] RebuildResult
executeRebuildPlan(
    std::vector<AffectedNode> const& plan,
    GetChildHashFn getOriginalChildHash)
{
    return detail::executeRebuildPlanImpl(
        plan,
        std::function<uint256(int, uint256 const&)>(
            std::move(getOriginalChildHash)));
}

/** End-to-end deferred rebuild: produce the new SHAMap root hash.

    Combines `planDeferredRebuild` + `executeRebuildPlan` into one call.
    This is the consumer-facing API — the integration site only needs
    to supply the set of modified keys and a callback that reads the
    parent SHAMap. No AffectedNode plumbing is exposed.

    @param modifiedKeys
        Keys whose leaves have been added/replaced/deleted in this
        ledger close. Empty → no rebuild; returns the existing root
        via the callback at (depth=0, prefix=zero).
    @param getOriginalChildHash
        Callable `uint256(int depth, uint256 const& prefix)` returning
        the hash at the given position in the parent SHAMap. May return
        uint256{} for absent positions.
    @return The new SHAMap root hash.
*/
template <typename GetChildHashFn>
[[nodiscard]] uint256
deferredRebuildRoot(
    std::vector<uint256> const& modifiedKeys,
    GetChildHashFn getOriginalChildHash)
{
    if (modifiedKeys.empty())
        return getOriginalChildHash(0, uint256{});

    auto const plan = planDeferredRebuild(modifiedKeys);
    auto const result = executeRebuildPlan(plan, std::move(getOriginalChildHash));
    AffectedNode const rootKey{0, uint256{}};
    auto const it = result.find(rootKey);
    if (it == result.end())
        return uint256{};
    return it->second;
}

/** Partition modified keys by their first nibble (0..15).

    At depth 1 the root has 16 child subtrees, one per first-nibble
    value. Keys in different subtrees rebuild independently, so this
    partition is the basis for plan-7 P7.3's parallel-by-subtree
    rebuild.

    Returns 16 buckets, one per first-nibble value, each containing
    only the keys whose first nibble matches the bucket index.
*/
[[nodiscard]] std::array<std::vector<uint256>, 16>
partitionByFirstNibble(std::vector<uint256> const& modifiedKeys);

namespace detail {

[[nodiscard]] uint256
deferredRebuildRootParallelImpl(
    std::vector<uint256> const& modifiedKeys,
    std::function<uint256(int, uint256 const&)> getOriginalChildHash);

}  // namespace detail

/** Parallel deferred rebuild: partition by first nibble, rebuild each
    of the 16 subtrees in parallel, combine into the root.

    Equivalent to `deferredRebuildRoot` in output; differs only in
    execution strategy. Useful when the parent SHAMap is large enough
    that the rebuild cost matters per-close. For small workloads
    (handful of modifications), the serial path is faster — threading
    overhead exceeds the work saved.

    @param modifiedKeys
        Keys whose leaves have been added/replaced/deleted.
    @param getOriginalChildHash
        Thread-safe callable; will be invoked concurrently from
        multiple subtree workers.
    @return The new SHAMap root hash, byte-identical to
            `deferredRebuildRoot` over the same inputs.
*/
template <typename GetChildHashFn>
[[nodiscard]] uint256
deferredRebuildRootParallel(
    std::vector<uint256> const& modifiedKeys,
    GetChildHashFn getOriginalChildHash)
{
    return detail::deferredRebuildRootParallelImpl(
        modifiedKeys,
        std::function<uint256(int, uint256 const&)>(
            std::move(getOriginalChildHash)));
}

}  // namespace xrpl
