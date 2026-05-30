#include <xrpl/ledger/DeferredRebuild.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// Zero the trailing (64 - depth) nibbles of `key`, keeping the first
// `depth` nibbles. Returns the resulting prefix uint256.
//
// SHAMap stores keys big-endian. The most significant nibble of the
// key is the depth-1 branch from root. So at depth N we want to keep
// the top N nibbles = N*4 bits and zero the rest.
[[nodiscard]] uint256
prefixAtDepth(uint256 const& key, int depth) noexcept
{
    if (depth >= 64)
        return key;
    if (depth <= 0)
        return uint256{};

    uint256 result = key;
    auto const totalNibbles = 64;
    auto const nibblesToZero = totalNibbles - depth;

    // Zero `nibblesToZero` nibbles starting from the least-significant
    // end. uint256 has 32 bytes (each holds two nibbles, high then low).
    // Byte index 31 holds the two lowest nibbles; byte 0 holds the two
    // highest.
    int nibblesZeroed = 0;
    for (int byteIdx = uint256::kBytes - 1;
         byteIdx >= 0 && nibblesZeroed < nibblesToZero;
         --byteIdx)
    {
        if (nibblesZeroed + 2 <= nibblesToZero)
        {
            // Zero both nibbles in this byte
            result.data()[byteIdx] = 0;
            nibblesZeroed += 2;
        }
        else
        {
            // Zero only the low nibble in this byte
            result.data()[byteIdx] &= 0xF0;
            nibblesZeroed += 1;
        }
    }
    return result;
}

}  // namespace

namespace {

// Longest common prefix in NIBBLES between two uint256 keys.
// Returns 0..64. 64 means the keys are identical.
[[nodiscard]] int
lcpNibbles(uint256 const& a, uint256 const& b) noexcept
{
    for (int byteIdx = 0; byteIdx < uint256::kBytes; ++byteIdx)
    {
        if (a.data()[byteIdx] != b.data()[byteIdx])
        {
            // They agree in 2*byteIdx whole nibbles. Check whether
            // the high nibble of this byte also agrees.
            std::uint8_t aHigh = a.data()[byteIdx] >> 4;
            std::uint8_t bHigh = b.data()[byteIdx] >> 4;
            if (aHigh == bHigh)
                return 2 * byteIdx + 1;
            return 2 * byteIdx;
        }
    }
    return 64;
}

}  // namespace

std::vector<AffectedNode>
planDeferredRebuild(std::vector<uint256> const& modifiedKeys)
{
    if (modifiedKeys.empty())
        return {};

    // LCP-based dedup. For sorted keys, the inner-node ancestors are
    // grouped by shared prefix; once a key has contributed its
    // ancestors at depths 0..63, the NEXT sorted key only adds NEW
    // ancestors at depths > LCP(prev, current). Everything at depth
    // ≤ LCP is already in the plan from `prev`.
    //
    // This replaces O(K × 64) prefix computations with O(K log K)
    // sort + ~O(K) for sequential-key workloads (where LCP ≈ 63).
    std::vector<uint256> sorted = modifiedKeys;
    std::sort(sorted.begin(), sorted.end());

    std::vector<AffectedNode> plan;
    // Upper bound for arbitrary inputs is K * 64, but realistic
    // workloads (sequential / clustered) produce ~K entries.
    plan.reserve(sorted.size() * 2);

    // First key contributes ancestors at every depth.
    for (int d = 0; d <= 63; ++d)
        plan.push_back({d, prefixAtDepth(sorted[0], d)});

    // Subsequent keys contribute only ancestors at depths > LCP with
    // their predecessor.
    for (std::size_t i = 1; i < sorted.size(); ++i)
    {
        // Identical adjacent keys: nothing new to contribute.
        if (sorted[i] == sorted[i - 1])
            continue;
        int const lcp = lcpNibbles(sorted[i - 1], sorted[i]);
        for (int d = lcp + 1; d <= 63; ++d)
            plan.push_back({d, prefixAtDepth(sorted[i], d)});
    }

    // Sort depth-descending so a bottom-up rebuild can iterate in
    // order. Within a depth, ascending prefix for determinism.
    std::sort(
        plan.begin(),
        plan.end(),
        [](AffectedNode const& a, AffectedNode const& b) {
            if (a.depth != b.depth)
                return a.depth > b.depth;
            return a.prefix < b.prefix;
        });

    return plan;
}

namespace {

// Given a parent inner node at (parentDepth, parentPrefix), compute the
// child prefix at the given branch (0..15). The child is at depth
// (parentDepth + 1); its prefix sets the nibble at position parentDepth
// to the branch value.
[[nodiscard]] uint256
childPrefixOf(uint256 const& parentPrefix, int parentDepth, std::uint8_t branch)
{
    uint256 result = parentPrefix;
    int const byteIdx = parentDepth / 2;
    bool const isHighNibble = (parentDepth % 2) == 0;
    if (isHighNibble)
        result.data()[byteIdx] =
            (result.data()[byteIdx] & 0x0F) |
            static_cast<std::uint8_t>((branch & 0x0F) << 4);
    else
        result.data()[byteIdx] =
            (result.data()[byteIdx] & 0xF0) |
            static_cast<std::uint8_t>(branch & 0x0F);
    return result;
}

}  // namespace

