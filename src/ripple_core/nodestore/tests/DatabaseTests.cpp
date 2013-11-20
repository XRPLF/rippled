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

class DatabaseTests : public TestBase
{
public:
    DatabaseTests ()
        : TestBase ("NodeStore")
    {
    }

    ~DatabaseTests ()
    {
    }

    //--------------------------------------------------------------------------

    void testImport (String destBackendType, String srcBackendType, int64 seedValue)
    {
        DummyScheduler scheduler;

        File const node_db (File::createTempFile ("node_db"));
        StringPairArray srcParams;
        srcParams.set ("type", srcBackendType);
        srcParams.set ("path", node_db.getFullPathName ());

        // Create a batch
        Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        // Write to source db
        {
            ScopedPointer <Database> src (Database::New ("test", scheduler, srcParams));
            storeBatch (*src, batch);
        }

        Batch copy;

        {
            // Re-open the db
            ScopedPointer <Database> src (Database::New ("test", scheduler, srcParams));

            // Set up the destination database
            File const dest_db (File::createTempFile ("dest_db"));
            StringPairArray destParams;
            destParams.set ("type", destBackendType);
            destParams.set ("path", dest_db.getFullPathName ());

            ScopedPointer <Database> dest (Database::New ("test", scheduler, destParams));

            beginTestCase (String ("import into '") + destBackendType + "' from '" + srcBackendType + "'");

            // Do the import
            dest->import (*src);

            // Get the results of the import
            fetchCopyOfBatch (*dest, &copy, batch);
        }

        // Canonicalize the source and destination batches
        std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
        std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
        expect (areBatchesEqual (batch, copy), "Should be equal");

    }

    //--------------------------------------------------------------------------

    void testNodeStore (String type,
                        bool const useEphemeralDatabase,
                        bool const testPersistence,
                        int64 const seedValue,
                        int numObjectsToTest = 2000)
    {
        DummyScheduler scheduler;

        String s;
        s << String ("NodeStore backend '") + type + "'";
        if (useEphemeralDatabase)
            s << " (with ephemeral database)";

        beginTestCase (s);

        File const node_db (File::createTempFile ("node_db"));
        StringPairArray nodeParams;
        nodeParams.set ("type", type);
        nodeParams.set ("path", node_db.getFullPathName ());

        File const temp_db  (File::createTempFile ("temp_db"));
        StringPairArray tempParams;
        if (useEphemeralDatabase)
        {
            tempParams.set ("type", type);
            tempParams.set ("path", temp_db.getFullPathName ());
        }

        // Create a batch
        Batch batch;
        createPredictableBatch (batch, 0, numObjectsToTest, seedValue);

        {
            // Open the database
            ScopedPointer <Database> db (Database::New ("test", scheduler, nodeParams, tempParams));

            // Write the batch
            storeBatch (*db, batch);

            {
                // Read it back in
                Batch copy;
                fetchCopyOfBatch (*db, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }

            {
                // Reorder and read the copy again
                Batch copy;
                UnitTestUtilities::repeatableShuffle (batch.size (), batch, seedValue);
                fetchCopyOfBatch (*db, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }
        }

        if (testPersistence)
        {
            {
                // Re-open the database without the ephemeral DB
                ScopedPointer <Database> db (Database::New ("test", scheduler, nodeParams));

                // Read it back in
                Batch copy;
                fetchCopyOfBatch (*db, &copy, batch);

                // Canonicalize the source and destination batches
                std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
                std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }

            if (useEphemeralDatabase)
            {
                // Verify the ephemeral db
                ScopedPointer <Database> db (Database::New ("test",
                    scheduler, tempParams, StringPairArray ()));

                // Read it back in
                Batch copy;
                fetchCopyOfBatch (*db, &copy, batch);

                // Canonicalize the source and destination batches
                std::sort (batch.begin (), batch.end (), NodeObject::LessThan ());
                std::sort (copy.begin (), copy.end (), NodeObject::LessThan ());
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }
        }
    }

    //--------------------------------------------------------------------------

    void runBackendTests (bool useEphemeralDatabase, int64 const seedValue)
    {
        testNodeStore ("leveldb", useEphemeralDatabase, true, seedValue);

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testNodeStore ("hyperleveldb", useEphemeralDatabase, true, seedValue);
    #endif

    #if RIPPLE_MDB_AVAILABLE
        testNodeStore ("mdb", useEphemeralDatabase, true, seedValue, 200);
    #endif

    #if RIPPLE_SOPHIA_AVAILABLE
        testNodeStore ("sophia", useEphemeralDatabase, true, seedValue);
    #endif

        testNodeStore ("sqlite", useEphemeralDatabase, true, seedValue);
    }

    //--------------------------------------------------------------------------

    void runImportTests (int64 const seedValue)
    {
        testImport ("leveldb", "leveldb", seedValue);

    #if RIPPLE_HYPERLEVELDB_AVAILABLE
        testImport ("hyperleveldb", "hyperleveldb", seedValue);
    #endif

    /*
    #if RIPPLE_MDB_AVAILABLE
        testImport ("mdb", "mdb", seedValue);
    #endif
    */

    /*
    #if RIPPLE_SOPHIA_AVAILABLE
        testImport ("sophia", "sophia", seedValue);
    #endif
    */

        testImport ("sqlite", "sqlite", seedValue);
    }

    //--------------------------------------------------------------------------

    void runTest ()
    {
        int64 const seedValue = 50;

        testNodeStore ("memory", false, false, seedValue);

        runBackendTests (false, seedValue);

        runBackendTests (true, seedValue);

        runImportTests (seedValue);
    }
};

static DatabaseTests databaseTests;

}
