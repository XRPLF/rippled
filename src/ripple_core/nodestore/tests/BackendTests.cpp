//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

// Tests the Backend interface
//
class BackendTests : public TestBase
{
public:
    void testBackend (String type, int64 const seedValue, int numObjectsToTest = 2000)
    {
        DummyScheduler scheduler;

        beginTestCase (String ("Backend type=") + type);

        StringPairArray params;
        File const path (File::createTempFile ("node_db"));
        params.set ("type", type);
        params.set ("path", path.getFullPathName ());

        // Create a batch
        Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        {
            // Open the backend
            ScopedPointer <Backend> backend (DatabaseImp::createBackend (params, scheduler));

            // Write the batch
            storeBatch (*backend, batch);

            {
                // Read it back in
                Batch copy;
                fetchCopyOfBatch (*backend, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }

            {
                // Reorder and read the copy again
                Batch copy;
                UnitTestUtilities::repeatableShuffle (batch.size (), batch, seedValue);
                fetchCopyOfBatch (*backend, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }
        }

        {
            // Re-open the backend
            ScopedPointer <Backend> backend (DatabaseImp::createBackend (params, scheduler));

            // Read it back in
            Batch copy;
            fetchCopyOfBatch (*backend, &copy, batch);
            // Canonicalize the source and destination batches
            std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
            std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
            expect (areBatchesEqual (batch, copy), "Should be equal");
        }
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        int const seedValue = 50;

        testBackend ("leveldb", seedValue);

        testBackend ("sqlite", seedValue);

        #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testBackend ("hyperleveldb", seedValue);
        #endif

        #if RIPPLE_MDB_AVAILABLE
        testBackend ("mdb", seedValue, 200);
        #endif

        #if RIPPLE_SOPHIA_AVAILABLE
        testBackend ("sophia", seedValue);
        #endif
    }

    BackendTests () : TestBase ("NodeStoreBackend")
    {
    }
};

static BackendTests backendTests;

}
