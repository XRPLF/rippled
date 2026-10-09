#include <xrpl/basics/TaggedCache.h>

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusiveRefCounts.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace xrpl {

struct TaggedCacheTestBase : public ::testing::Test
{
    using Key = LedgerIndex;

    // A single `++clock` plus a sweep is enough to age any entry out.
    static constexpr std::chrono::seconds kExpiration{1};
    static constexpr int kTargetSize = 1;

    beast::Journal const journal{TestSink::instance()};
    TestStopwatch clock;  ///< ManualClock starts at zero
};

struct TaggedCacheTest : public TaggedCacheTestBase
{
    using Value = std::string;
    using Cache = TaggedCache<Key, Value>;

    Cache cache{"test", kTargetSize, kExpiration, clock, journal};
};

TEST_F(TaggedCacheTest, insert_then_retrieve)
{
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);

    EXPECT_FALSE(cache.insert(1, "one"));
    EXPECT_EQ(cache.getCacheSize(), 1);
    EXPECT_EQ(cache.getTrackSize(), 1);

    std::string retrieved;
    EXPECT_TRUE(cache.retrieve(1, retrieved));
    EXPECT_EQ(retrieved, "one");
}

TEST_F(TaggedCacheTest, sweep_purges_an_aged_entry)
{
    EXPECT_FALSE(cache.insert(1, "one"));

    ++clock;
    cache.sweep();

    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);
}

TEST_F(TaggedCacheTest, sweep_keeps_tracking_an_entry_a_caller_still_holds)
{
    EXPECT_FALSE(cache.insert(2, "two"));
    EXPECT_EQ(cache.getCacheSize(), 1);
    EXPECT_EQ(cache.getTrackSize(), 1);

    {
        // Scope is load-bearing: the entry survives the sweep only while this
        // pointer is alive.
        auto held = cache.fetch(2);
        EXPECT_NE(held, nullptr);

        ++clock;
        cache.sweep();
        EXPECT_EQ(cache.getCacheSize(), 0);
        EXPECT_EQ(cache.getTrackSize(), 1);
    }

    ++clock;
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);
}

TEST_F(TaggedCacheTest, canonicalize_replace_client_hands_back_the_cached_object)
{
    EXPECT_FALSE(cache.insert(3, "three"));

    {
        // Scope is load-bearing: both pointers must die before the sweep below.
        auto const cached = cache.fetch(3);
        auto candidate = std::make_shared<Value>("three");
        cache.canonicalizeReplaceClient(3, candidate);

        EXPECT_EQ(cached.get(), candidate.get());
    }

    // Canonicalizing left no lingering reference, so the entry still ages out.
    ++clock;
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);
}

TEST_F(TaggedCacheTest, canonicalize_replace_client_revives_a_swept_entry)
{
    EXPECT_FALSE(cache.insert(4, "four"));
    EXPECT_EQ(cache.getCacheSize(), 1);
    EXPECT_EQ(cache.getTrackSize(), 1);

    {
        // Scope is load-bearing: the final sweep must see nothing held.
        auto const held = cache.fetch(4);
        EXPECT_NE(held, nullptr);
        // Fetching does not disturb the counts.
        EXPECT_EQ(cache.getCacheSize(), 1);
        EXPECT_EQ(cache.getTrackSize(), 1);

        ++clock;
        cache.sweep();
        EXPECT_EQ(cache.getCacheSize(), 0);
        EXPECT_EQ(cache.getTrackSize(), 1);

        auto candidate = std::make_shared<Value>("four");
        EXPECT_TRUE(cache.canonicalizeReplaceClient(4, candidate));
        EXPECT_EQ(cache.getCacheSize(), 1);
        EXPECT_EQ(cache.getTrackSize(), 1);

        EXPECT_EQ(held.get(), candidate.get());
    }

    ++clock;
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.getTrackSize(), 0);
}

