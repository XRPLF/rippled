// Tests for the Plan 7 deferred-SHAMap rebuild planning kernel.
//
// The full plan-7 algorithm (bottom-up parallel rebuild of a SHAMap
// from a parent SHAMap + delta) decomposes into two layers:
//
//   1. PLAN — given a set of modified leaf keys, compute which inner
//      nodes need their hash recomputed. Pure algorithm; no SHAMap.
//   2. EXECUTE — given the plan + a parent SHAMap + the delta, produce
//      the new SHAMap with byte-identical root hash.
//
// This file tests (1). Layer (2) requires SHAMap fixtures (Family,
// NodeStore) and lands in a follow-up.

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/DeferredRebuild.h>
#include <xrpl/shamap/SHAMapInnerNode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace xrpl;

namespace {

// Inhibit dead-code elimination in benchmark loops.
template <typename T>
inline void
benchmark_use(T const& v)
{
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "r,m"(v) : "memory");
#else
    (void)v;
#endif
}

[[nodiscard]] uint256
keyOf(std::uint64_t v)
{
    return uint256{v};
}

// Build a key whose first `nibblesIntoKey` nibbles match a given pattern,
// remaining nibbles set to 0. SHAMap stores keys big-endian; the most
// significant nibble of the key is the depth-1 branch from root.
[[nodiscard]] uint256
keyWithPrefix(std::vector<std::uint8_t> const& prefixNibbles)
{
    uint256 k;  // zero-initialised
    // Each byte of uint256 holds two nibbles, high nibble first.
    for (std::size_t i = 0; i < prefixNibbles.size(); ++i)
    {
        auto const byteIdx = i / 2;
        auto const isHighNibble = (i % 2) == 0;
        if (byteIdx >= uint256::kBytes)
            break;
        auto const shift = isHighNibble ? 4 : 0;
        k.data()[byteIdx] |= static_cast<std::uint8_t>(
            (prefixNibbles[i] & 0x0F) << shift);
    }
    return k;
}

}  // namespace

// ---------------------------------------------------------------------------
// Empty + single-key shape
// ---------------------------------------------------------------------------

TEST(DeferredRebuild_Plan, EmptyKeySetYieldsEmptyPlan)
{
    std::vector<uint256> keys;
    auto const plan = planDeferredRebuild(keys);
    EXPECT_TRUE(plan.empty());
}

TEST(DeferredRebuild_Plan, SingleKeyTouchesEveryDepth)
{
    // A single 256-bit key has 64 nibbles, so the path from root to
    // the leaf passes through 64 inner nodes (depth 1 through 64).
    // Plus the root itself at depth 0 — that gives 65 affected
    // ancestor positions.
    //
    // Actually the leaf at depth 64 is the SLE itself, not an inner
    // node. The inner nodes along the path are at depths 0 (root)
    // through 63. So 64 inner-node positions total.
    std::vector<uint256> keys{keyOf(1)};
    auto const plan = planDeferredRebuild(keys);
    EXPECT_EQ(plan.size(), 64u);
}

TEST(DeferredRebuild_Plan, PlanIsDepthDescending)
{
    // Bottom-up rebuild walks deepest nodes first. The plan must be
    // ordered so the consumer can iterate and find each level before
    // its parent.
    std::vector<uint256> keys{keyOf(1)};
    auto const plan = planDeferredRebuild(keys);

    for (std::size_t i = 1; i < plan.size(); ++i)
    {
        EXPECT_LE(plan[i].depth, plan[i - 1].depth)
            << "Plan not depth-descending at index " << i;
    }
    EXPECT_EQ(plan.front().depth, 63);  // deepest inner node
    EXPECT_EQ(plan.back().depth, 0);    // root
}

// ---------------------------------------------------------------------------
// Multiple keys — ancestor sharing
// ---------------------------------------------------------------------------

TEST(DeferredRebuild_Plan, DisjointKeysShareOnlyRoot)
{
    // Two keys that differ in their very first nibble share only one
    // ancestor: the root (depth 0). Each contributes 63 unique
    // ancestors at depths 1..63, plus the shared root.
    //
    // Total affected nodes: 63 + 63 + 1 = 127.
    auto const keyA = keyWithPrefix({0x0});  // first nibble = 0
    auto const keyB = keyWithPrefix({0xF});  // first nibble = 15

    auto const plan = planDeferredRebuild({keyA, keyB});
    EXPECT_EQ(plan.size(), 127u);
}

