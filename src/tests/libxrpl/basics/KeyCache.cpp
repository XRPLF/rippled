#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <chrono>
#include <string>

namespace xrpl {

class KeyCacheTest : public ::testing::Test
{
public:
    using Cache = TaggedCache<std::string, int, true>;

    static constexpr std::chrono::seconds kExpiration{2};

protected:
    beast::Journal const j_{TestSink::instance()};
    TestStopwatch clock_;  ///< ManualClock starts at zero
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

TEST_F(KeyCacheTest, touch_if_exists_counts_hits_and_misses)
{
    Cache c("test", LedgerIndex(1), kExpiration, clock_, j_);
    EXPECT_FLOAT_EQ(c.getHitRate(), 0.0f);

    EXPECT_TRUE(c.insert("one"));
    EXPECT_TRUE(c.touchIfExists("one"));
    EXPECT_FALSE(c.touchIfExists("two"));

    // One hit and one miss, so the rate the reporters read is fifty percent.
    EXPECT_FLOAT_EQ(c.getHitRate(), 50.0f);
}

TEST_F(KeyCacheTest, sweep_counts_removals)
{
    Cache c("test", LedgerIndex(2), kExpiration, clock_, j_);

    EXPECT_TRUE(c.insert("one"));
    EXPECT_TRUE(c.insert("two"));
    EXPECT_EQ(c.getCacheSize(), 2);
    EXPECT_EQ(c.getTrackSize(), 2);

    // A repeat insert half way to expiry refreshes the entry and is not counted again.
    clock_.advance(kExpiration / 2);
    EXPECT_FALSE(c.insert("two"));
    EXPECT_EQ(c.getCacheSize(), 2);

    // At the expiry of "one", only "one" ages out: the refreshed "two" has half its age left, so
    // the cache count follows the map down by one.
    clock_.advance(kExpiration / 2);
    c.sweep();
    EXPECT_EQ(c.getCacheSize(), 1);
    EXPECT_EQ(c.getTrackSize(), 1);

    // Another half expiry later "two" ages out as well.
    clock_.advance(kExpiration / 2);
    c.sweep();
    EXPECT_EQ(c.getCacheSize(), 0);
    EXPECT_EQ(c.getTrackSize(), 0);
}

}  // namespace xrpl
