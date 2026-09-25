#include <xrpl/shamap/SHAMapMissingNode.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/common.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace xrpl::tests {

namespace {

// Fixed seed, so a failure reproduces when the test runs on its own.
constexpr std::uint32_t kSeed = 0xdeadbeefU;

// Largest number of nodes getMissingNodes may report per round.
constexpr int kMaxNodesPerRequest = 2048;

/**
 * Copy part of a SHAMap into another SHAMap.
 *
 * The function answers at most nodeLimit of the requests that
 * dest.getMissingNodes() makes. The remaining subtrees stay absent from the
 * destination family's database, which is the state this test needs.
 *
 * @param source The complete map to copy from.
 * @param dest The map to copy into. It must be in the synching state.
 * @param nodeLimit The largest number of missing-node requests to answer.
 * @return Nothing. The function reports a fatal gtest failure on error, so
 *         wrap the call in ASSERT_NO_FATAL_FAILURE.
 */
void
copyPartialMap(SHAMap const& source, SHAMap& dest, std::size_t nodeLimit)
{
    std::vector<SHAMapNodeData> rootData;
    if (!source.getNodeFat(SHAMapNodeID{}, rootData, false, 0))
        FAIL() << "Could not get root node";

    auto rootNode = SHAMapTreeNode::makeFromWire(makeSlice(rootData[0].data));
    if (!rootNode)
        FAIL() << "Could not deserialize root node";
    if (!dest.addRootNode(source.getHash(), std::move(rootNode), nullptr).isGood())
        FAIL() << "Could not add root node";

    std::size_t answered = 0;
    while (answered < nodeLimit)
    {
        auto const missing = dest.getMissingNodes(kMaxNodesPerRequest, nullptr);
        if (missing.empty())
            break;

        for (auto const& request : missing)
        {
            if (answered >= nodeLimit)
                return;
            ++answered;

            std::vector<SHAMapNodeData> nodeData;
            if (!source.getNodeFat(request.first, nodeData, false, 0))
                continue;

            for (auto const& entry : nodeData)
            {
                auto node = SHAMapTreeNode::makeFromWire(makeSlice(entry.data));
                if (!node)
                    FAIL() << "Could not deserialize node " << entry.nodeID;
                if (!dest.addKnownNode(entry.nodeID, std::move(node), nullptr).isGood())
                    FAIL() << "Could not add known node " << entry.nodeID;
            }
        }
    }
}

/**
 * Lift a leaf out of a one-item map in the wire format a peer would send it in.
 *
 * A locally built map always has an inner node for its root, so this is how a
 * test reaches the one-node shape that addRootNode accepts.
 *
 * @param map A map holding exactly one item.
 * @param wire Set to the leaf's wire form.
 * @param hash Set to the leaf's hash.
 * @return Nothing. The function reports a fatal gtest failure on error, so
 *         wrap the call in ASSERT_NO_FATAL_FAILURE.
 */
void
extractSoleLeaf(SHAMap const& map, Blob& wire, SHAMapHash& hash)
{
    auto leaves = 0;
    map.visitNodes([&](SHAMapTreeNode& node) {
        if (!node.isInner())
        {
            ++leaves;
            Serializer s;
            node.serializeForWire(s);
            wire = s.modData();
            hash = node.getHash();
        }
        return true;
    });
    if (leaves != 1)
        FAIL() << "Expected one leaf, found " << leaves;
}

struct NodeCounts
{
    bool complete;
    int inner;
    int leaves;
};

/**
 * Count the inner and leaf nodes that visitNodes reaches.
 *
 * @param map The map to walk.
 * @return The counts, plus what visitNodes reported about the walk.
 */
NodeCounts
countNodes(SHAMap const& map)
{
    int inner = 0;
    int leaves = 0;
    bool const complete = map.visitNodes([&inner, &leaves](SHAMapTreeNode& node) {
        if (node.isInner())
        {
            ++inner;
        }
        else
        {
            ++leaves;
        }
        return true;
    });
    return {.complete = complete, .inner = inner, .leaves = leaves};
}

}  // namespace

// visitLeaves on a map with unreadable child nodes must not throw. It must end the walk
// at the first one and report that the walk was incomplete.
TEST(SHAMapMissingNode, visit_leaves_stops_at_an_unreadable_node)
{
    static constexpr auto kItems = 200;
    static constexpr auto kNodesToCopy = 3uz;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    // The source map is complete, so it holds one leaf per item added.
    auto beforeCount = 0;
    EXPECT_TRUE(source.visitLeaves([&beforeCount](auto const&) { ++beforeCount; }));
    EXPECT_EQ(beforeCount, kItems);

    // Copy only a few nodes, so most subtrees stay absent from destFamily's
    // database. This is the state a node reaches when a child is evicted after
    // a database rotation, or when a sync is still incomplete.
    SHAMap dest{SHAMapType::FREE, source.getHash().asUInt256(), destFamily};
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(copyPartialMap(source, dest, kNodesToCopy));

    // The walk must return, must report that it was incomplete, and must reach fewer
    // leaves than the source holds.
    auto afterCount = 0;
    auto complete = true;
    EXPECT_NO_THROW(complete = dest.visitLeaves([&afterCount](auto const&) { ++afterCount; }));
    EXPECT_FALSE(complete);
    EXPECT_LT(afterCount, beforeCount);
}