TEST(DeferredRebuild_Plan, KeysSharingPrefixShareAncestors)
{
    // Two keys that share their first 3 nibbles share their prefixes
    // at depths 0, 1, 2, AND 3 — at depth N the prefix is the first N
    // nibbles, so shared-first-3-nibbles means shared at depths 0..3.
    //
    // They diverge at depth 4 (the prefix at depth 4 includes the 4th
    // nibble, which differs). So each contributes unique ancestors at
    // depths 4..63 = 60 levels.
    //
    // Total: 4 shared (depths 0..3) + 60*2 unique = 124.
    auto const keyA = keyWithPrefix({0x1, 0x2, 0x3, 0x4});
    auto const keyB = keyWithPrefix({0x1, 0x2, 0x3, 0x5});

    auto const plan = planDeferredRebuild({keyA, keyB});
    EXPECT_EQ(plan.size(), 124u);
}

TEST(DeferredRebuild_Plan, IdenticalKeysCountedOnce)
{
    // Two identical keys produce the same plan as a single key — the
    // delta is "this key changed", duplicated or not.
    auto const k = keyOf(7);
    auto const plan = planDeferredRebuild({k, k});
    EXPECT_EQ(plan.size(), 64u);
}

// ---------------------------------------------------------------------------
// Correctness of node-identity
// ---------------------------------------------------------------------------

TEST(DeferredRebuild_Plan, NodesAtSameDepthHaveDistinctPrefixesWhenKeysDiffer)
{
    auto const keyA = keyWithPrefix({0x0});
    auto const keyB = keyWithPrefix({0xF});

    auto const plan = planDeferredRebuild({keyA, keyB});

    // At depth 1, the two keys yield distinct inner-node positions —
    // they branch at nibble 0 vs nibble F.
    int depth1NodeCount = 0;
    for (auto const& node : plan)
        if (node.depth == 1)
            ++depth1NodeCount;
    EXPECT_EQ(depth1NodeCount, 2);
}

TEST(DeferredRebuild_Plan, RootAlwaysPresent)
{
    // Every non-empty plan includes the root (depth 0).
    std::vector<uint256> keys{keyOf(1), keyOf(2), keyOf(3)};
    auto const plan = planDeferredRebuild(keys);

    auto const rootCount = std::count_if(
        plan.begin(),
        plan.end(),
        [](AffectedNode const& n) { return n.depth == 0; });
    EXPECT_EQ(rootCount, 1);
}

// ---------------------------------------------------------------------------
// Benchmarks. TDD with benchmarks: gate the plan-generation cost so a
// regression shows up as a failed test, not a mainnet incident.
//
// `planDeferredRebuild` runs at every ledger close to determine which
// inner nodes to recompute. At realistic mainnet workload (~3000 SLEs
// modified per ledger), this must be cheap — well under a millisecond.
// ---------------------------------------------------------------------------

