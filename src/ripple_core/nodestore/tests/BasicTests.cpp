//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

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
