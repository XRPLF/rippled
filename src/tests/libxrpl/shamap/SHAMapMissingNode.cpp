#include <xrpl/shamap/SHAMapMissingNode.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapNodeID.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <gtest/gtest.h>
#include <helpers/SHAMapTestHelpers.h>
#include <helpers/TestFamily.h>
#include <helpers/TestRNG.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace xrpl;

namespace {

// Seed for the random number generator used to create the test data.
constexpr std::uint32_t kSeed = 0xdeadbeefU;

// Sync source into dest, delivering at most nodeLimit nodes requested by dest.getMissingNodes().
// This intentionally leaves parts of the tree absent to simulate missing subtrees.
void
syncWithLimit(SHAMap const& source, SHAMap& dest, std::size_t nodeLimit)
{
    // Copy over the root node from the source map to the dest map.
    std::vector<std::pair<SHAMapNodeID, Blob>> rootData;
    if (!source.getNodeFat(SHAMapNodeID{}, rootData, false, 0))
        FAIL() << "Could not get root node";
    if (!dest.addRootNode(source.getHash(), makeSlice(rootData[0].second), nullptr).isGood())
        FAIL() << "Could not add root node";

    // Fetch nodes from the source map and deliver them to the dest map until we reach the limit or
    // there are no more missing nodes.
    std::size_t delivered = 0;
    do
    {
        auto missing = dest.getMissingNodes(2048, nullptr);
        if (missing.empty())
            break;

        for (auto const& [nodeID, _] : missing)
        {
            if (delivered >= nodeLimit)
                return;

            std::vector<std::pair<SHAMapNodeID, Blob>> nodeData;
            if (!source.getNodeFat(nodeID, nodeData, false, 0))
                continue;

            for (auto const& [id, blob] : nodeData)
            {
                if (!dest.addKnownNode(id, makeSlice(blob), nullptr).isGood())
                    FAIL() << "Could not add known node";
            }

            ++delivered;
        }
    } while (true);
}

}  // namespace

// visitLeaves on a map with missing child nodes must not throw and must produce a partial
// traversal, skipping the unavailable subtrees.
TEST(SHAMapMissingNode, visitLeavesSkipsMissingNodes)
{
    auto rngEngine = test::rng(kSeed);

    beast::Journal const j{beast::Journal::getNullSink()};
    test::TestFamily sourceFamily(j);
    test::TestFamily destFamily(j);

    // Create a map and add random nodes to it.
    SHAMap source(SHAMapType::FREE, sourceFamily);
    static constexpr auto kItems = 200;
    for (auto i = 0; i < kItems; ++i)
        source.addItem(SHAMapNodeType::TnAccountState, test::makeRandomSHAMapItem(rngEngine));
    source.setImmutable();

    // Count the total number of leaf nodes in the source map, which should match the number of
    // items that were added.
    auto beforeCount = 0;
    source.visitLeaves([&beforeCount](auto const&) { ++beforeCount; });
    EXPECT_EQ(beforeCount, kItems);

    // Deliver only 3 inner nodes so most subtrees remain absent from destFamily's DB, simulating
    // child nodes evicted after a rotation.
    SHAMap dest(SHAMapType::FREE, source.getHash().asUInt256(), destFamily);
    dest.setSynching();
    ASSERT_NO_FATAL_FAILURE(syncWithLimit(source, dest, 3));

    // Count the total number of leaf nodes in the dest map. Since most subtrees are now missing,
    // the traversal will result in fewer nodes being visited. The function must not throw.
    auto afterCount = 0;
    EXPECT_NO_THROW(dest.visitLeaves([&afterCount](auto const&) { ++afterCount; }));
    EXPECT_LT(afterCount, beforeCount);
}

// visitNodes on a fully populated map must visit every node without skipping.
TEST(SHAMapMissingNode, visitNodesCompleteMap)
{
    auto rngEngine = test::rng(kSeed);

    beast::Journal const j{beast::Journal::getNullSink()};
    test::TestFamily family(j);

    // Create a map and add random nodes to it.
    SHAMap map(SHAMapType::FREE, family);
    static constexpr auto kItems = 50;
    for (auto i = 0; i < kItems; ++i)
        map.addItem(SHAMapNodeType::TnAccountState, test::makeRandomSHAMapItem(rngEngine));
    map.setImmutable();

    // There should be an identical number of leaf nodes as the number of items that were added.
    // During inserting the items, the map may have created additional inner nodes to store them.
    auto const counts = test::countNodes(map);
    EXPECT_EQ(counts.leaves, kItems);
    EXPECT_GE(counts.inner + counts.leaves, kItems);
}