// walkMap must report the nodes it cannot read rather than throwing, so that
// Ledger::walkLedger can return false and let its caller re-acquire the ledger.
TEST(SHAMapMissingNode, walk_map_reports_missing_nodes)
{
    static constexpr auto kItems = 200;
    static constexpr auto kNodesToCopy = 3uz;
    static constexpr auto kMaxMissing = 32;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    SHAMap dest{SHAMapType::FREE, source.getHash().asUInt256(), destFamily};
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(copyPartialMap(source, dest, kNodesToCopy));

    std::vector<SHAMapMissingNode> missing;
    EXPECT_NO_THROW(dest.walkMap(missing, kMaxMissing));
    EXPECT_FALSE(missing.empty());

    // A complete map reports nothing missing.
    std::vector<SHAMapMissingNode> none;
    EXPECT_NO_THROW(source.walkMap(none, kMaxMissing));
    EXPECT_TRUE(none.empty());
}

// walkMapParallel must report a partial map as incomplete. Its workers record an
// unreadable child instead of throwing, so the result has to consult that list and not
// only the exceptions the workers caught.
TEST(SHAMapMissingNode, walk_map_parallel_reports_missing_nodes)
{
    static constexpr auto kItems = 200;
    static constexpr auto kNodesToCopy = 3uz;
    static constexpr auto kMaxMissing = 32;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    SHAMap dest{SHAMapType::FREE, source.getHash().asUInt256(), destFamily};
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(copyPartialMap(source, dest, kNodesToCopy));

    std::vector<SHAMapMissingNode> missing;
    EXPECT_FALSE(dest.walkMapParallel(missing, kMaxMissing));
    EXPECT_FALSE(missing.empty());

    // A complete map reports nothing missing.
    std::vector<SHAMapMissingNode> none;
    EXPECT_TRUE(source.walkMapParallel(none, kMaxMissing));
    EXPECT_TRUE(none.empty());
}

// An empty branch of the root holds nothing, so the parallel walk must skip it rather
// than report it missing. Three items cannot fill sixteen branches, whereas the larger
// maps in this file leave none of them empty.
TEST(SHAMapMissingNode, walk_map_parallel_skips_empty_root_branches)
{
    static constexpr auto kItems = 3;
    static constexpr auto kMaxMissing = 32;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily family{j};

    SHAMap map{SHAMapType::FREE, family};
    for (auto i = 0; i < kItems; ++i)
        map.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    map.setImmutable();

    // The root has to be inner, or the walk returns before it reads a single branch.
    auto const [complete, inner, leaves] = countNodes(map);
    EXPECT_TRUE(complete);
    EXPECT_EQ(leaves, kItems);
    EXPECT_GT(inner, 0);

    std::vector<SHAMapMissingNode> missing;
    EXPECT_TRUE(map.walkMapParallel(missing, kMaxMissing));
    EXPECT_TRUE(missing.empty());
}

// walkMapParallel reads the root's children before it starts any worker, and skips a null
// one. That pass has to record the miss itself, or a map holding nothing but its root
// reports as complete.
TEST(SHAMapMissingNode, walk_map_parallel_reports_missing_root_children)
{
    static constexpr auto kItems = 200;
    static constexpr auto kMaxMissing = 32;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    // Copy the root and nothing else, so every one of its children is unreadable and no
    // worker ever runs.
    SHAMap dest{SHAMapType::FREE, source.getHash().asUInt256(), destFamily};
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(copyPartialMap(source, dest, 0));

    std::vector<SHAMapMissingNode> missing;
    EXPECT_FALSE(dest.walkMapParallel(missing, kMaxMissing));
    EXPECT_FALSE(missing.empty());
}

// Stopping the walk through the visitor is not the same thing as an unreadable node, so it
// must not make the result false. A stop at the root counts too; the root's answer used to
// be discarded.
TEST(SHAMapMissingNode, early_stop_on_a_complete_map_reports_complete)
{
    static constexpr auto kItems = 200;
    static constexpr auto kVisitsBeforeStop = 3;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily family{j};

    SHAMap map{SHAMapType::FREE, family};
    for (auto i = 0; i < kItems; ++i)
        map.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    map.setImmutable();

    auto visits = 0;
    EXPECT_TRUE(map.visitNodes([&visits](SHAMapTreeNode&) {
        ++visits;
        return visits < kVisitsBeforeStop;
    }));
    EXPECT_EQ(visits, kVisitsBeforeStop);

    // Stopping at the root leaves the rest of the map unvisited.
    auto rootVisits = 0;
    EXPECT_TRUE(map.visitNodes([&rootVisits](SHAMapTreeNode&) {
        ++rootVisits;
        return false;
    }));
    EXPECT_EQ(rootVisits, 1);
}