TEST(DeferredRebuild_Bench, PlanGenerationAtLedgerScale)
{
    // 3000 modified keys ≈ realistic 1500-TPS-target ledger.
    constexpr std::size_t N = 3'000;
    std::vector<uint256> keys;
    keys.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
        keys.push_back(keyOf(i));

    auto const t0 = std::chrono::high_resolution_clock::now();
    auto const plan = planDeferredRebuild(keys);
    auto const elapsed = std::chrono::high_resolution_clock::now() - t0;
    auto const us =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

    std::printf(
        "  planDeferredRebuild N=%zu modified keys : %lld µs (plan size %zu)\n",
        N,
        static_cast<long long>(us),
        plan.size());

    // Post-LCP-optimization: measured ~1.5 ms locally. 15 ms is ~10×
    // measured — generous for slow CI but tight enough to catch real
    // regressions (e.g., accidental return to O(K*64) prefix ops).
    EXPECT_LT(us, 15'000)
        << "Plan generation at 3000 modified keys should be under 15 ms";
}

TEST(DeferredRebuild_Bench, PlanGenerationAtTenKKeys)
{
    // Stress-test at 10x typical to catch O(N²) regressions early.
    constexpr std::size_t N = 30'000;
    std::vector<uint256> keys;
    keys.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
        keys.push_back(keyOf(i));

    auto const t0 = std::chrono::high_resolution_clock::now();
    auto const plan = planDeferredRebuild(keys);
    auto const elapsed = std::chrono::high_resolution_clock::now() - t0;
    auto const us =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

    std::printf(
        "  planDeferredRebuild N=%zu modified keys : %lld µs (plan size %zu)\n",
        N,
        static_cast<long long>(us),
        plan.size());

    // Post-LCP: measured ~16 ms locally. 150 ms threshold = ~10×.
    EXPECT_LT(us, 150'000)
        << "Plan generation at 30k keys should be under 150 ms";
}

TEST(DeferredRebuild_Plan, NoDuplicateNodes)
{
    // A given inner node must appear at most once in the plan, even if
    // many leaves share it as an ancestor.
    std::vector<uint256> keys;
    for (std::uint64_t i = 0; i < 100; ++i)
        keys.push_back(keyOf(i));

    auto const plan = planDeferredRebuild(keys);

    for (std::size_t i = 0; i < plan.size(); ++i)
        for (std::size_t j = i + 1; j < plan.size(); ++j)
            EXPECT_FALSE(
                plan[i].depth == plan[j].depth &&
                plan[i].prefix == plan[j].prefix)
                << "Duplicate at indices " << i << " and " << j;
}

// ---------------------------------------------------------------------------
// Inner-node hash computation (P7.2.1).
//
// The bottom-up rebuild's elementary operation is: given the 16 child
// hashes of an inner node, compute the inner node's own hash. This is
// the pure function `computeInnerNodeHash` — the kernel of plan-7's
// "EXECUTE" layer.
//
// We test it against the production code path as oracle:
// `SHAMapInnerNode::makeFullInner` deserializes 16 hashes from a
// 512-byte slice and calls `updateHash()` to compute the node's hash.
// Our function must produce byte-identical output to that path — the
// safety property that makes plan-7 a pure internal optimization
// (no protocol change).
// ---------------------------------------------------------------------------

namespace {

// Pack 16 child hashes into the 512-byte buffer SHAMapInnerNode expects.
[[nodiscard]] std::vector<std::uint8_t>
packChildHashes(std::array<uint256, 16> const& children)
{
    std::vector<std::uint8_t> buf;
    buf.reserve(16 * 32);
    for (auto const& h : children)
        for (auto b : h)
            buf.push_back(b);
    return buf;
}

// Use SHAMapInnerNode's makeFullInner factory to produce the canonical
// hash. The returned tree node has updateHash() already invoked.
[[nodiscard]] uint256
oracleHash(std::array<uint256, 16> const& children)
{
    auto const buf = packChildHashes(children);
    auto node = SHAMapInnerNode::makeFullInner(
        Slice{buf.data(), buf.size()}, SHAMapHash{}, /*hashValid=*/false);
    return node->getHash().asUInt256();
}

}  // namespace

TEST(DeferredRebuild_InnerHash, AllZeroChildrenMatchesOracle)
{
    // An inner node with all-zero children is the same byte pattern
    // an empty branch produces; computing its hash via either path
    // must agree.
    std::array<uint256, 16> children{};  // all zero
    auto const oracle = oracleHash(children);
    auto const ours = computeInnerNodeHash(children);
    EXPECT_EQ(ours, oracle);
}

TEST(DeferredRebuild_InnerHash, SingleChildMatchesOracle)
{
    std::array<uint256, 16> children{};
    children[7] = uint256{0xDEADBEEF};
    auto const oracle = oracleHash(children);
    auto const ours = computeInnerNodeHash(children);
    EXPECT_EQ(ours, oracle);
}

TEST(DeferredRebuild_InnerHash, AllChildrenSetMatchesOracle)
{
    std::array<uint256, 16> children;
    for (std::size_t i = 0; i < 16; ++i)
        children[i] = uint256{0x100ULL + i};
    auto const oracle = oracleHash(children);
    auto const ours = computeInnerNodeHash(children);
    EXPECT_EQ(ours, oracle);
}

TEST(DeferredRebuild_InnerHash, SwappingChildrenChangesHash)
{
    // Order matters — branch position is part of the hash input.
    std::array<uint256, 16> a;
    for (std::size_t i = 0; i < 16; ++i)
        a[i] = uint256{0x200ULL + i};

    std::array<uint256, 16> b = a;
    std::swap(b[3], b[11]);

    EXPECT_NE(computeInnerNodeHash(a), computeInnerNodeHash(b));
    EXPECT_EQ(computeInnerNodeHash(a), oracleHash(a));
    EXPECT_EQ(computeInnerNodeHash(b), oracleHash(b));
}

TEST(DeferredRebuild_InnerHash, Deterministic)
{
    std::array<uint256, 16> children;
    for (std::size_t i = 0; i < 16; ++i)
        children[i] = uint256{0x300ULL + i * 0x1234};

    auto const h1 = computeInnerNodeHash(children);
    auto const h2 = computeInnerNodeHash(children);
    auto const h3 = computeInnerNodeHash(children);
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h2, h3);
}

TEST(DeferredRebuild_InnerHash, ChangingOneChildChangesHash)
{
    std::array<uint256, 16> base;
    for (std::size_t i = 0; i < 16; ++i)
        base[i] = uint256{0x400ULL + i};

    for (std::size_t branchToBump = 0; branchToBump < 16; ++branchToBump)
    {
        auto modified = base;
        modified[branchToBump] = uint256{0xCAFEBABEULL + branchToBump};
        EXPECT_NE(computeInnerNodeHash(base), computeInnerNodeHash(modified))
            << "Hash unchanged when modifying branch " << branchToBump;
    }
}

// ---------------------------------------------------------------------------
// Bottom-up plan execution (P7.2.2).
//
// Given a depth-descending plan and a callback that supplies "original"
// child hashes from the parent SHAMap, walk the plan, compute each
// affected node's new hash, and return them in a map. Hashes computed
// earlier in the walk (deeper nodes) are visible to later (shallower)
// nodes that have them as children.
//
// The pure-function design takes a callback rather than a SHAMap
// reference so the algorithm can be tested without SHAMap fixtures.
// Real integration will pass a callback that walks the actual SHAMap.
// ---------------------------------------------------------------------------

namespace {

// Given a parent inner node at (parentDepth, parentPrefix), compute the
// child prefix at the given branch (0..15). The child is at depth
// (parentDepth + 1) and its prefix has the nibble at position
// parentDepth set to the branch value.
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

TEST(DeferredRebuild_ChildPrefix, BranchZeroPreservesPrefix)
{
    auto const parent = keyWithPrefix({0xA, 0xB});
    // Setting nibble 2 to 0 — that's already the case
    auto const child = childPrefixOf(parent, 2, 0);
    EXPECT_EQ(child, parent);
}

TEST(DeferredRebuild_ChildPrefix, SetsCorrectNibblePosition)
{
    uint256 const empty{};
    // Parent at depth 0, branch 5 → nibble 0 of result = 5
    auto const child = childPrefixOf(empty, 0, 5);
    auto const expected = keyWithPrefix({5});
    EXPECT_EQ(child, expected);
}

TEST(DeferredRebuild_ChildPrefix, DepthOneSetsNibbleOne)
{
    auto const parent = keyWithPrefix({0xA});
    // Parent at depth 1 (first nibble set to A), branch 7
    // → child has nibbles (A, 7, 0, 0, ...)
    auto const child = childPrefixOf(parent, 1, 7);
    auto const expected = keyWithPrefix({0xA, 0x7});
    EXPECT_EQ(child, expected);
}

TEST(DeferredRebuild_ChildPrefix, DepthSixtyThreeIsLastNibble)
{
    uint256 parent{};
    for (int i = 0; i < uint256::kBytes; ++i)
        parent.data()[i] = 0xAB;  // arbitrary fill
    // Parent at depth 63 → set the last nibble (low nibble of last byte)
    auto const child = childPrefixOf(parent, 63, 0xC);
    uint256 expected = parent;
    expected.data()[31] = (expected.data()[31] & 0xF0) | 0x0C;
    EXPECT_EQ(child, expected);
}

// ---------------------------------------------------------------------------
// Plan execution — the actual rebuild walk
// ---------------------------------------------------------------------------

TEST(DeferredRebuild_Execute, EmptyPlanReturnsEmptyResult)
{
    std::vector<AffectedNode> plan;
    auto const result = executeRebuildPlan(
        plan,
        [](int /*depth*/, uint256 const& /*prefix*/) { return uint256{}; });
    EXPECT_TRUE(result.empty());
}

TEST(DeferredRebuild_Execute, SingleRootNodePlanComputesRootHash)
{
    // Plan: just the root at depth 0. All 16 children come from the
    // parent SHAMap (none are themselves affected). The rebuild
    // should produce a root hash equal to computeInnerNodeHash over
    // those children.
    std::vector<AffectedNode> plan{{0, uint256{}}};

    // Mock parent: child branch b has hash 0x100 + b
    auto const parentLookup = [](int depth, uint256 const& prefix) {
        if (depth != 1)
            return uint256{};
        // Branch is the high nibble of the first byte of prefix
        std::uint8_t branch = (prefix.data()[0] >> 4) & 0x0F;
        return uint256{0x100ULL + branch};
    };

    auto const result = executeRebuildPlan(plan, parentLookup);
    ASSERT_EQ(result.size(), 1u);

    // Expected: compute the root hash from the 16 child hashes
    std::array<uint256, 16> children;
    for (std::uint8_t b = 0; b < 16; ++b)
        children[b] = uint256{0x100ULL + b};
    auto const expectedRoot = computeInnerNodeHash(children);

    AffectedNode const rootKey{0, uint256{}};
    auto it = result.find(rootKey);
    ASSERT_NE(it, result.end());
    EXPECT_EQ(it->second, expectedRoot);
}

TEST(DeferredRebuild_Execute, ChildInPlanShadowsParentLookup)
{
    // Plan contains:
    //   - depth 1, prefix (5,0,0,...)  — this child of root is affected
    //   - depth 0, root — the root, which uses the depth-1 result as
    //                     its branch-5 child
    //
    // The plan is depth-descending so the depth-1 node is processed
    // first, and its computed hash is used as branch-5 of root.
    //
    // For the depth-1 node, all 16 of ITS children come from parent
    // lookup (depth=2).
    auto const depth1Prefix = keyWithPrefix({0x5});
    std::vector<AffectedNode> plan{{1, depth1Prefix}, {0, uint256{}}};

    // Mock parent:
    //   - depth 1 children (depth 2 lookup): return distinct hashes per branch
    //   - depth 0 children OTHER THAN branch 5 (depth 1 lookup): return
    //     distinct hashes per branch
    int parentLookupCalls = 0;
    auto const parentLookup =
        [&parentLookupCalls](int depth, uint256 const& prefix) -> uint256 {
        ++parentLookupCalls;
        if (depth == 2)
        {
            // High nibble of byte 0 is the parent's prefix nibble (5),
            // low nibble of byte 0 is the branch within that parent.
            std::uint8_t branch = prefix.data()[0] & 0x0F;
            return uint256{0xA00ULL + branch};
        }
        if (depth == 1)
        {
            std::uint8_t branch = (prefix.data()[0] >> 4) & 0x0F;
            return uint256{0xB00ULL + branch};
        }
        return uint256{};
    };

    auto const result = executeRebuildPlan(plan, parentLookup);
    ASSERT_EQ(result.size(), 2u);

    // Compute expected depth-1 hash: 16 children from depth-2 lookup
    std::array<uint256, 16> depth1Children;
    for (std::uint8_t b = 0; b < 16; ++b)
        depth1Children[b] = uint256{0xA00ULL + b};
    auto const expectedDepth1Hash = computeInnerNodeHash(depth1Children);

    AffectedNode const depth1Key{1, depth1Prefix};
    auto it1 = result.find(depth1Key);
    ASSERT_NE(it1, result.end());
    EXPECT_EQ(it1->second, expectedDepth1Hash);

    // Compute expected root hash: branch 5 is the depth-1 result, all
    // other branches come from parentLookup at depth 1
    std::array<uint256, 16> rootChildren;
    for (std::uint8_t b = 0; b < 16; ++b)
        rootChildren[b] = (b == 5) ? expectedDepth1Hash : uint256{0xB00ULL + b};
    auto const expectedRoot = computeInnerNodeHash(rootChildren);

    AffectedNode const rootKey{0, uint256{}};
    auto it0 = result.find(rootKey);
    ASSERT_NE(it0, result.end());
    EXPECT_EQ(it0->second, expectedRoot);

    // Sanity: the parentLookup should NOT have been called for the
    // branch-5 child of root (depth=1, prefix=depth1Prefix), because
    // that child IS the affected depth-1 node. Verify by counting:
    //  - depth 2 lookups: 16 (one per branch of the depth-1 node)
    //  - depth 1 lookups: 15 (all branches of root EXCEPT branch 5)
    //  - total: 31
    EXPECT_EQ(parentLookupCalls, 31);
}

TEST(DeferredRebuild_Execute, DeterministicAcrossRuns)
{
    auto const depth1Prefix = keyWithPrefix({0x3});
    std::vector<AffectedNode> plan{{1, depth1Prefix}, {0, uint256{}}};

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | prefix.data()[i];
        return uint256{v + static_cast<std::uint64_t>(depth) * 0xABCDEF};
    };

    auto const r1 = executeRebuildPlan(plan, parentLookup);
    auto const r2 = executeRebuildPlan(plan, parentLookup);
    auto const r3 = executeRebuildPlan(plan, parentLookup);
    EXPECT_EQ(r1, r2);
    EXPECT_EQ(r2, r3);
}

// ---------------------------------------------------------------------------
// Unified entry point: deferredRebuildRoot
//
// The consumer-facing API: given (modified keys, parent-state callback),
// return the new SHAMap root hash. Equivalent to plan + execute + pluck
// the depth-0 entry, but exposed as a single call so the integration
// site doesn't have to know about the AffectedNode plumbing.
// ---------------------------------------------------------------------------

TEST(DeferredRebuild_Root, EmptyKeysReturnsExistingRootFromCallback)
{
    // No modifications → no rebuild needed → the new root equals the
    // existing root, which the parent-state callback supplies at
    // (depth=0, prefix=zero).
    uint256 const expectedExistingRoot{0xDEADBEEF};
    auto const parentLookup = [&expectedExistingRoot](
                                  int depth, uint256 const& prefix) {
        if (depth == 0 && prefix == uint256{})
            return expectedExistingRoot;
        return uint256{};
    };

    auto const newRoot = deferredRebuildRoot({}, parentLookup);
    EXPECT_EQ(newRoot, expectedExistingRoot);
}

TEST(DeferredRebuild_Root, SingleKeyMatchesPlanAndExecuteComposition)
{
    // The unified entry point must be byte-identical to the explicit
    // plan + execute composition. This is the safety guarantee that
    // consumers can switch to the unified entry point with no
    // observable change.
    std::vector<uint256> keys{keyOf(42)};

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        std::uint64_t v = static_cast<std::uint64_t>(depth) * 0x1000;
        for (int i = 0; i < 4; ++i)
            v += prefix.data()[i];
        return uint256{v};
    };

    // Via composition
    auto const plan = planDeferredRebuild(keys);
    auto const result = executeRebuildPlan(plan, parentLookup);
    AffectedNode const rootKey{0, uint256{}};
    auto const composedRoot = result.at(rootKey);

    // Via unified API
    auto const unifiedRoot = deferredRebuildRoot(keys, parentLookup);

    EXPECT_EQ(unifiedRoot, composedRoot);
}

