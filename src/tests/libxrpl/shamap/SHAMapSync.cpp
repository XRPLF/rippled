#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/common.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <utility>
#include <vector>

namespace xrpl::tests {

class SHAMapSyncTest : public ::testing::Test
{
protected:
    beast::Journal const j_{TestSink::instance()};
    beast::XorShiftEngine eng_;

    boost::intrusive_ptr<SHAMapItem>
    makeRandomAS()
    {
        static constexpr auto kWordsPerState = 3uz;

        Serializer s;

        for (auto word = 0uz; word < kWordsPerState; ++word)
            s.add32(randInt<std::uint32_t>(eng_));
        return makeShamapitem(s.getSHA512Half(), s.slice());
    }

    bool
    confuseMap(SHAMap& map, std::size_t count)
    {
        // add a bunch of random states to a map, then remove them
        // map should be the same
        SHAMapHash const beforeHash = map.getHash();

        std::list<UInt256> items;

        for (auto i = 0uz; i < count; ++i)
        {
            auto item = makeRandomAS();
            items.push_back(item->key());

            if (!map.addItem(SHAMapNodeType::TnAccountState, item))
            {
                ADD_FAILURE() << "Unable to add item to map";
                return false;
            }
        }

        for (auto const& item : items)
        {
            if (!map.delItem(item))
            {
                ADD_FAILURE() << "Unable to remove item from map";
                return false;
            }
        }

        if (beforeHash != map.getHash())
        {
            ADD_FAILURE() << "Hashes do not match " << beforeHash << " " << map.getHash();
            return false;
        }

        return true;
    }
};

TEST_F(SHAMapSyncTest, sync)
{
    TestNodeFamily f{j_}, f2{j_};
    SHAMap source{SHAMapType::FREE, f};
    SHAMap destination{SHAMapType::FREE, f2};

    static constexpr auto kItemCount = 10000uz;
    static constexpr auto kInvariantInterval = 100uz;
    static constexpr auto kNodesToConfuse = 500uz;
    static constexpr auto kMaxNodesPerRequest = 2048;

    for (auto i = 0uz; i < kItemCount; ++i)
    {
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAS());
        if (i % kInvariantInterval == 0)
            source.invariants();
    }

    source.invariants();
    ASSERT_TRUE(confuseMap(source, kNodesToConfuse));
    source.invariants();

    source.setImmutable();

    std::size_t count = 0;
    source.visitLeaves([&count]([[maybe_unused]] auto const& item) { ++count; });
    EXPECT_EQ(count, kItemCount);

    std::vector<SHAMapMissingNode> missingNodes;
    source.walkMap(missingNodes, kMaxNodesPerRequest);
    EXPECT_TRUE(missingNodes.empty());

    destination.setSynching();

    {
        std::vector<SHAMapNodeData> a;

        ASSERT_TRUE(source.getNodeFat(SHAMapNodeID(), a, randBool(eng_), randInt(eng_, 2)));

        ASSERT_FALSE(a.empty()) << "NodeSize";

        auto node = SHAMapTreeNode::makeFromWire(makeSlice(a[0].data));
        if (!node)
            FAIL() << "Could not create node";
        ASSERT_TRUE(destination.addRootNode(source.getHash(), std::move(node), nullptr).isGood());
    }

    do
    {
        f.clock().advance(std::chrono::seconds(1));

        // get the list of nodes we know we need
        auto nodesMissing = destination.getMissingNodes(kMaxNodesPerRequest, nullptr);

        if (nodesMissing.empty())
            break;

        // get as many nodes as possible based on this information
        std::vector<SHAMapNodeData> b;

        for (auto& it : nodesMissing)
        {
            // Keep failures fatal here because this loop is data-dependent.
            // non-deterministic number of times and the number of tests run
            // should be deterministic
            if (!source.getNodeFat(it.first, b, randBool(eng_), randInt(eng_, 2)))
                FAIL() << "Unable to fetch node";
        }

        // Keep failures fatal here because this loop is data-dependent.
        // non-deterministic number of times and the number of tests run
        // should be deterministic
        if (b.empty())
            FAIL() << "No nodes returned";

        for (auto const& i : b)
        {
            // Keep failures fatal here because this loop is data-dependent.
            // non-deterministic number of times and the number of tests run
            // should be deterministic
            auto node = SHAMapTreeNode::makeFromWire(makeSlice(i.data));
            if (!node)
                FAIL() << "Could not create node";
            if (i.isLeaf != node->isLeaf())
                FAIL() << "Node is not a leaf";
            if (!destination.addKnownNode(i.nodeID, std::move(node), nullptr).isUseful())
                FAIL() << "Known node was not useful";
        }
    } while (true);

    destination.clearSynching();

    EXPECT_TRUE(source.deepCompare(destination));

    destination.invariants();
}

// The duplicate verdict also answers for a node that did not need to be added, which is the half
// of incDuplicate()'s contract that "already held" does not cover. addKnownNode() reaches it when
// the map has stopped taking nodes, and returns there before looking at the offer at all.
//
// An A/B on one offer and two maps of the same shape, so the synching state is the only difference
// between the two verdicts. Neither map holds the offered node, so a count that meant "already
// held" would be wrong for it.
TEST_F(SHAMapSyncTest, add_known_node_reports_duplicate_once_a_map_stops_synching)
{
    TestNodeFamily f{j_};

    // A well-formed inner node with one child. Its contents do not matter: both verdicts below
    // are reached without the map reading them.
    auto const makeOffer = [] {
        Serializer s;
        s.addBitString(UInt256{1});
        s.add8(0);
        s.add8(kWireTypeCompressedInner);
        return SHAMapTreeNode::makeFromWire(makeSlice(s.peekData()));
    };
    ASSERT_TRUE(makeOffer());

    SHAMapNodeID const target{1, UInt256{}};

    // Synching, so the offer is examined, and refused because the empty root has no branch to
    // hook it onto. The point is that the map looked.
    SHAMap synching{SHAMapType::FREE, f};
    synching.setSynching();

    auto const examined = synching.addKnownNode(target, makeOffer(), nullptr);
    EXPECT_TRUE(examined.isInvalid());
    EXPECT_EQ(examined.getDuplicate(), 0);

    // The same offer to a map that has stopped taking nodes.
    SHAMap stopped{SHAMapType::FREE, f};
    stopped.setSynching();
    stopped.clearSynching();
    ASSERT_FALSE(stopped.isSynching());

    auto const notNeeded = stopped.addKnownNode(target, makeOffer(), nullptr);
    EXPECT_EQ(notNeeded.getGood(), 0);
    EXPECT_EQ(notNeeded.getBad(), 0);
    EXPECT_EQ(notNeeded.getDuplicate(), 1);
    EXPECT_TRUE(notNeeded.isGood()) << "a node that was not needed counts on the accepted side";
}

}  // namespace xrpl::tests
