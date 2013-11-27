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

    #if RIPPLE_ROCKSDB_AVAILABLE
        testBackend ("rocksdb", seedValue);
    #endif

    /*
        testBackend ("sqlite", seedValue);
    */
    }
};

static TimingTests timingTests;

}
