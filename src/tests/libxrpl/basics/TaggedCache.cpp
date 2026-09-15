#include <xrpl/basics/TaggedCache.h>

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusiveRefCounts.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

TEST(TaggedCacheTest, tagged_cache)
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
        EXPECT_EQ(c.getCacheSize(), 0);
        EXPECT_EQ(c.getTrackSize(), 0);
        EXPECT_FALSE(c.insert(1, "one"));
        EXPECT_EQ(c.getCacheSize(), 1);
        EXPECT_EQ(c.getTrackSize(), 1);

        {
            std::string s;
            EXPECT_TRUE(c.retrieve(1, s));
            EXPECT_EQ(s, "one");
        }

        ++clock;
        c.sweep();
        EXPECT_EQ(c.getCacheSize(), 0);
        EXPECT_EQ(c.getTrackSize(), 0);
    }

    // Insert an item, maintain a strong pointer, age it, and
    // verify that the entry still exists.
    {
        EXPECT_FALSE(c.insert(2, "two"));
        EXPECT_EQ(c.getCacheSize(), 1);
        EXPECT_EQ(c.getTrackSize(), 1);

        {
            auto p = c.fetch(2);
            EXPECT_NE(p, nullptr);
            ++clock;
            c.sweep();
            EXPECT_EQ(c.getCacheSize(), 0);
            EXPECT_EQ(c.getTrackSize(), 1);
        }

        // Make sure its gone now that our reference is gone
        ++clock;
        c.sweep();
        EXPECT_EQ(c.getCacheSize(), 0);
        EXPECT_EQ(c.getTrackSize(), 0);
    }

    // Insert the same key/value pair and make sure we get the same result
    {
        EXPECT_FALSE(c.insert(3, "three"));

        {
            auto const p1 = c.fetch(3);
            auto p2 = std::make_shared<Value>("three");
            c.canonicalizeReplaceClient(3, p2);
            EXPECT_EQ(p1.get(), p2.get());
        }
        ++clock;
        c.sweep();
        EXPECT_EQ(c.getCacheSize(), 0);
        EXPECT_EQ(c.getTrackSize(), 0);
    }

    // Put an object in but keep a strong pointer to it, advance the clock a
    // lot, then canonicalize a new object with the same key, make sure you
    // get the original object.
    {
        // Put an object in
        EXPECT_FALSE(c.insert(4, "four"));
        EXPECT_EQ(c.getCacheSize(), 1);
        EXPECT_EQ(c.getTrackSize(), 1);

        {
            // Keep a strong pointer to it
            auto const p1 = c.fetch(4);
            EXPECT_NE(p1, nullptr);
            EXPECT_EQ(c.getCacheSize(), 1);
            EXPECT_EQ(c.getTrackSize(), 1);
            // Advance the clock a lot
            ++clock;
            c.sweep();
            EXPECT_EQ(c.getCacheSize(), 0);
            EXPECT_EQ(c.getTrackSize(), 1);
            // Canonicalize a new object with the same key
            auto p2 = std::make_shared<std::string>("four");
            EXPECT_TRUE(c.canonicalizeReplaceClient(4, p2));
            EXPECT_EQ(c.getCacheSize(), 1);
            EXPECT_EQ(c.getTrackSize(), 1);
            // Make sure we get the original object
            EXPECT_EQ(p1.get(), p2.get());
        }

        ++clock;
        c.sweep();
        EXPECT_EQ(c.getCacheSize(), 0);
        EXPECT_EQ(c.getTrackSize(), 0);
    }

    {
        EXPECT_FALSE(c.insert(5, "five"));
        EXPECT_EQ(c.getCacheSize(), 1);
        EXPECT_EQ(c.size(), 1);

        {
            auto const p1 = c.fetch(5);
            EXPECT_NE(p1, nullptr);
            EXPECT_EQ(c.getCacheSize(), 1);
            EXPECT_EQ(c.size(), 1);

            // Advance the clock a lot
            ++clock;
            c.sweep();
            EXPECT_EQ(c.getCacheSize(), 0);
            EXPECT_EQ(c.size(), 1);

            auto p2 = std::make_shared<std::string>("five_2");
            EXPECT_TRUE(c.canonicalizeReplaceCache(5, p2));
            EXPECT_EQ(c.getCacheSize(), 1);
            EXPECT_EQ(c.size(), 1);
            // Make sure the caller's original pointer is unchanged
            EXPECT_NE(p1.get(), p2.get());
            EXPECT_EQ(*p2, "five_2");

            auto const p3 = c.fetch(5);
            EXPECT_NE(p3, nullptr);
            EXPECT_EQ(p3.get(), p2.get());
            EXPECT_NE(p3.get(), p1.get());
        }

        ++clock;
        c.sweep();
        EXPECT_EQ(c.getCacheSize(), 0);
        EXPECT_EQ(c.size(), 0);
    }

    {
        struct MyRefCountObject : IntrusiveRefCounts
        {
            std::string data;

            // Needed to support weak intrusive pointers
            virtual void
            partialDestructor()
            {
            }

            MyRefCountObject() = default;
            explicit MyRefCountObject(std::string data) : data(std::move(data))
            {
            }

            bool
            operator==(std::string const& other) const
            {
                return data == other;
            }
        };

        using IntrPtrCache = TaggedCache<
            Key,
            MyRefCountObject,
            /*IsKeyCache*/ false,
            intr_ptr::SharedWeakUnionPtr<MyRefCountObject>,
            intr_ptr::SharedPtr<MyRefCountObject>>;

        IntrPtrCache intrPtrCache("IntrPtrTest", 1, 1s, clock, journal);

        intrPtrCache.canonicalizeReplaceCache(1, intr_ptr::makeShared<MyRefCountObject>("one"));
        EXPECT_EQ(intrPtrCache.getCacheSize(), 1);
        EXPECT_EQ(intrPtrCache.size(), 1);

        {
            {
                intrPtrCache.canonicalizeReplaceCache(
                    1, intr_ptr::makeShared<MyRefCountObject>("one_replaced"));

                auto p = intrPtrCache.fetch(1);
                EXPECT_EQ(*p, "one_replaced");

                // Advance the clock a lot
                ++clock;
                intrPtrCache.sweep();
                EXPECT_EQ(intrPtrCache.getCacheSize(), 0);
                EXPECT_EQ(intrPtrCache.size(), 1);

                intrPtrCache.canonicalizeReplaceCache(
                    1, intr_ptr::makeShared<MyRefCountObject>("one_replaced_2"));

                auto p2 = intrPtrCache.fetch(1);
                EXPECT_EQ(*p2, "one_replaced_2");

                intrPtrCache.del(1, true);
            }

            intrPtrCache.canonicalizeReplaceCache(
                1, intr_ptr::makeShared<MyRefCountObject>("one_replaced_3"));
            auto p3 = intrPtrCache.fetch(1);
            EXPECT_EQ(*p3, "one_replaced_3");
        }

        ++clock;
        intrPtrCache.sweep();
        EXPECT_EQ(intrPtrCache.getCacheSize(), 0);
        EXPECT_EQ(intrPtrCache.size(), 0);
    }
}

