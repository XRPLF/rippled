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
    for (unsigned int n = 1; n <= SHAMapInnerNode::kBranchFactor; ++n)
    {
        auto const node = makeInnerWithBranches(n);
        auto const sz = node->sizeForWire();
        Blob buf(sz);
        node->serializeForWire(buf.data());
        EXPECT_EQ(sz, buf.size()) << "branch count: " << n;
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

        auto const checkNode = [&](auto const& node) {
            auto const sz = node->sizeForWire();
            Blob buf(sz);
            node->serializeForWire(buf.data());
            EXPECT_EQ(sz, buf.size()) << "payload: " << payloadSize;
        };

        checkNode(intr_ptr::makeShared<SHAMapTxLeafNode>(item, 1));
        checkNode(intr_ptr::makeShared<SHAMapTxPlusMetaLeafNode>(item, 1));
        checkNode(intr_ptr::makeShared<SHAMapAccountStateLeafNode>(item, 1));
    }
}

}  // namespace xrpl
