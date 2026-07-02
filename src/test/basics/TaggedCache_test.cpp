#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusiveRefCounts.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Protocol.h>

#include <memory>
#include <utility>

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

class TaggedCache_test : public beast::unit_test::Suite
{
public:
    // Mutable value type for testing fetchAndModify
    struct MutableValue
    {
        int counter = 0;
        std::string name;
    };

    void
    run() override
    {
        using namespace std::chrono_literals;
        using beast::Severity;
        test::SuiteJournal journal("TaggedCache_test", *this);

        TestStopwatch clock;
        clock.set(0);

        using Key = LedgerIndex;
        using Value = std::string;
        using Cache = TaggedCache<Key, Value>;

        Cache c("test", 1, 1s, clock, journal);

        // Insert an item, retrieve it, and age it so it gets purged.
        {
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
            BEAST_EXPECT(!c.insert(1, "one"));
            BEAST_EXPECT(c.getCacheSize() == 1);
            BEAST_EXPECT(c.getTrackSize() == 1);

            {
                std::string s;
                BEAST_EXPECT(c.retrieve(1, s));
                BEAST_EXPECT(s == "one");
            }

            ++clock;
            c.sweep();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
        }

        // Insert an item, maintain a strong pointer, age it, and
        // verify that the entry still exists.
        {
            BEAST_EXPECT(!c.insert(2, "two"));
            BEAST_EXPECT(c.getCacheSize() == 1);
            BEAST_EXPECT(c.getTrackSize() == 1);

            {
                auto p = c.fetch(2);
                BEAST_EXPECT(p != nullptr);
                ++clock;
                c.sweep();
                BEAST_EXPECT(c.getCacheSize() == 0);
                BEAST_EXPECT(c.getTrackSize() == 1);
            }

            // Make sure its gone now that our reference is gone
            ++clock;
            c.sweep();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
        }

        // Insert the same key/value pair and make sure we get the same result
        {
            BEAST_EXPECT(!c.insert(3, "three"));

            {
                auto const p1 = c.fetch(3);
                auto p2 = std::make_shared<Value>("three");
                c.canonicalizeReplaceClient(3, p2);
                BEAST_EXPECT(p1.get() == p2.get());
            }
            ++clock;
            c.sweep();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
        }

        // Put an object in but keep a strong pointer to it, advance the clock a
        // lot, then canonicalize a new object with the same key, make sure you
        // get the original object.
        {
            // Put an object in
            BEAST_EXPECT(!c.insert(4, "four"));
            BEAST_EXPECT(c.getCacheSize() == 1);
            BEAST_EXPECT(c.getTrackSize() == 1);

            {
                // Keep a strong pointer to it
                auto const p1 = c.fetch(4);
                BEAST_EXPECT(p1 != nullptr);
                BEAST_EXPECT(c.getCacheSize() == 1);
                BEAST_EXPECT(c.getTrackSize() == 1);
                // Advance the clock a lot
                ++clock;
                c.sweep();
                BEAST_EXPECT(c.getCacheSize() == 0);
                BEAST_EXPECT(c.getTrackSize() == 1);
                // Canonicalize a new object with the same key
                auto p2 = std::make_shared<std::string>("four");
                BEAST_EXPECT(c.canonicalizeReplaceClient(4, p2));
                BEAST_EXPECT(c.getCacheSize() == 1);
                BEAST_EXPECT(c.getTrackSize() == 1);
                // Make sure we get the original object
                BEAST_EXPECT(p1.get() == p2.get());
            }

            ++clock;
            c.sweep();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.getTrackSize() == 0);
        }