TEST(TaggedCacheTest, lock_hold_peak_records_getkeys_and_sweep_then_resets)
{
    using namespace std::chrono_literals;
    beast::Journal const journal{TestSink::instance()};
    TestStopwatch clock;
    clock.set(0);

    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache c("peak", 0, 1s, clock, journal);

    // Nothing has held the lock yet.
    EXPECT_EQ(c.takeLockHoldPeak(), 0ns);

    // Enough entries that copying every key takes a measurable time.
    for (LedgerIndex i = 0; i < 200'000; ++i)
        c.insert(i, "v");

    auto const keys = c.getKeys();
    ASSERT_EQ(keys.size(), 200'000u);
    auto const afterGetKeys = c.takeLockHoldPeak();
    EXPECT_GT(afterGetKeys, 0ns);
    // take() is destructive: the next read starts from zero.
    EXPECT_EQ(c.takeLockHoldPeak(), 0ns);

    // A sweep that expires everything also holds the lock over every entry.
    ++clock;
    ++clock;
    c.sweep();
    EXPECT_GT(c.takeLockHoldPeak(), 0ns);
    EXPECT_EQ(c.getTrackSize(), 0);
}

TEST(TaggedCacheTest, for_each_key_partition_visits_every_key_exactly_once)
{
    using namespace std::chrono_literals;
    beast::Journal const journal{TestSink::instance()};
    TestStopwatch clock;
    clock.set(0);

    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache c("partitions", 0, 1s, clock, journal);

    constexpr LedgerIndex keyCount = 10'000;
    for (LedgerIndex i = 0; i < keyCount; ++i)
        c.insert(i, "v");
    ASSERT_EQ(c.getTrackSize(), static_cast<int>(keyCount));

    std::vector<LedgerIndex> visited;
    std::size_t calls = 0;
    c.forEachKeyPartition([&](std::vector<LedgerIndex> const& keys) {
        ++calls;
        visited.insert(visited.end(), keys.begin(), keys.end());
    });

    // The cache's map is built with one partition per hardware thread, and the
    // callback runs once per partition.
    EXPECT_EQ(calls, static_cast<std::size_t>(std::thread::hardware_concurrency()));

    auto expected = c.getKeys();
    std::ranges::sort(expected);
    std::ranges::sort(visited);
    ASSERT_EQ(visited.size(), keyCount);
    EXPECT_EQ(visited, expected);
}

TEST(TaggedCacheTest, for_each_key_partition_on_empty_cache_calls_back_with_empty_batches)
{
    using namespace std::chrono_literals;
    beast::Journal const journal{TestSink::instance()};
    TestStopwatch clock;
    clock.set(0);

    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache c("empty", 0, 1s, clock, journal);

    std::size_t calls = 0;
    std::size_t keysSeen = 0;
    c.forEachKeyPartition([&](std::vector<LedgerIndex> const& keys) {
        ++calls;
        keysSeen += keys.size();
    });

    EXPECT_EQ(calls, static_cast<std::size_t>(std::thread::hardware_concurrency()));
    EXPECT_EQ(keysSeen, 0u);
}

TEST(TaggedCacheTest, for_each_key_partition_releases_the_mutex_while_the_callback_runs)
{
    using namespace std::chrono_literals;
    beast::Journal const journal{TestSink::instance()};
    TestStopwatch clock;
    clock.set(0);

    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache c("unlocked", 0, 1s, clock, journal);
    for (LedgerIndex i = 0; i < 1'000; ++i)
        c.insert(i, "v");

    // A recursive mutex lets the calling thread re-lock it, so the probe has to
    // run on another thread: try_lock there succeeds only if this thread is not
    // holding the mutex while the callback runs.
    std::size_t calls = 0;
    std::size_t unlockedDuringCallback = 0;
    c.forEachKeyPartition([&](std::vector<LedgerIndex> const&) {
        ++calls;
        bool acquired = false;
        std::thread probe([&] {
            acquired = c.peekMutex().try_lock();
            if (acquired)
                c.peekMutex().unlock();
        });
        probe.join();
        if (acquired)
            ++unlockedDuringCallback;
    });

    EXPECT_EQ(calls, static_cast<std::size_t>(std::thread::hardware_concurrency()));
    EXPECT_EQ(unlockedDuringCallback, calls);
}

TEST(TaggedCacheTest, for_each_key_partition_tolerates_inserts_from_another_thread)
{
    using namespace std::chrono_literals;
    beast::Journal const journal{TestSink::instance()};
    TestStopwatch clock;
    clock.set(0);

    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache c("concurrent", 0, 1s, clock, journal);

    constexpr LedgerIndex original = 5'000;
    for (LedgerIndex i = 0; i < original; ++i)
        c.insert(i, "v");

    // While one partition's keys are being handled, another thread inserts a
    // fresh key. The mutex is free during the callback, so the insert must
    // complete (a held mutex would deadlock the join), and every original key
    // must still be visited exactly once. Whether the new keys are visited
    // depends on which partition they land in, so only their count is bounded.
    std::vector<LedgerIndex> visited;
    LedgerIndex next = original;
    c.forEachKeyPartition([&](std::vector<LedgerIndex> const& keys) {
        visited.insert(visited.end(), keys.begin(), keys.end());
        std::thread inserter([&] { c.insert(next, "late"); });
        inserter.join();
        ++next;
    });

    std::ranges::sort(visited);
    EXPECT_EQ(std::ranges::adjacent_find(visited), visited.end());
    auto const originals =
        std::ranges::count_if(visited, [](LedgerIndex k) { return k < original; });
    EXPECT_EQ(static_cast<LedgerIndex>(originals), original);
    EXPECT_LE(visited.size() - static_cast<std::size_t>(originals), next - original);
    EXPECT_EQ(c.getTrackSize(), static_cast<int>(next));
}

TEST(TaggedCacheTest, for_each_key_partition_records_a_lock_hold_peak)
{
    using namespace std::chrono_literals;
    beast::Journal const journal{TestSink::instance()};
    TestStopwatch clock;
    clock.set(0);

    using Cache = TaggedCache<LedgerIndex, std::string>;
    Cache c("peak-by-partition", 0, 1s, clock, journal);
    for (LedgerIndex i = 0; i < 200'000; ++i)
        c.insert(i, "v");
    EXPECT_EQ(c.takeLockHoldPeak(), 0ns);

    c.forEachKeyPartition([](std::vector<LedgerIndex> const&) {});

    // Each partition copy is one hold; the peak is the longest of them.
    EXPECT_GT(c.takeLockHoldPeak(), 0ns);
    EXPECT_EQ(c.takeLockHoldPeak(), 0ns);
}

}  // namespace xrpl