TEST(DeferredRebuild_Root, ManyKeysMatchesPlanAndExecuteComposition)
{
    std::vector<uint256> keys;
    for (std::uint64_t i = 0; i < 100; ++i)
        keys.push_back(keyOf(i * 13));

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        std::uint64_t v = static_cast<std::uint64_t>(depth);
        for (int i = 0; i < 8; ++i)
            v = (v * 257) + prefix.data()[i];
        return uint256{v};
    };

    auto const plan = planDeferredRebuild(keys);
    auto const result = executeRebuildPlan(plan, parentLookup);
    AffectedNode const rootKey{0, uint256{}};
    auto const composedRoot = result.at(rootKey);

    auto const unifiedRoot = deferredRebuildRoot(keys, parentLookup);

    EXPECT_EQ(unifiedRoot, composedRoot);
}

TEST(DeferredRebuild_Root, IdenticalInputsProduceIdenticalRoots)
{
    std::vector<uint256> keys{keyOf(1), keyOf(2), keyOf(3)};
    auto const parentLookup = [](int depth, uint256 const& prefix) {
        return uint256{
            static_cast<std::uint64_t>(depth) * 1000 + prefix.data()[0]};
    };

    auto const a = deferredRebuildRoot(keys, parentLookup);
    auto const b = deferredRebuildRoot(keys, parentLookup);
    EXPECT_EQ(a, b);
}