namespace detail {

RebuildResult
executeRebuildPlanImpl(
    std::vector<AffectedNode> const& plan,
    std::function<uint256(int, uint256 const&)> getOriginalChildHash)
{
    RebuildResult result;
    result.reserve(plan.size());

    // Plan is depth-descending; deepest nodes processed first. Their
    // hashes are visible to shallower nodes in this same walk.
    for (auto const& node : plan)
    {
        std::array<uint256, 16> children;
        for (std::uint8_t b = 0; b < 16; ++b)
        {
            AffectedNode const childPos{
                node.depth + 1, childPrefixOf(node.prefix, node.depth, b)};
            auto it = result.find(childPos);
            if (it != result.end())
                children[b] = it->second;
            else
                children[b] = getOriginalChildHash(childPos.depth, childPos.prefix);
        }
        result.emplace(node, computeInnerNodeHash(children));
    }

    return result;
}

}  // namespace detail

std::array<std::vector<uint256>, 16>
partitionByFirstNibble(std::vector<uint256> const& modifiedKeys)
{
    std::array<std::vector<uint256>, 16> buckets;
    for (auto const& key : modifiedKeys)
    {
        // First nibble = high nibble of byte 0
        std::uint8_t const firstNibble = (key.data()[0] >> 4) & 0x0F;
        buckets[firstNibble].push_back(key);
    }
    return buckets;
}

namespace detail {

uint256
deferredRebuildRootParallelImpl(
    std::vector<uint256> const& modifiedKeys,
    std::function<uint256(int, uint256 const&)> getOriginalChildHash)
{
    if (modifiedKeys.empty())
        return getOriginalChildHash(0, uint256{});

    auto const buckets = partitionByFirstNibble(modifiedKeys);

    // For each subtree, compute the new depth-1 hash (if any keys
    // changed in that subtree) in parallel. Empty subtrees fall back
    // to the parent's depth-1 hash, which is read from the callback.
    //
    // Each future captures the relevant bucket and runs an independent
    // plan-and-execute over just that subtree's keys. The result is
    // the new hash of the depth-1 inner node rooting that subtree
    // (which corresponds to the root's branch-b child).
    std::array<std::future<uint256>, 16> futures;
    for (std::uint8_t b = 0; b < 16; ++b)
    {
        if (buckets[b].empty())
            continue;

        futures[b] = std::async(
            std::launch::async,
            [bucket = buckets[b], &getOriginalChildHash, b]() -> uint256 {
                // Plan + execute for this subtree's keys. The depth-1
                // node is the rooting node; we want its new hash.
                auto const plan = planDeferredRebuild(bucket);
                auto const result =
                    executeRebuildPlan(plan, getOriginalChildHash);

                // The depth-1 prefix for this subtree has its first
                // nibble set to b, rest zero.
                uint256 prefix{};
                prefix.data()[0] =
                    static_cast<std::uint8_t>(b << 4);

                AffectedNode const subtreeRoot{1, prefix};
                auto const it = result.find(subtreeRoot);
                if (it == result.end())
                    return uint256{};
                return it->second;
            });
    }

    // Gather: 16 child hashes for the root.
    std::array<uint256, 16> rootChildren;
    for (std::uint8_t b = 0; b < 16; ++b)
    {
        if (buckets[b].empty())
        {
            // Untouched subtree — read original depth-1 hash from
            // parent SHAMap via callback.
            uint256 prefix{};
            prefix.data()[0] = static_cast<std::uint8_t>(b << 4);
            rootChildren[b] = getOriginalChildHash(1, prefix);
        }
        else
        {
            rootChildren[b] = futures[b].get();
        }
    }

    return computeInnerNodeHash(rootChildren);
}

}  // namespace detail

uint256
computeInnerNodeHash(std::array<uint256, 16> const& childHashes)
{
    // SHAMapInnerNode::updateHash short-circuits to a zero hash when
    // every branch is empty (isBranch_ == 0). Match that convention —
    // a "node with no children" hashes to zero, not to SHA-512 of a
    // zero-filled buffer.
    bool anyNonZero = false;
    for (auto const& h : childHashes)
    {
        if (h.isNonZero())
        {
            anyNonZero = true;
            break;
        }
    }
    if (!anyNonZero)
        return uint256{};

    // Layout: 4-byte big-endian HashPrefix::InnerNode || 16 × 32-byte
    // child hashes. Total: 516 bytes. Matches SHAMapInnerNode::updateHash
    // byte-for-byte; differential-tested against it.
    constexpr std::size_t kBufSize = 4 + 16 * uint256::kBytes;
    alignas(64) std::array<std::uint8_t, kBufSize> buf{};

    auto const prefix = static_cast<std::uint32_t>(HashPrefix::InnerNode);
    buf[0] = static_cast<std::uint8_t>(prefix >> 24);
    buf[1] = static_cast<std::uint8_t>(prefix >> 16);
    buf[2] = static_cast<std::uint8_t>(prefix >> 8);
    buf[3] = static_cast<std::uint8_t>(prefix);

    std::uint8_t* out = buf.data() + 4;
    for (auto const& h : childHashes)
    {
        std::memcpy(out, h.data(), uint256::kBytes);
        out += uint256::kBytes;
    }

    return sha512Half(Slice{buf.data(), buf.size()});
}

}  // namespace xrpl
