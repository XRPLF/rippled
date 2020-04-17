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

#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <test/nodestore/TestBase.h>

namespace ripple {
namespace NodeStore {

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

        auto batch1 = createPredictableBatch(numObjectsToTest, seedValue);

        auto batch2 = createPredictableBatch(numObjectsToTest, seedValue);

        BEAST_EXPECT(areBatchesEqual(batch1, batch2));

        auto batch3 = createPredictableBatch(numObjectsToTest, seedValue + 1);

        BEAST_EXPECT(!areBatchesEqual(batch1, batch3));
    }

    // Checks encoding/decoding blobs
    void
    testBlobs(std::uint64_t const seedValue)
    {
        testcase("encoding");

        auto batch = createPredictableBatch(numObjectsToTest, seedValue);

        EncodedBlob encoded;
        for (int i = 0; i < batch.size(); ++i)
        {
            encoded.prepare(batch[i]);

            DecodedBlob decoded(
                encoded.getKey(), encoded.getData(), encoded.getSize());

            BEAST_EXPECT(decoded.wasOk());

            if (decoded.wasOk())
            {
                std::shared_ptr<NodeObject> const object(
                    decoded.createObject());

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

BEAST_DEFINE_TESTSUITE(NodeStoreBasic, ripple_core, ripple);

}  // namespace NodeStore
}  // namespace ripple