// ---------------------------------------------------------------------------
// End-to-end benchmark
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Parallel rebuild by subtree (P7.3).
//
// The root has 16 children at depth 1 — one per first-nibble value of
// the keys. Modified keys partition disjointly into these 16 subtrees;
// each subtree's rebuild touches only its own ancestor paths and never
// reads or writes another subtree's nodes. So we can rebuild all 16
// subtrees in parallel, then combine the 16 child hashes into the
// root in one final step.
//
// The combine step needs:
//   * For each non-empty subtree: the new depth-1 hash from the rebuild
//   * For each empty subtree:    the existing depth-1 hash from the
//                                parent SHAMap (unchanged)
// — then compute the root via computeInnerNodeHash.
//
// The pure-function design keeps parallelism orthogonal to correctness:
// the caller chooses serial or parallel execution, but the result must
// be byte-identical to the single-threaded deferredRebuildRoot.
// ---------------------------------------------------------------------------

TEST(DeferredRebuild_Partition, EmptyKeysProduceEmptyBuckets)
{
    auto const buckets = partitionByFirstNibble({});
    for (auto const& b : buckets)
        EXPECT_TRUE(b.empty());
}

TEST(DeferredRebuild_Partition, KeysGoToCorrectBucketByFirstNibble)
{
    auto const keyA = keyWithPrefix({0x0});
    auto const keyB = keyWithPrefix({0x5});
    auto const keyC = keyWithPrefix({0xF});

    auto const buckets = partitionByFirstNibble({keyA, keyB, keyC});

    EXPECT_EQ(buckets[0x0].size(), 1u);
    EXPECT_EQ(buckets[0x5].size(), 1u);
    EXPECT_EQ(buckets[0xF].size(), 1u);
    for (std::size_t i = 0; i < 16; ++i)
        if (i != 0x0 && i != 0x5 && i != 0xF)
            EXPECT_TRUE(buckets[i].empty()) << "Bucket " << i << " unexpectedly populated";
}

