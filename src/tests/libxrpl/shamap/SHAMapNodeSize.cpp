#include <xrpl/basics/Blob.h>
#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/IntrusivePointer.ipp>  // IWYU pragma: keep
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/shamap/SHAMapAccountStateLeafNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/shamap/SHAMapTxLeafNode.h>
#include <xrpl/shamap/SHAMapTxPlusMetaLeafNode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace xrpl {

// Build an inner node with exactly n populated branches via the full wire format round-trip so we
// get a properly initialized SHAMapInnerNode.
static SHAMapTreeNodePtr
makeInnerWithBranches(unsigned int n)
{
    std::vector<std::uint8_t> buf(SHAMapInnerNode::kBranchFactor * uint256::kBytes, 0);
    for (unsigned int i = 0; i < n; ++i)
    {
        std::uint8_t* p = buf.data() + (i * uint256::kBytes);
        std::fill(p, p + uint256::kBytes, static_cast<std::uint8_t>(i + 1));
    }

    return SHAMapInnerNode::makeFullInner(Slice(buf.data(), buf.size()), SHAMapHash{}, false);
}

// Verifies sizeForWire() for every branch count from 1 to 16, which covers the compressed format
// (< kCompressedThreshold), the threshold boundary, and the full format (>= kCompressedThreshold).
TEST(SHAMapNodeSize, InnerNode)
{
    // Concrete size spot-checks at key boundary points (manually verified):
    //   compressed (n=1):  1*(32+1)+1 = 34
    //   compressed (n=11): 11*(32+1)+1 = 364  (last compressed)
    //   full (n=12):       16*32+1 = 513       (first full)
    //   full (n=16):       16*32+1 = 513
    static std::map<unsigned int, std::size_t> const kSizeCheckpoints = {
        {1, 34},
        {11, 364},
        {12, 513},
        {16, 513},
    };

    for (unsigned int n = 1; n <= SHAMapInnerNode::kBranchFactor; ++n)
    {
        auto const node = makeInnerWithBranches(n);
        auto const sz = node->sizeForWire();

        if (auto it = kSizeCheckpoints.find(n); it != kSizeCheckpoints.end())
        {
            EXPECT_EQ(sz, it->second) << "branch count: " << n;
        }

        // Sentinel byte catches overruns in serializeForWire() independently of sizeForWire().
        Blob buf(sz + 1, 0xCD);
        node->serializeForWire(buf.data());
        EXPECT_EQ(buf[sz], 0xCD) << "branch count: " << n;
        EXPECT_EQ(
            buf[sz - 1],
            (n < SHAMapInnerNode::kCompressedThreshold) ? kWireTypeCompressedInner : kWireTypeInner)
            << "branch count: " << n;

        if (n < SHAMapInnerNode::kCompressedThreshold)
        {
            // makeInnerWithBranches populates branches 0..n-1 in order. Each compressed entry is
            // [32-byte hash][1-byte branch index], so verify each branch index byte.
            for (unsigned int i = 0; i < n; ++i)
            {
                EXPECT_EQ(buf[i * (uint256::kBytes + 1) + uint256::kBytes], i)
                    << "branch count: " << n;
            }
        }
    }
}

// Verifies sizeForWire() for each leaf node type across a range of payload sizes.
TEST(SHAMapNodeSize, LeafNodes)
{
    for (std::size_t const payloadSize : {12u, 64u, 256u})
    {
        uint256 const key{};
        std::vector<std::uint8_t> const payload(payloadSize, 0xAB);
        Slice const data(payload.data(), payload.size());
        auto item = makeShamapitem(key, data);

        auto const checkNode = [&](auto const& node, std::size_t expectedSize, std::uint8_t tag) {
            auto const sz = node->sizeForWire();
            EXPECT_EQ(sz, expectedSize) << "payload: " << payloadSize;
            Blob buf(sz + 1, 0xCD);
            node->serializeForWire(buf.data());
            EXPECT_EQ(buf[sz], 0xCD) << "payload: " << payloadSize;
            EXPECT_EQ(buf[sz - 1], tag) << "payload: " << payloadSize;
        };

        checkNode(
            intr_ptr::makeShared<SHAMapTxLeafNode>(item, 1),
            payloadSize + sizeof(std::uint8_t),
            kWireTypeTransaction);
        checkNode(
            intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(item, 1),
            payloadSize + uint256::kBytes + sizeof(std::uint8_t),
            kWireTypeTransactionWithMeta);
        checkNode(
            intr_ptr::makeShared<SHAMapAccountStateLeafNode>(item, 1),
            payloadSize + uint256::kBytes + sizeof(std::uint8_t),
            kWireTypeAccountState);
    }
}

}  // namespace xrpl
