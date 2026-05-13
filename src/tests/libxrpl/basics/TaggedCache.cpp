#include <xrpl/basics/TaggedCache.h>

#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <memory>
#include <string>

namespace xrpl {

/*
I guess you can put some items in, make sure they're still there. Let some
time pass, make sure they're gone. Keep a strong pointer to one of them, make
sure you can still find it even after time passes. Create two objects with
the same key, canonicalize them both and make sure you get the same object.
Put an object in but keep a strong pointer to it, advance the clock a lot,
then canonicalize a new object with the same key, make sure you get the
original object.
*/

class TaggedCacheTest : public ::testing::Test
{
public:
    static void
    run()
    {
        using namespace std::chrono_literals;
        beast::Journal const journal{TestSink::instance()};

        TestStopwatch clock;
        clock.set(0);

        using Key = LedgerIndex;
        using Value = std::string;
        using Cache = TaggedCache<Key, Value>;

        Cache c("test", 1, 1s, clock, journal);

        // Insert an item, retrieve it, and age it so it gets purged.
        {
            EXPECT_TRUE(c.getCacheSize() == 0);
            EXPECT_TRUE(c.getTrackSize() == 0);
            EXPECT_TRUE(!c.insert(1, "one"));
            EXPECT_TRUE(c.getCacheSize() == 1);
            EXPECT_TRUE(c.getTrackSize() == 1);

            {
                std::string s;
                EXPECT_TRUE(c.retrieve(1, s));
                EXPECT_TRUE(s == "one");
            }

            ++clock;
            c.sweep();
            EXPECT_TRUE(c.getCacheSize() == 0);
            EXPECT_TRUE(c.getTrackSize() == 0);
        }

        // Insert an item, maintain a strong pointer, age it, and
        // verify that the entry still exists.
        {
            EXPECT_TRUE(!c.insert(2, "two"));
            EXPECT_TRUE(c.getCacheSize() == 1);
            EXPECT_TRUE(c.getTrackSize() == 1);

            {
                auto p = c.fetch(2);
                EXPECT_TRUE(p != nullptr);
                ++clock;
                c.sweep();
                EXPECT_TRUE(c.getCacheSize() == 0);
                EXPECT_TRUE(c.getTrackSize() == 1);
            }

            // Make sure its gone now that our reference is gone
            ++clock;
            c.sweep();
            EXPECT_TRUE(c.getCacheSize() == 0);
            EXPECT_TRUE(c.getTrackSize() == 0);
        }

        // Insert the same key/value pair and make sure we get the same result
        {
            EXPECT_TRUE(!c.insert(3, "three"));

            {
                auto const p1 = c.fetch(3);
                auto p2 = std::make_shared<Value>("three");
                c.canonicalizeReplaceClient(3, p2);
                EXPECT_TRUE(p1.get() == p2.get());
            }
            ++clock;
            c.sweep();
            EXPECT_TRUE(c.getCacheSize() == 0);
            EXPECT_TRUE(c.getTrackSize() == 0);
        }

        // Put an object in but keep a strong pointer to it, advance the clock a
        // lot, then canonicalize a new object with the same key, make sure you
        // get the original object.
        {
            // Put an object in
            EXPECT_TRUE(!c.insert(4, "four"));
            EXPECT_TRUE(c.getCacheSize() == 1);
            EXPECT_TRUE(c.getTrackSize() == 1);

            {
                // Keep a strong pointer to it
                auto const p1 = c.fetch(4);
                EXPECT_TRUE(p1 != nullptr);
                EXPECT_TRUE(c.getCacheSize() == 1);
                EXPECT_TRUE(c.getTrackSize() == 1);
                // Advance the clock a lot
                ++clock;
                c.sweep();
                EXPECT_TRUE(c.getCacheSize() == 0);
                EXPECT_TRUE(c.getTrackSize() == 1);
                // Canonicalize a new object with the same key
                auto p2 = std::make_shared<std::string>("four");
                EXPECT_TRUE(c.canonicalizeReplaceClient(4, p2));
                EXPECT_TRUE(c.getCacheSize() == 1);
                EXPECT_TRUE(c.getTrackSize() == 1);
                // Make sure we get the original object
                EXPECT_TRUE(p1.get() == p2.get());
            }

            ++clock;
            c.sweep();
            EXPECT_TRUE(c.getCacheSize() == 0);
            EXPECT_TRUE(c.getTrackSize() == 0);
        }
    }
};

TEST_F(TaggedCacheTest, tagged_cache)
{
    run();
}

}  // namespace xrpl
