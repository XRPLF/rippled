#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>

#include <gtest/gtest.h>
#include <nodestore/TestBase.h>

#include <cstddef>
#include <memory>
#include <string>

namespace xrpl::node_store {

TEST(NodeStoreBasics, predictable_batches)
{
    auto const batch1 = createPredictableBatch(kNumObjectsToTest, kSeedValue);
    auto const batch2 = createPredictableBatch(kNumObjectsToTest, kSeedValue);
    EXPECT_EQ(batch1, batch2);

    auto const batch3 = createPredictableBatch(kNumObjectsToTest, kSeedValue + 1);
    EXPECT_NE(batch1, batch3);
}

TEST(NodeStoreBasics, blob_encoding)
{
    auto const batch = createPredictableBatch(kNumObjectsToTest, kSeedValue);
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        SCOPED_TRACE("blob index=" + std::to_string(i));
        EncodedBlob const encoded(batch[i]);
        DecodedBlob decoded(encoded.getKey(), encoded.getData(), encoded.getSize());
        EXPECT_TRUE(decoded.wasOk());
        if (decoded.wasOk())
        {
            std::shared_ptr<NodeObject> const object(decoded.createObject());
            EXPECT_TRUE(isSame(batch[i], object));
        }
    }
}

}  // namespace xrpl::node_store
