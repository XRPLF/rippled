#include <xrpl/core/HashRouter.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>

#include <gtest/gtest.h>

namespace xrpl::tests {

namespace {

/**
 * A router with the default setup on a manual clock.
 */
struct HashRouterTest : public ::testing::Test
{
    TestStopwatch clock;
    HashRouter router{HashRouter::Setup{}, clock};
};

}  // namespace

TEST_F(HashRouterTest, clear_flags_clears_only_the_named_flags)
{
    UInt256 const key{1};
    EXPECT_TRUE(router.setFlags(key, HashRouterFlags::SAVED | HashRouterFlags::BAD));

    EXPECT_TRUE(router.clearFlags(key, HashRouterFlags::SAVED));
    EXPECT_EQ(router.getFlags(key), HashRouterFlags::BAD);

    // The flag is clear, so setting it again is a change.
    EXPECT_TRUE(router.setFlags(key, HashRouterFlags::SAVED));

    // A mask with one set flag and one clear flag is still a change.
    EXPECT_TRUE(router.clearFlags(key, HashRouterFlags::SAVED | HashRouterFlags::HELD));
    EXPECT_EQ(router.getFlags(key), HashRouterFlags::BAD);
}

TEST_F(HashRouterTest, clear_flags_reports_when_nothing_changes)
{
    UInt256 const key{2};

    // No entry, so nothing to clear and no entry is created: the next insertion is a new one.
    EXPECT_FALSE(router.clearFlags(key, HashRouterFlags::SAVED));
    EXPECT_TRUE(router.addSuppressionPeer(key, 0));

    // An entry whose flags do not include the one named.
    EXPECT_TRUE(router.setFlags(key, HashRouterFlags::BAD));
    EXPECT_FALSE(router.clearFlags(key, HashRouterFlags::SAVED));
    EXPECT_EQ(router.getFlags(key), HashRouterFlags::BAD);
}

}  // namespace xrpl::tests
