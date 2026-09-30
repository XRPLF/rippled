#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <string>

namespace xrpl {

class KeyCacheTest : public ::testing::Test
{
public:
};

TEST_F(KeyCacheTest, key_cache)
{
    using namespace std::chrono_literals;
    TestStopwatch clock;
    clock.set(0);

    using Key = std::string;
    using Cache = TaggedCache<Key, int, true>;

    beast::Journal const j{TestSink::instance()};

    // Insert an item, retrieve it, and age it so it gets purged.
    {
        Cache c("test", LedgerIndex(1), 2s, clock, j);

        EXPECT_EQ(c.size(), 0);
        EXPECT_TRUE(c.insert("one"));
        EXPECT_FALSE(c.insert("one"));
        EXPECT_EQ(c.size(), 1);
        EXPECT_TRUE(c.touchIfExists("one"));
        ++clock;
        c.sweep();
        EXPECT_EQ(c.size(), 1);
        ++clock;
        c.sweep();
        EXPECT_EQ(c.size(), 0);
        EXPECT_FALSE(c.touchIfExists("one"));
    }

    // Insert two items, have one expire
    {
        Cache c("test", LedgerIndex(2), 2s, clock, j);

        EXPECT_TRUE(c.insert("one"));
        EXPECT_EQ(c.size(), 1);
        EXPECT_TRUE(c.insert("two"));
        EXPECT_EQ(c.size(), 2);
        ++clock;
        c.sweep();
        EXPECT_EQ(c.size(), 2);
        EXPECT_TRUE(c.touchIfExists("two"));
        ++clock;
        c.sweep();
        EXPECT_EQ(c.size(), 1);
    }

    // Insert three items (1 over limit), sweep
    {
        Cache c("test", LedgerIndex(2), 3s, clock, j);

        EXPECT_TRUE(c.insert("one"));
        ++clock;
        EXPECT_TRUE(c.insert("two"));
        ++clock;
        EXPECT_TRUE(c.insert("three"));
        ++clock;
        EXPECT_EQ(c.size(), 3);
        c.sweep();
        EXPECT_LT(c.size(), 3);
    }
}

}  // namespace xrpl