TEST(DeferredRebuild_Partition, MultipleKeysShareBuckets)
{
    auto const keyA = keyWithPrefix({0x3, 0x1});
    auto const keyB = keyWithPrefix({0x3, 0x2});
    auto const keyC = keyWithPrefix({0x7, 0x0});

    auto const buckets = partitionByFirstNibble({keyA, keyB, keyC});

    EXPECT_EQ(buckets[0x3].size(), 2u);
    EXPECT_EQ(buckets[0x7].size(), 1u);
}

TEST(DeferredRebuild_Parallel, EmptyKeysMatchSerial)
{
    auto const parentLookup = [](int depth, uint256 const& prefix) {
        if (depth == 0 && prefix == uint256{})
            return uint256{0xDEAD};
        return uint256{};
    };

    EXPECT_EQ(
        deferredRebuildRootParallel({}, parentLookup),
        deferredRebuildRoot({}, parentLookup));
}

TEST(DeferredRebuild_Parallel, SingleKeyMatchesSerial)
{
    std::vector<uint256> keys{keyOf(42)};

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        std::uint64_t v = static_cast<std::uint64_t>(depth) * 0x1000;
        for (int i = 0; i < 4; ++i)
            v += prefix.data()[i];
        return uint256{v};
    };

    EXPECT_EQ(
        deferredRebuildRootParallel(keys, parentLookup),
        deferredRebuildRoot(keys, parentLookup));
}

