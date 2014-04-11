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

namespace ripple {
namespace NodeStore {

// Tests predictable batches, and NodeObject blob encoding
//
class BasicTests : public TestBase
{
public:
    BasicTests () : TestBase ("NodeStoreBasics")
    {
    }

    // Make sure predictable object generation works!
    void testBatches (int64 const seedValue)
    {
        beginTestCase ("batch");

        Batch batch1;
        createPredictableBatch (batch1, 0, numObjectsToTest, seedValue);

        Batch batch2;
        createPredictableBatch (batch2, 0, numObjectsToTest, seedValue);

        expect (areBatchesEqual (batch1, batch2), "Should be equal");

        Batch batch3;
        createPredictableBatch (batch3, 1, numObjectsToTest, seedValue);

        expect (! areBatchesEqual (batch1, batch3), "Should not be equal");
    }

    // Checks encoding/decoding blobs
    void testBlobs (int64 const seedValue)
    {
        beginTestCase ("encoding");

        Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        EncodedBlob encoded;
        for (int i = 0; i < batch.size (); ++i)
        {
            encoded.prepare (batch [i]);

            DecodedBlob decoded (encoded.getKey (), encoded.getData (), encoded.getSize ());

            expect (decoded.wasOk (), "Should be ok");

            if (decoded.wasOk ())
            {
                NodeObject::Ptr const object (decoded.createObject ());

                expect (batch [i]->isCloneOf (object), "Should be clones");
            }
        }
    }

    void runTest ()
    {
        int64 const seedValue = 50;

        testBatches (seedValue);

        testBlobs (seedValue);
    }
};

static BasicTests basicTests;

}
}
