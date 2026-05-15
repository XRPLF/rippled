#include <test/nodestore/TestBase.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/detail/DecodedBlob.h>
#include <xrpl/nodestore/detail/EncodedBlob.h>

#include <cstdint>
#include <memory>

namespace xrpl::NodeStore {

// Tests predictable batches, and NodeObject blob encoding
//
class NodeStoreBasic_test : public TestBase
{
public:
    // Make sure predictable object generation works!
    void
    testBatches(std::uint64_t const seedValue)
    {
        testcase("batch");

        auto batch1 = createPredictableBatch(kNumObjectsToTest, seedValue);

        auto batch2 = createPredictableBatch(kNumObjectsToTest, seedValue);

        BEAST_EXPECT(areBatchesEqual(batch1, batch2));

        auto batch3 = createPredictableBatch(kNumObjectsToTest, seedValue + 1);

        BEAST_EXPECT(!areBatchesEqual(batch1, batch3));
    }

    // Checks encoding/decoding blobs
    void
    testBlobs(std::uint64_t const seedValue)
    {
        testcase("encoding");

        auto batch = createPredictableBatch(kNumObjectsToTest, seedValue);

        for (int i = 0; i < batch.size(); ++i)
        {
            EncodedBlob const encoded(batch[i]);

            DecodedBlob decoded(encoded.getKey(), encoded.getData(), encoded.getSize());

            BEAST_EXPECT(decoded.wasOk());

            if (decoded.wasOk())
            {
                std::shared_ptr<NodeObject> const object(decoded.createObject());

                BEAST_EXPECT(isSame(batch[i], object));
            }
        }
    }

    void
    run() override
    {
        std::uint64_t const seedValue = 50;

        testBatches(seedValue);

        testBlobs(seedValue);
    }
};

BEAST_DEFINE_TESTSUITE(NodeStoreBasic, nodestore, xrpl);

}  // namespace xrpl::NodeStore
