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

class NodeStoreTiming_test : public TestBase
{
public:
    enum
    {
        numObjectsToTest     = 1000000
    };

    class Stopwatch
    {
    public:
        Stopwatch ()
        {
        }

        void start ()
        {
            m_startTime = beast::Time::getHighResolutionTicks ();
        }

        double getElapsed ()
        {
            std::int64_t const now = beast::Time::getHighResolutionTicks();

            return beast::Time::highResolutionTicksToSeconds (now - m_startTime);
        }

    private:
        std::int64_t m_startTime;
    };

    //--------------------------------------------------------------------------

    void testBackend (std::string const& type, std::int64_t const seedValue)
    {
        std::unique_ptr <Manager> manager (make_Manager ());

        DummyScheduler scheduler;

        testcase ("Testing backend '" + type + "' performance");

        beast::StringPairArray params;
        beast::UnitTestUtilities::TempDirectory path ("node_db");

        params.set ("type", type);
        params.set ("path", path.getFullPathName ());

        // Create batches
        NodeStore::Batch batch1;
        createPredictableBatch (batch1, numObjectsToTest, seedValue);
        NodeStore::Batch batch2;
        createPredictableBatch (batch2, numObjectsToTest, seedValue);
        NodeStore::Batch missingBatch;
        createPredictableBatch (missingBatch, numObjectsToTest, seedValue+1);

        beast::Journal j;

        // Open the backend
        std::unique_ptr <Backend> backend (manager->make_Backend (
            params, scheduler, j));

        Stopwatch t;

        // Individual write batch test
        t.start ();
        storeBatch (*backend, batch1);
        log << "  Single write:       " << std::to_string (t.getElapsed ()) << " seconds";

        // Bulk write batch test
        t.start ();
        backend->storeBatch (batch2);
        log << "  Batch write:        " << std::to_string (t.getElapsed ()) << " seconds";

        // Read test
        Batch copy;
        t.start ();
        fetchCopyOfBatch (*backend, &copy, batch1);
        fetchCopyOfBatch (*backend, &copy, batch2);
        log << "  Batch read:         " << std::to_string (t.getElapsed ()) << " seconds";

        // Read missing keys test
        t.start ();
        fetchMissing (*backend, missingBatch);
        log << "  Batch read missing: " << std::to_string (t.getElapsed ()) << " seconds";
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        int const seedValue = 50;

        testBackend ("leveldb", seedValue);

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testBackend ("hyperleveldb", seedValue);
    #endif

    #if RIPPLE_ROCKSDB_AVAILABLE
        testBackend ("rocksdb", seedValue);
    #endif

    #if RIPPLE_ENABLE_SQLITE_BACKEND_TESTS
        testBackend ("sqlite", seedValue);
    #endif
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(NodeStoreTiming,ripple_core,ripple);

}
}
