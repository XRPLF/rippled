//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

class TimingTests : public TestBase
{
public:
    enum
    {
        numObjectsToTest     = 10000
    };

    TimingTests ()
        : TestBase ("NodeStoreTiming", UnitTest::runManual)
    {
    }

    class Stopwatch
    {
    public:
        Stopwatch ()
        {
        }

        void start ()
        {
            m_startTime = Time::getHighResolutionTicks ();
        }

        double getElapsed ()
        {
            int64 const now = Time::getHighResolutionTicks();

            return Time::highResolutionTicksToSeconds (now - m_startTime);
        }

    private:
        int64 m_startTime;
    };

    //--------------------------------------------------------------------------

    void testBackend (String type, int64 const seedValue)
    {
        DummyScheduler scheduler;

        String s;
        s << "Testing backend '" << type << "' performance";
        beginTestCase (s);

        StringPairArray params;
        File const path (File::createTempFile ("node_db"));
        params.set ("type", type);
        params.set ("path", path.getFullPathName ());

        // Create batches
        NodeStore::Batch batch1;
        createPredictableBatch (batch1, 0, numObjectsToTest, seedValue);
        NodeStore::Batch batch2;
        createPredictableBatch (batch2, 0, numObjectsToTest, seedValue);

        // Open the backend
        ScopedPointer <Backend> backend (DatabaseImp::createBackend (params, scheduler));

        Stopwatch t;

        // Individual write batch test
        t.start ();
        storeBatch (*backend, batch1);
        s = "";
        s << "  Single write: " << String (t.getElapsed (), 2) << " seconds";
        logMessage (s);

        // Bulk write batch test
        t.start ();
        backend->storeBatch (batch2);
        s = "";
        s << "  Batch write:  " << String (t.getElapsed (), 2) << " seconds";
        logMessage (s);

        // Read test
        Batch copy;
        t.start ();
        fetchCopyOfBatch (*backend, &copy, batch1);
        fetchCopyOfBatch (*backend, &copy, batch2);
        s = "";
        s << "  Batch read:   " << String (t.getElapsed (), 2) << " seconds";
        logMessage (s);
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        int const seedValue = 50;

        testBackend ("leveldb", seedValue);

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testBackend ("hyperleveldb", seedValue);
    #endif

    /*
    #if RIPPLE_MDB_AVAILABLE
        testBackend ("mdb", seedValue);
    #endif
    */

    #if RIPPLE_SOPHIA_AVAILABLE
        testBackend ("sophia", seedValue);
    #endif

    /*
        testBackend ("sqlite", seedValue);
    */
    }
};

static TimingTests timingTests;

}