TEST_F(TaggedCacheTest, canonicalize_replace_cache_installs_the_new_object)
{
    EXPECT_FALSE(cache.insert(5, "five"));
    EXPECT_EQ(cache.getCacheSize(), 1);
    EXPECT_EQ(cache.size(), 1);

    {
        // Scope is load-bearing: the final sweep must see nothing held.
        auto const held = cache.fetch(5);
        EXPECT_NE(held, nullptr);
        // Fetching does not disturb the counts.
        EXPECT_EQ(cache.getCacheSize(), 1);
        EXPECT_EQ(cache.size(), 1);

        ++clock;
        cache.sweep();
        EXPECT_EQ(cache.getCacheSize(), 0);
        EXPECT_EQ(cache.size(), 1);

        auto replacement = std::make_shared<Value>("five_2");
        EXPECT_TRUE(cache.canonicalizeReplaceCache(5, replacement));
        EXPECT_EQ(cache.getCacheSize(), 1);
        EXPECT_EQ(cache.size(), 1);

        // Unlike ReplaceClient, the caller's object wins and the old one is
        // left untouched in the caller's hands.
        EXPECT_NE(held.get(), replacement.get());
        EXPECT_EQ(*replacement, "five_2");

        auto const refetched = cache.fetch(5);
        EXPECT_NE(refetched, nullptr);
        EXPECT_EQ(refetched.get(), replacement.get());
        EXPECT_NE(refetched.get(), held.get());
    }

    ++clock;
    cache.sweep();
    EXPECT_EQ(cache.getCacheSize(), 0);
    EXPECT_EQ(cache.size(), 0);
}

namespace {

struct TestRefCountObject : IntrusiveRefCounts
{
    std::string data;

    // Needed to support weak intrusive pointers
    virtual void
    partialDestructor()
    {
    }

    TestRefCountObject() = default;
    explicit TestRefCountObject(std::string data) : data(std::move(data))
    {
    }

    bool
    operator==(std::string const& other) const
    {
        return data == other;
    }
};

}  // namespace

struct IntrusiveTaggedCacheTest : public TaggedCacheTestBase
{
    using IntrPtrCache = TaggedCache<
        Key,
        TestRefCountObject,
        /*IsKeyCache*/ false,
        intr_ptr::SharedWeakUnionPtr<TestRefCountObject>,
        intr_ptr::SharedPtr<TestRefCountObject>>;

    IntrPtrCache intrPtrCache{"IntrPtrTest", kTargetSize, kExpiration, clock, journal};
};

TEST_F(IntrusiveTaggedCacheTest, canonicalize_replace_cache_replaces_the_entry)
{
    intrPtrCache.canonicalizeReplaceCache(1, intr_ptr::makeShared<TestRefCountObject>("one"));
    EXPECT_EQ(intrPtrCache.getCacheSize(), 1);
    EXPECT_EQ(intrPtrCache.size(), 1);

    intrPtrCache.canonicalizeReplaceCache(
        1, intr_ptr::makeShared<TestRefCountObject>("one_replaced"));
    EXPECT_EQ(*intrPtrCache.fetch(1), "one_replaced");
}

TEST_F(IntrusiveTaggedCacheTest, canonicalize_replace_cache_replaces_a_swept_entry)
{
    intrPtrCache.canonicalizeReplaceCache(1, intr_ptr::makeShared<TestRefCountObject>("one"));

    // Load-bearing: without this the sweep drops the entry and size() is 0.
    auto const held = intrPtrCache.fetch(1);

    ++clock;
    intrPtrCache.sweep();
    EXPECT_EQ(intrPtrCache.getCacheSize(), 0);
    EXPECT_EQ(intrPtrCache.size(), 1);

    intrPtrCache.canonicalizeReplaceCache(
        1, intr_ptr::makeShared<TestRefCountObject>("one_replaced_2"));
    EXPECT_EQ(*intrPtrCache.fetch(1), "one_replaced_2");
}

TEST_F(IntrusiveTaggedCacheTest, an_entry_can_be_reinserted_after_del)
{
    intrPtrCache.canonicalizeReplaceCache(1, intr_ptr::makeShared<TestRefCountObject>("one"));

    // Nothing else holds the entry, so del() drops it from the map too.
    EXPECT_TRUE(intrPtrCache.del(1, true));
    EXPECT_EQ(intrPtrCache.getCacheSize(), 0);
    EXPECT_EQ(intrPtrCache.size(), 0);

    intrPtrCache.canonicalizeReplaceCache(
        1, intr_ptr::makeShared<TestRefCountObject>("one_replaced_3"));
    EXPECT_EQ(*intrPtrCache.fetch(1), "one_replaced_3");
}

