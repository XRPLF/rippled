//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/xor_shift_engine.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/shamap/SHAMapItem.h>
#include <test/shamap/common.h>
#include <test/unit_test/SuiteJournal.h>

namespace ripple {
namespace tests {

class SHAMapSync_test : public beast::unit_test::suite
{
public:
    beast::xor_shift_engine eng_;

    std::shared_ptr<SHAMapItem>
    makeRandomAS()
    {
        Serializer s;

        for (int d = 0; d < 3; ++d)
            s.add32(rand_int<std::uint32_t>(eng_));
        return std::make_shared<SHAMapItem>(s.getSHA512Half(), s.slice());
    }

    bool
    confuseMap(SHAMap& map, int count)
    {
        // add a bunch of random states to a map, then remove them
        // map should be the same
        SHAMapHash beforeHash = map.getHash();

        std::list<uint256> items;

        for (int i = 0; i < count; ++i)
        {
            std::shared_ptr<SHAMapItem> item = makeRandomAS();
            items.push_back(item->key());

            if (!map.addItem(SHAMapNodeType::tnACCOUNT_STATE, std::move(*item)))
            {
                log << "Unable to add item to map\n";
                return false;
            }
        }

        for (auto const& item : items)
        {
            if (!map.delItem(item))
            {
                log << "Unable to remove item from map\n";
                return false;
            }
        }

        if (beforeHash != map.getHash())
        {
            log << "Hashes do not match " << beforeHash << " " << map.getHash()
                << std::endl;
            return false;
        }

        return true;
    }

    void
    run() override
    {
        using namespace beast::severities;
        test::SuiteJournal journal("SHAMapSync_test", *this);

        TestNodeFamily f(journal), f2(journal);
        SHAMap source(SHAMapType::FREE, f);
        SHAMap destination(SHAMapType::FREE, f2);

        int items = 10000;
        for (int i = 0; i < items; ++i)
        {
            source.addItem(
                SHAMapNodeType::tnACCOUNT_STATE, std::move(*makeRandomAS()));
            if (i % 100 == 0)
                source.invariants();
        }

        source.invariants();
        BEAST_EXPECT(confuseMap(source, 500));
        source.invariants();

        source.setImmutable();

        int count = 0;
        source.visitLeaves([&count](auto const& item) { ++count; });
        BEAST_EXPECT(count == items);

        std::vector<SHAMapMissingNode> missingNodes;
        source.walkMap(missingNodes, 2048);
        BEAST_EXPECT(missingNodes.empty());

        std::vector<SHAMapNodeID> nodeIDs, gotNodeIDs;
        std::vector<Blob> gotNodes;
        std::vector<uint256> hashes;

        destination.setSynching();

        {
            std::vector<std::pair<SHAMapNodeID, Blob>> a;

            BEAST_EXPECT(source.getNodeFat(
                SHAMapNodeID(), a, rand_bool(eng_), rand_int(eng_, 2)));

            unexpected(a.size() < 1, "NodeSize");

            BEAST_EXPECT(
                destination
                    .addRootNode(
                        source.getHash(), makeSlice(a[0].second), nullptr)
                    .isGood());
        }

        do
        {
            f.clock().advance(std::chrono::seconds(1));

            // get the list of nodes we know we need
            auto nodesMissing = destination.getMissingNodes(2048, nullptr);

            if (nodesMissing.empty())
                break;

            // get as many nodes as possible based on this information
            std::vector<std::pair<SHAMapNodeID, Blob>> b;

            for (auto& it : nodesMissing)
            {
                // Don't use BEAST_EXPECT here b/c it will be called a
                // non-deterministic number of times and the number of tests run
                // should be deterministic
                if (!source.getNodeFat(
                        it.first, b, rand_bool(eng_), rand_int(eng_, 2)))
                    fail("", __FILE__, __LINE__);
            }

            // Don't use BEAST_EXPECT here b/c it will be called a
            // non-deterministic number of times and the number of tests run
            // should be deterministic
            if (b.empty())
                fail("", __FILE__, __LINE__);

            for (std::size_t i = 0; i < b.size(); ++i)
            {
                // Don't use BEAST_EXPECT here b/c it will be called a
                // non-deterministic number of times and the number of tests run
                // should be deterministic
                if (!destination
                         .addKnownNode(
                             b[i].first, makeSlice(b[i].second), nullptr)
                         .isUseful())
                    fail("", __FILE__, __LINE__);
            }
        } while (true);

        destination.clearSynching();

        BEAST_EXPECT(source.deepCompare(destination));

        destination.invariants();
    }
};

BEAST_DEFINE_TESTSUITE(SHAMapSync, shamap, ripple);

}  // namespace tests
}  // namespace ripple