// A map holding nothing but its root cannot read any branch, so the walk visits the root
// alone and reports itself incomplete.
TEST(SHAMapMissingNode, a_root_only_map_reports_incomplete)
{
    static constexpr auto kItems = 200;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    SHAMap dest{SHAMapType::FREE, source.getHash().asUInt256(), destFamily};
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(copyPartialMap(source, dest, 0));

    auto visits = 0;
    EXPECT_FALSE(dest.visitNodes([&visits](SHAMapTreeNode&) {
        ++visits;
        return true;
    }));
    EXPECT_EQ(visits, 1);
}

// visitNodes on a complete map must reach every node.
TEST(SHAMapMissingNode, visit_nodes_complete_map)
{
    static constexpr auto kItems = 50;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily family{j};

    SHAMap map{SHAMapType::FREE, family};
    for (auto i = 0; i < kItems; ++i)
        map.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    map.setImmutable();

    // The walk must report itself complete, and the map must hold one leaf per item
    // added, plus the inner nodes that hold those leaves.
    auto const [complete, inner, leaves] = countNodes(map);
    EXPECT_TRUE(complete);
    EXPECT_EQ(leaves, kItems);
    EXPECT_GT(inner, 0);
}

// The budget caps what a walk records. Once it is spent the walk stops, and the parallel
// walk spends it on the root's children before it starts a worker.
TEST(SHAMapMissingNode, a_spent_missing_node_budget_stops_the_walk)
{
    static constexpr auto kItems = 200;
    static constexpr auto kMaxMissing = 1;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    // Copy the root and nothing else, so more than one child is unreadable and the
    // budget of one runs out.
    SHAMap dest{SHAMapType::FREE, source.getHash().asUInt256(), destFamily};
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(copyPartialMap(source, dest, 0));

    std::vector<SHAMapMissingNode> serial;
    EXPECT_NO_THROW(dest.walkMap(serial, kMaxMissing));
    EXPECT_EQ(serial.size(), 1u);

    std::vector<SHAMapMissingNode> parallel;
    EXPECT_FALSE(dest.walkMapParallel(parallel, kMaxMissing));
    EXPECT_EQ(parallel.size(), 1u);
}

// A map whose root is a leaf holds one node and has it, so every walk reports it
// complete. addRootNode is the only way into that shape.
TEST(SHAMapMissingNode, a_leaf_rooted_map_reports_complete)
{
    static constexpr auto kMaxMissing = 32;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    Blob wire;
    SHAMapHash hash;
    ASSERT_NO_FATAL_FAILURE(extractSoleLeaf(source, wire, hash));

    SHAMap dest{SHAMapType::FREE, hash.asUInt256(), destFamily};
    dest.setSynching();
    auto leaf = SHAMapTreeNode::makeFromWire(makeSlice(wire));
    ASSERT_TRUE(leaf);
    ASSERT_TRUE(dest.addRootNode(hash, std::move(leaf), nullptr).isGood());

    auto visits = 0;
    EXPECT_TRUE(dest.visitNodes([&visits](SHAMapTreeNode&) {
        ++visits;
        return true;
    }));
    EXPECT_EQ(visits, 1);

    std::vector<SHAMapMissingNode> serial;
    EXPECT_NO_THROW(dest.walkMap(serial, kMaxMissing));
    EXPECT_TRUE(serial.empty());

    std::vector<SHAMapMissingNode> parallel;
    EXPECT_TRUE(dest.walkMapParallel(parallel, kMaxMissing));
    EXPECT_TRUE(parallel.empty());
}

// The missing-node budget belongs to the whole walk, not to each worker. Every worker that
// is still running shares one counter, so it has to be tested before a node is added to the
// list. Testing it only afterwards let a 16-worker walk record 17 nodes against a cap of 2.
TEST(SHAMapMissingNode, a_missing_node_budget_is_shared_by_every_worker)
{
    static constexpr auto kItems = 200;
    static constexpr auto kMaxMissing = 2;

    // Enough to make all sixteen of the root's children readable inner nodes, so sixteen
    // workers start and each one finds unreadable children below its own.
    static constexpr auto kNodesToCopy = 16uz;

    beast::Journal const j{TestSink::instance()};
    beast::xor_shift_engine engine{kSeed};
    TestNodeFamily sourceFamily{j};
    TestNodeFamily destFamily{j};

    SHAMap source{SHAMapType::FREE, sourceFamily};
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, makeRandomAccountStateItem(engine));
    source.setImmutable();

    SHAMap dest{SHAMapType::FREE, source.getHash().asUInt256(), destFamily};
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(copyPartialMap(source, dest, kNodesToCopy));

    std::vector<SHAMapMissingNode> missing;
    EXPECT_FALSE(dest.walkMapParallel(missing, kMaxMissing));
    EXPECT_FALSE(missing.empty());
    EXPECT_LE(missing.size(), static_cast<std::size_t>(kMaxMissing));
}

}  // namespace xrpl::tests
