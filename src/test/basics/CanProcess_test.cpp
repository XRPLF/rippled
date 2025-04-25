//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/basics/CanProcess.h>
#include <xrpl/beast/unit_test.h>

namespace ripple {
namespace test {

struct CanProcess_test : beast::unit_test::suite
{
    template <class Mutex, class Collection, class Item>
    void
    test(
        std::string const& name,
        Mutex& mtx,
        Collection& collection,
        std::vector<Item> const& items)
    {
        testcase(name);

        if (!BEAST_EXPECT(!items.empty()))
            return;
        if (!BEAST_EXPECT(collection.empty()))
            return;

        // CanProcess objects can't be copied or moved. To make that easier,
        // store shared_ptrs
        std::vector<std::shared_ptr<CanProcess>> trackers;
        // Fill up the vector with two CanProcess for each Item. The first
        // inserts the item into the collection and is "good". The second does
        // not and is "bad".
        for (int i = 0; i < items.size(); ++i)
        {
            {
                auto const& good = trackers.emplace_back(
                    std::make_shared<CanProcess>(mtx, collection, items[i]));
                BEAST_EXPECT(*good);
            }
            BEAST_EXPECT(trackers.size() == (2 * i) + 1);
            BEAST_EXPECT(collection.size() == i + 1);
            {
                auto const& bad = trackers.emplace_back(
                    std::make_shared<CanProcess>(mtx, collection, items[i]));
                BEAST_EXPECT(!*bad);
            }
            BEAST_EXPECT(trackers.size() == 2 * (i + 1));
            BEAST_EXPECT(collection.size() == i + 1);
        }
        BEAST_EXPECT(collection.size() == items.size());
        // Now remove the items from the vector<CanProcess> two at a time, and
        // try to get another CanProcess for that item.
        for (int i = 0; i < items.size(); ++i)
        {
            // Remove the "bad" one in the second position
            // This will have no effect on the collection
            {
                auto const iter = trackers.begin() + 1;
                BEAST_EXPECT(!**iter);
                trackers.erase(iter);
            }
            BEAST_EXPECT(trackers.size() == (2 * items.size()) - 1);
            BEAST_EXPECT(collection.size() == items.size());
            {
                // Append a new "bad" one
                auto const& bad = trackers.emplace_back(
                    std::make_shared<CanProcess>(mtx, collection, items[i]));
                BEAST_EXPECT(!*bad);
            }
            BEAST_EXPECT(trackers.size() == 2 * items.size());
            BEAST_EXPECT(collection.size() == items.size());

            // Remove the "good" one from the front
            {
                auto const iter = trackers.begin();
                BEAST_EXPECT(**iter);
                trackers.erase(iter);
            }
            BEAST_EXPECT(trackers.size() == (2 * items.size()) - 1);
            BEAST_EXPECT(collection.size() == items.size() - 1);
            {
                // Append a new "good" one
                auto const& good = trackers.emplace_back(
                    std::make_shared<CanProcess>(mtx, collection, items[i]));
                BEAST_EXPECT(*good);
            }
            BEAST_EXPECT(trackers.size() == 2 * items.size());
            BEAST_EXPECT(collection.size() == items.size());
        }
        // Now remove them all two at a time
        for (int i = items.size() - 1; i >= 0; --i)
        {
            // Remove the "bad" one from the front
            {
                auto const iter = trackers.begin();
                BEAST_EXPECT(!**iter);
                trackers.erase(iter);
            }
            BEAST_EXPECT(trackers.size() == (2 * i) + 1);
            BEAST_EXPECT(collection.size() == i + 1);
            // Remove the "good" one now in front
            {
                auto const iter = trackers.begin();
                BEAST_EXPECT(**iter);
                trackers.erase(iter);
            }
            BEAST_EXPECT(trackers.size() == 2 * i);
            BEAST_EXPECT(collection.size() == i);
        }
        BEAST_EXPECT(trackers.empty());
        BEAST_EXPECT(collection.empty());
    }

    void
    run() override
    {
        {
            std::mutex m;
            std::set<int> collection;
            std::vector<int> const items{1, 2, 3, 4, 5};
            test("set of int", m, collection, items);
        }
        {
            std::mutex m;
            std::set<std::string> collection;
            std::vector<std::string> const items{
                "one", "two", "three", "four", "five"};
            test("set of string", m, collection, items);
        }
        {
            std::mutex m;
            std::unordered_set<char> collection;
            std::vector<char> const items{'1', '2', '3', '4', '5'};
            test("unorderd_set of char", m, collection, items);
        }
        {
            std::mutex m;
            std::unordered_set<std::uint64_t> collection;
            std::vector<std::uint64_t> const items{100u, 1000u, 150u, 4u, 0u};
            test("unordered_set of uint64_t", m, collection, items);
        }
    }
};

BEAST_DEFINE_TESTSUITE(CanProcess, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