TEST(DeferredRebuild_Parallel, ManyKeysAcrossManySubtreesMatchSerial)
{
    // Keys spread across all 16 first-nibble buckets to exercise the
    // multi-subtree case.
    std::vector<uint256> keys;
    for (std::uint64_t n = 0; n < 16; ++n)
    {
        for (std::uint64_t j = 0; j < 30; ++j)
        {
            // First nibble = n, rest scattered
            auto k = keyWithPrefix({static_cast<std::uint8_t>(n)});
            // Mix in some entropy for nibbles 1+
            for (int i = 1; i < 8; ++i)
                k.data()[i / 2] ^= static_cast<std::uint8_t>(j * 0x37 + i);
            keys.push_back(k);
        }
    }

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        std::uint64_t v = static_cast<std::uint64_t>(depth);
        for (int i = 0; i < 8; ++i)
            v = v * 257 + prefix.data()[i];
        return uint256{v};
    };

    auto const serialRoot = deferredRebuildRoot(keys, parentLookup);
    auto const parallelRoot = deferredRebuildRootParallel(keys, parentLookup);
    EXPECT_EQ(parallelRoot, serialRoot);
}

TEST(DeferredRebuild_Parallel, AllKeysInSingleSubtreeMatchSerial)
{
    // Adversarial case: every key has the same first nibble, so 15
    // subtrees are empty and the workload doesn't parallelize. The
    // parallel implementation must still produce the same result.
    std::vector<uint256> keys;
    for (std::uint64_t j = 0; j < 50; ++j)
    {
        auto k = keyWithPrefix({0x7});  // all keys start with 7
        for (int i = 1; i < 8; ++i)
            k.data()[i / 2] ^= static_cast<std::uint8_t>(j * 0x11 + i);
        keys.push_back(k);
    }

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        std::uint64_t v = static_cast<std::uint64_t>(depth) * 0xABCDEF;
        for (int i = 0; i < 8; ++i)
            v += prefix.data()[i];
        return uint256{v};
    };

    EXPECT_EQ(
        deferredRebuildRootParallel(keys, parentLookup),
        deferredRebuildRoot(keys, parentLookup));
}

