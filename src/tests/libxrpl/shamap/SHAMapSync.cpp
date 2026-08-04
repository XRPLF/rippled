#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapLeafNode.h>
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
    beast::xor_shift_engine eng_;

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

        std::list<uint256> items;

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

// `visitDifferences` walks this map and reports only the nodes the other map does not already
// have, which is how a fetch pack is assembled (see LedgerMaster's populateFetchPack). It decides
// what to skip by asking `hasInnerNode` and `hasLeafNode`, both private, so these tests are the
// only way to reach them.
//
// Both answers matter to a peer waiting on the pack. Reporting a node it already has wastes space
// in a size-limited message; skipping one it does not have leaves it unable to complete the
// ledger. The tests below therefore check exactly which nodes come back, not just how many.

// Node hashes are computed on demand, and `visitDifferences` returns early while the root hash is
// still zero, so a map has to be hashed before it can be compared against another.
static void
finalize(SHAMap& map)
{
    map.setImmutable();
    ASSERT_FALSE(map.getHash().isZero());
}

TEST_F(SHAMapSyncTest, visit_differences_reports_only_what_is_missing)
{
    TestNodeFamily f{j_};

    // Enough shared items that they build inner nodes of their own: the walk then has whole
    // matching subtrees to skip, which is what hasInnerNode decides. With only a couple of shared
    // leaves the tree is too shallow for that path to be taken at all.
    std::vector<boost::intrusive_ptr<SHAMapItem>> shared;
    shared.reserve(64);
    for (int i = 0; i < 64; ++i)
    {
        shared.push_back(makeRandomAS());
    }

    auto const extra = makeRandomAS();

    SHAMap have{SHAMapType::FREE, f};
    for (auto const& item : shared)
    {
        ASSERT_TRUE(have.addItem(SHAMapNodeType::TnAccountState, item));
    }
    finalize(have);

    SHAMap want{SHAMapType::FREE, f};
    for (auto const& item : shared)
    {
        ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, item));
    }
    ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, extra));
    finalize(want);

    std::vector<uint256> leaves;
    std::size_t inners = 0;
    want.visitDifferences(&have, [&leaves, &inners](SHAMapTreeNode const& node) {
        if (node.isLeaf())
        {
            leaves.push_back(safeDowncast<SHAMapLeafNode const&>(node).peekItem()->key());
        }
        else
        {
            ++inners;
        }
        return true;
    });

    // Every shared leaf is already on the far side, so only `extra` is worth sending.
    EXPECT_EQ(leaves, std::vector<uint256>{extra->key()});

    // The inner nodes on `extra`'s path are reported, but the matching subtrees are skipped, so
    // the walk must not have visited every inner node in the tree.
    std::size_t allInners = 0;
    want.visitNodes([&allInners](SHAMapTreeNode& node) {
        if (!node.isLeaf())
        {
            ++allInners;
        }
        return true;
    });
    EXPECT_GT(inners, 0u);
    EXPECT_LT(inners, allInners);
}

TEST_F(SHAMapSyncTest, visit_differences_against_identical_map_reports_nothing)
{
    TestNodeFamily f{j_};

    auto const item = makeRandomAS();

    SHAMap have{SHAMapType::FREE, f};
    ASSERT_TRUE(have.addItem(SHAMapNodeType::TnAccountState, item));
    finalize(have);

    SHAMap want{SHAMapType::FREE, f};
    ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, item));
    finalize(want);
    ASSERT_EQ(want.getHash(), have.getHash());

    std::size_t visited = 0;
    want.visitDifferences(&have, [&visited]([[maybe_unused]] SHAMapTreeNode const& node) {
        ++visited;
        return true;
    });

    EXPECT_EQ(visited, 0u);
}

TEST_F(SHAMapSyncTest, visit_differences_against_no_map_reports_every_node)
{
    TestNodeFamily f{j_};

    SHAMap want{SHAMapType::FREE, f};
    for (int i = 0; i < 32; ++i)
    {
        ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, makeRandomAS()));
    }
    finalize(want);

    // A null `have` means the far side holds nothing, so every node counts as missing. Compared
    // against visitNodes, which walks the same tree with no such filtering.
    std::size_t differences = 0;
    want.visitDifferences(nullptr, [&differences]([[maybe_unused]] SHAMapTreeNode const& node) {
        ++differences;
        return true;
    });

    std::size_t all = 0;
    want.visitNodes([&all]([[maybe_unused]] SHAMapTreeNode& node) {
        ++all;
        return true;
    });

    EXPECT_GT(differences, 0u);
    EXPECT_EQ(differences, all);
}

TEST_F(SHAMapSyncTest, visit_differences_stops_when_callback_returns_false)
{
    TestNodeFamily f{j_};

    SHAMap want{SHAMapType::FREE, f};
    for (int i = 0; i < 32; ++i)
    {
        ASSERT_TRUE(want.addItem(SHAMapNodeType::TnAccountState, makeRandomAS()));
    }
    finalize(want);

    // Returning false is how populateFetchPack stops once the pack is full, so the walk must
    // honour it rather than visiting the rest of the tree.
    std::size_t visited = 0;
    want.visitDifferences(nullptr, [&visited]([[maybe_unused]] SHAMapTreeNode const& node) {
        ++visited;
        return visited < 3;
    });

    EXPECT_EQ(visited, 3u);
}

}  // namespace xrpl::tests