        // Test fetchAndModify: insert on miss, modify on hit
        {
            using MutCache = TaggedCache<Key, MutableValue>;
            MutCache mc("mutable_test", 2, 2s, clock, journal);

            // A. Insert on miss: fetchAndModify creates entry and mutates it
            mc.fetchAndModify(5, [](MutableValue& v) {
                v.counter = 42;
                v.name = "initial";
            });

            BEAST_EXPECT(mc.getCacheSize() == 1);
            BEAST_EXPECT(mc.getTrackSize() == 1);

            // Verify the mutation persisted
            auto p1 = mc.fetch(5);
            BEAST_EXPECT(p1 != nullptr);
            BEAST_EXPECT(p1->counter == 42);
            BEAST_EXPECT(p1->name == "initial");

            // B. Modify existing object on hit
            // Keep strong pointer to verify in-place modification
            auto p2 = mc.fetch(5);
            BEAST_EXPECT(p2 != nullptr);
            BEAST_EXPECT(p1.get() == p2.get());  // Same object

            // Modify through fetchAndModify
            mc.fetchAndModify(5, [](MutableValue& v) {
                v.counter += 10;
                v.name = "modified";
            });

            // Verify no new entry was created
            BEAST_EXPECT(mc.getCacheSize() == 1);
            BEAST_EXPECT(mc.getTrackSize() == 1);

            // Verify the same object was mutated (strong pointer sees change)
            BEAST_EXPECT(p1->counter == 52);
            BEAST_EXPECT(p1->name == "modified");
            BEAST_EXPECT(p2->counter == 52);  // Original pointer sees mutation

            // Verify via fresh fetch
            auto p3 = mc.fetch(5);
            BEAST_EXPECT(p3 != nullptr);
            BEAST_EXPECT(p3.get() == p1.get());  // Same object identity
            BEAST_EXPECT(p3->counter == 52);
        }

        {
            BEAST_EXPECT(!c.insert(5, "five"));
            BEAST_EXPECT(c.getCacheSize() == 1);
            BEAST_EXPECT(c.size() == 1);

            {
                auto const p1 = c.fetch(5);
                BEAST_EXPECT(p1 != nullptr);
                BEAST_EXPECT(c.getCacheSize() == 1);
                BEAST_EXPECT(c.size() == 1);

                // Advance the clock a lot
                ++clock;
                c.sweep();
                BEAST_EXPECT(c.getCacheSize() == 0);
                BEAST_EXPECT(c.size() == 1);

                auto p2 = std::make_shared<std::string>("five_2");
                BEAST_EXPECT(c.canonicalizeReplaceCache(5, p2));
                BEAST_EXPECT(c.getCacheSize() == 1);
                BEAST_EXPECT(c.size() == 1);
                // Make sure the caller's original pointer is unchanged
                BEAST_EXPECT(p1.get() != p2.get());
                BEAST_EXPECT(*p2 == "five_2");

                auto const p3 = c.fetch(5);
                BEAST_EXPECT(p3 != nullptr);
                BEAST_EXPECT(p3.get() == p2.get());
                BEAST_EXPECT(p3.get() != p1.get());
            }

            ++clock;
            c.sweep();
            BEAST_EXPECT(c.getCacheSize() == 0);
            BEAST_EXPECT(c.size() == 0);
        }

        {
            testcase("intrptr");

            struct MyRefCountObject : IntrusiveRefCounts
            {
                std::string data;

                // Needed to support weak intrusive pointers
                virtual void
                partialDestructor() {};

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
            BEAST_EXPECT(intrPtrCache.getCacheSize() == 1);
            BEAST_EXPECT(intrPtrCache.size() == 1);

            {
                {
                    intrPtrCache.canonicalizeReplaceCache(
                        1, intr_ptr::makeShared<MyRefCountObject>("one_replaced"));

                    auto p = intrPtrCache.fetch(1);
                    BEAST_EXPECT(*p == "one_replaced");

                    // Advance the clock a lot
                    ++clock;
                    intrPtrCache.sweep();
                    BEAST_EXPECT(intrPtrCache.getCacheSize() == 0);
                    BEAST_EXPECT(intrPtrCache.size() == 1);

                    intrPtrCache.canonicalizeReplaceCache(
                        1, intr_ptr::makeShared<MyRefCountObject>("one_replaced_2"));

                    auto p2 = intrPtrCache.fetch(1);
                    BEAST_EXPECT(*p2 == "one_replaced_2");

                    intrPtrCache.del(1, true);
                }

                intrPtrCache.canonicalizeReplaceCache(
                    1, intr_ptr::makeShared<MyRefCountObject>("one_replaced_3"));
                auto p3 = intrPtrCache.fetch(1);
                BEAST_EXPECT(*p3 == "one_replaced_3");
            }

            ++clock;
            intrPtrCache.sweep();
            BEAST_EXPECT(intrPtrCache.getCacheSize() == 0);
            BEAST_EXPECT(intrPtrCache.size() == 0);
        }
    }
};

BEAST_DEFINE_TESTSUITE(TaggedCache, basics, xrpl);

}  // namespace xrpl