TEST(DeferredRebuild_Bench, EndToEndAtLedgerScale)
{
    // Realistic mainnet workload: ~3000 modifications per ledger.
    // Measure the full plan + execute pipeline as a single number,
    // since that's what production close-time will pay.
    //
    // The parent lookup is a constant-time computation — in production
    // it's a SHAMap walk, which dominates the cost. This benchmark
    // measures only the algorithmic overhead of the deferred rebuild
    // itself, isolated from SHAMap traversal cost.
    constexpr std::size_t N = 3'000;
    std::vector<uint256> keys;
    keys.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
        keys.push_back(keyOf(i));

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        return uint256{
            static_cast<std::uint64_t>(depth) * 0xABCDEF +
            prefix.data()[0] * 0x100 + prefix.data()[1]};
    };

    auto const t0 = std::chrono::high_resolution_clock::now();
    auto const newRoot = deferredRebuildRoot(keys, parentLookup);
    auto const elapsed = std::chrono::high_resolution_clock::now() - t0;
    auto const us =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

    std::printf(
        "  deferredRebuildRoot N=%zu keys : %lld µs total\n",
        N,
        static_cast<long long>(us));

    benchmark_use(newRoot);

    // Post-LCP: measured ~2.3 ms locally — comfortably within
    // plan-7's ~7 ms close-time budget. 25 ms threshold = ~10×.
    EXPECT_LT(us, 25'000)
        << "End-to-end rebuild at 3k keys exceeded 25 ms (plan-7 budget ~7 ms)";
}

TEST(DeferredRebuild_Bench, ParallelVsSerialAtLedgerScale)
{
    constexpr std::size_t N = 3'000;
    std::vector<uint256> keys;
    keys.reserve(N);
    // Spread keys across first-nibble buckets so all 16 subtrees see work.
    for (std::uint64_t i = 0; i < N; ++i)
    {
        auto k = keyOf(i);
        // Force first nibble to vary by i % 16
        k.data()[0] = (k.data()[0] & 0x0F) |
            static_cast<std::uint8_t>((i % 16) << 4);
        keys.push_back(k);
    }

    auto const parentLookup = [](int depth, uint256 const& prefix) {
        return uint256{
            static_cast<std::uint64_t>(depth) * 0xABCDEF +
            prefix.data()[0] * 0x100 + prefix.data()[1]};
    };

    auto const t0 = std::chrono::high_resolution_clock::now();
    auto const serialRoot = deferredRebuildRoot(keys, parentLookup);
    auto const t1 = std::chrono::high_resolution_clock::now();
    auto const parallelRoot = deferredRebuildRootParallel(keys, parentLookup);
    auto const t2 = std::chrono::high_resolution_clock::now();

    auto const serialUs =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto const parallelUs =
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

    std::printf(
        "  serial   : %lld µs\n"
        "  parallel : %lld µs  (speedup %.2fx)\n",
        static_cast<long long>(serialUs),
        static_cast<long long>(parallelUs),
        static_cast<double>(serialUs) / std::max<long long>(1, parallelUs));

    EXPECT_EQ(serialRoot, parallelRoot);
    // Parallel must not be slower than serial by more than 2× (which
    // would indicate the threading overhead dominates the work and
    // we're shipping the wrong implementation).
    EXPECT_LT(parallelUs, serialUs * 2)
        << "Parallel slower than 2× serial — threading overhead unexpected";
}
