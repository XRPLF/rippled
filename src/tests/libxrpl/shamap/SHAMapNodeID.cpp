#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>

#include <gtest/gtest.h>

#include <stdexcept>

namespace xrpl::tests {

// An arbitrary 32-byte key reused across tests below that don't care about its specific value,
// only that it is a well-formed key.
constexpr uint256 kTestKey("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

TEST(SHAMapNodeIDTest, root_is_prefix_of_every_key)
{
    SHAMapNodeID const root;
    EXPECT_EQ(root.getDepth(), 0u);
    EXPECT_TRUE(root.isPrefixOf(uint256{}));
    EXPECT_TRUE(root.isPrefixOf(kTestKey));
}

TEST(SHAMapNodeIDTest, child_id_is_prefix_of_keys_in_that_branch)
{
    // Walking the branches spelled by the key's own nibbles must keep every
    // intermediate ID a prefix of that key.
    SHAMapNodeID id;
    for (auto depth = 0u; depth < SHAMap::kLeafDepth; ++depth)
    {
        id = id.getChildNodeID(selectBranch(id, kTestKey));
        EXPECT_EQ(id.getDepth(), depth + 1);
        EXPECT_TRUE(id.isPrefixOf(kTestKey)) << "depth " << id.getDepth();
    }
}

TEST(SHAMapNodeIDTest, wrong_branch_is_not_prefix_of_key)
{
    SHAMapNodeID const root;
    auto const correct = selectBranch(root, kTestKey);
    ASSERT_EQ(correct, 0xbu);

    // An ID built from the wrong branch still has a valid depth and a self-consistent mask, so
    // isPrefixOf(kTestKey) below is what actually distinguishes the correct branch from the rest.
    for (auto branch = 0u; branch < SHAMap::kBranchFactor; ++branch)
    {
        auto const child = root.getChildNodeID(branch);
        EXPECT_EQ(child.getDepth(), 1u);
        EXPECT_EQ(child.isPrefixOf(kTestKey), branch == correct) << "branch " << branch;
    }
}

TEST(SHAMapNodeIDTest, prefix_check_is_depth_sensitive)
{
    // kTestKey and kOther agree on the first two nibbles ("b9") and then diverge.
    constexpr uint256 kOther("b99891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca8");

    auto id = SHAMapNodeID{}.getChildNodeID(selectBranch(SHAMapNodeID{}, kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kOther)) << "shared first nibble";

    id = id.getChildNodeID(selectBranch(id, kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kOther)) << "shared second nibble";

    // Third nibble differs, so the deeper ID no longer covers kOther.
    id = id.getChildNodeID(selectBranch(id, kTestKey));
    EXPECT_TRUE(id.isPrefixOf(kTestKey));
    EXPECT_FALSE(id.isPrefixOf(kOther));
}

TEST(SHAMapNodeIDTest, leaf_id_from_key_is_prefix_of_that_key)
{
    SHAMapNodeID const leaf{SHAMap::kLeafDepth, kTestKey};
    EXPECT_TRUE(leaf.isPrefixOf(kTestKey));

    // At full depth the prefix is the whole key, so nothing else matches.
    constexpr uint256 kOther("b92891fe4ef6cee585fdc6fda1e09eb4d386363158ec3321b8123e5a772c6ca9");
    EXPECT_FALSE(leaf.isPrefixOf(kOther));
}

TEST(SHAMapNodeIDTest, create_id_masks_key_to_depth)
{
    for (auto depth = 0u; depth <= SHAMap::kLeafDepth; ++depth)
    {
        auto const id = SHAMapNodeID::createID(depth, kTestKey);
        EXPECT_EQ(id.getDepth(), depth);
        EXPECT_TRUE(id.isPrefixOf(kTestKey)) << "depth " << depth;
    }
}

// The guards below must hold with XRPL_ASSERT compiled out (NDEBUG), so each one
// has to be a real runtime check rather than an assert.

TEST(SHAMapNodeIDTest, child_of_leaf_depth_id_throws)
{
    auto const leafDepthID = SHAMapNodeID::createID(SHAMap::kLeafDepth, kTestKey);
    ASSERT_EQ(leafDepthID.getDepth(), SHAMap::kLeafDepth);
    EXPECT_THROW((void)leafDepthID.getChildNodeID(0), std::logic_error);
}

TEST(SHAMapNodeIDTest, out_of_range_depth_is_clamped)
{
    // A depth past kLeafDepth has no mask in depthMask's 65-entry table, so both the constructor
    // and createID clamp it. createID needs its own clamp: it picks the mask while evaluating the
    // constructor's argument, so the constructor's clamp cannot cover that read.
    //
    // Both clamps are marked UNREACHABLE, which is an assert and therefore fatal wherever asserts
    // are live. Only a build with them compiled out (or routed to Antithesis's non-fatal handler)
    // reaches the clamp itself, so that is the only configuration that can assert on the result.
#if defined(NDEBUG) || defined(ENABLE_VOIDSTAR)
    for (auto const depth : {SHAMap::kLeafDepth + 1u, 100u, 255u, 256u, 320u})
    {
        auto const id = SHAMapNodeID::createID(depth, kTestKey);

        // Clamped to a real depth, not the depth asked for, and not a byte-narrowed version of it:
        // 256 would otherwise become 0 and name the root, 320 would become 64.
        EXPECT_EQ(id.getDepth(), SHAMap::kLeafDepth) << "depth " << depth;

        // id_ and depth_ still agree, so the object is usable rather than merely non-crashing.
        EXPECT_TRUE(id.isPrefixOf(kTestKey)) << "depth " << depth;
        EXPECT_EQ(id, SHAMapNodeID::createID(SHAMap::kLeafDepth, kTestKey)) << "depth " << depth;

        // The clamp holds through the wire format too, which encodes the depth in one byte.
        auto const roundTripped = deserializeSHAMapNodeID(id.getRawString());
        ASSERT_TRUE(roundTripped.has_value()) << "depth " << depth;
        EXPECT_EQ(roundTripped->getDepth(), SHAMap::kLeafDepth) << "depth " << depth;
    }

    // The constructor clamps on its own, for the paths that do not go through createID.
    SHAMapNodeID const direct{SHAMap::kLeafDepth + 1u, uint256{}};
    EXPECT_EQ(direct.getDepth(), SHAMap::kLeafDepth);
#else
    EXPECT_DEATH(
        (void)SHAMapNodeID::createID(SHAMap::kLeafDepth + 1u, kTestKey), "depth within tree");
#endif
}

TEST(SHAMapNodeIDTest, select_branch_clamps_leaf_depth)
{
    // selectBranch's own precondition is depth < kLeafDepth: a depth-64 ID has no nibble left
    // to select. That makes it unlike the guards above, which have a throw/return reachable
    // even with XRPL_ASSERT compiled out; selectBranch has no such path, so the two build
    // configurations have to be tested differently.
    //
    // Under ENABLE_VOIDSTAR, XRPL_ASSERT routes to Antithesis's assert_impl, which only records
    // the hit and returns rather than aborting, even though NDEBUG is undefined there (voidstar
    // requires a Debug build). So the assert is live in name but never fatal, the same as the
    // NDEBUG case below.
    auto const leafDepthID = SHAMapNodeID::createID(SHAMap::kLeafDepth, kTestKey);

#if defined(NDEBUG) || defined(ENABLE_VOIDSTAR)
    // With the assert compiled out or routed to a non-fatal handler, the clamp is what stands
    // between this call and reading past the end of the 32-byte key. Clamping means it reads the
    // same byte, and returns the same branch, as the deepest ID that still has one: depth 63.
    auto const deepestWithBranchID = SHAMapNodeID::createID(SHAMap::kLeafDepth - 1u, kTestKey);
    auto const branch = selectBranch(leafDepthID, kTestKey);
    EXPECT_LT(branch, SHAMap::kBranchFactor);
    EXPECT_EQ(branch, selectBranch(deepestWithBranchID, kTestKey));
#else
    // In a debug build the assert is live and must reject this call outright, in a forked
    // process so a failure here cannot take down the rest of the suite.
    EXPECT_DEATH((void)selectBranch(leafDepthID, kTestKey), "depth below leaf depth");
#endif
}

TEST(SHAMapNodeIDTest, deserialize_rejects_out_of_range_depth)
{
    // getRawString() only serializes a depth already accepted by the constructor's own
    // assertion, so an out-of-range depth here is built by hand instead.
    auto serializeWithRawDepth = [](unsigned int depth) {
        Serializer s;
        s.addBitString(uint256{});
        s.add8(static_cast<unsigned char>(depth));
        return s.getString();
    };

    for (auto const depth : {65u, 100u, 255u})
    {
        EXPECT_FALSE(deserializeSHAMapNodeID(serializeWithRawDepth(depth)).has_value())
            << "depth " << depth;
    }

    // A depth-64 ID is legal, since leaves live there, but it has no children.
    auto const id =
        deserializeSHAMapNodeID(SHAMapNodeID{SHAMap::kLeafDepth, uint256{}}.getRawString());
    ASSERT_TRUE(id.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) has_value checked above
    EXPECT_THROW((void)id->getChildNodeID(0), std::logic_error);
}

}  // namespace xrpl::tests