TEST_F(IntrusiveTaggedCacheTest, an_expired_weak_entry_is_replaced_after_del)
{
    intrPtrCache.canonicalizeReplaceCache(1, intr_ptr::makeShared<TestRefCountObject>("one"));
    {
        // While held, del() only demotes the entry to a weak pointer, so it
        // stays in the map and expires once this scope releases it.
        auto const held = intrPtrCache.fetch(1);
        EXPECT_TRUE(intrPtrCache.del(1, true));
        EXPECT_EQ(intrPtrCache.getCacheSize(), 0);
        EXPECT_EQ(intrPtrCache.size(), 1);
    }

    EXPECT_FALSE(intrPtrCache.canonicalizeReplaceCache(
        1, intr_ptr::makeShared<TestRefCountObject>("one_replaced")));
    EXPECT_EQ(intrPtrCache.getCacheSize(), 1);
    EXPECT_EQ(intrPtrCache.size(), 1);
    EXPECT_EQ(*intrPtrCache.fetch(1), "one_replaced");
}

TEST_F(IntrusiveTaggedCacheTest, sweep_empties_the_cache_once_nothing_is_held)
{
    intrPtrCache.canonicalizeReplaceCache(1, intr_ptr::makeShared<TestRefCountObject>("one"));

    ++clock;
    intrPtrCache.sweep();
    EXPECT_EQ(intrPtrCache.getCacheSize(), 0);
    EXPECT_EQ(intrPtrCache.size(), 0);
}

TEST_F(IntrusiveTaggedCacheTest, fetch_and_modify_supports_intrusive_pointers)
{
    // Miss: the placeholder is built with intr_ptr::makeShared and installed.
    intrPtrCache.fetchAndModify(1, [](TestRefCountObject& v) { v.data = "one"; });
    EXPECT_EQ(intrPtrCache.getCacheSize(), 1);
    EXPECT_EQ(intrPtrCache.getTrackSize(), 1);

    auto const held = intrPtrCache.fetch(1);
    ASSERT_NE(held, nullptr);
    EXPECT_EQ(*held, "one");

    // Hit: the cached object is mutated in place, no new entry.
    intrPtrCache.fetchAndModify(1, [](TestRefCountObject& v) { v.data += "_modified"; });
    EXPECT_EQ(*held, "one_modified");
    EXPECT_EQ(intrPtrCache.getCacheSize(), 1);
    EXPECT_EQ(intrPtrCache.fetch(1).get(), held.get());
}

TEST_F(TaggedCacheTest, fetch_and_modify)
{
    using namespace std::chrono_literals;
    beast::Journal const journal{TestSink::instance()};

    TestStopwatch clock;
    clock.set(0);

    struct MutableValue
    {
        int counter = 0;
        std::string name;
    };

    using Key = LedgerIndex;
    using MutCache = TaggedCache<Key, MutableValue>;

    MutCache mc("mutable_test", 2, 2s, clock, journal);

    // A. Insert on miss: fetchAndModify creates entry and mutates it.
    mc.fetchAndModify(5, [](MutableValue& v) {
        v.counter = 42;
        v.name = "initial";
    });

    EXPECT_EQ(mc.getCacheSize(), 1);
    EXPECT_EQ(mc.getTrackSize(), 1);

    // Verify the mutation persisted.
    auto p1 = mc.fetch(5);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1->counter, 42);
    EXPECT_EQ(p1->name, "initial");

    // Keep a second strong pointer to verify in-place modification.
    auto p2 = mc.fetch(5);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p1.get(), p2.get());  // Same object

    // B. Modify existing object on hit.
    mc.fetchAndModify(5, [](MutableValue& v) {
        v.counter += 10;
        v.name = "modified";
    });

    // No new entry was created.
    EXPECT_EQ(mc.getCacheSize(), 1);
    EXPECT_EQ(mc.getTrackSize(), 1);

    // The same object was mutated (both strong pointers see the change).
    EXPECT_EQ(p1->counter, 52);
    EXPECT_EQ(p1->name, "modified");
    EXPECT_EQ(p2->counter, 52);

    // Fresh fetch returns the same object identity.
    auto p3 = mc.fetch(5);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(p3.get(), p1.get());
    EXPECT_EQ(p3->counter, 52);
}

}  // namespace xrpl
