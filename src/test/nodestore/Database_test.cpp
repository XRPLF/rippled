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

#include <ripple/beast/utility/temp_dir.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <test/nodestore/TestBase.h>
#include <test/unit_test/SuiteJournal.h>

namespace ripple {
namespace NodeStore {

class Database_test : public TestBase
{
    test::SuiteJournal journal_;

public:
    Database_test() : journal_("Database_test", *this)
    {
    }

    void
    testImport(
        std::string const& destBackendType,
        std::string const& srcBackendType,
        std::int64_t seedValue)
    {
        DummyScheduler scheduler;
        RootStoppable parent("TestRootStoppable");

        beast::temp_dir node_db;
        Section srcParams;
        srcParams.set("type", srcBackendType);
        srcParams.set("path", node_db.path());

        // Create a batch
        auto batch = createPredictableBatch(numObjectsToTest, seedValue);

        // Write to source db
        {
            std::unique_ptr<Database> src = Manager::instance().make_Database(
                "test", scheduler, 2, parent, srcParams, journal_);
            storeBatch(*src, batch);
        }

        Batch copy;

        {
            // Re-open the db
            std::unique_ptr<Database> src = Manager::instance().make_Database(
                "test", scheduler, 2, parent, srcParams, journal_);

            // Set up the destination database
            beast::temp_dir dest_db;
            Section destParams;
            destParams.set("type", destBackendType);
            destParams.set("path", dest_db.path());

            std::unique_ptr<Database> dest = Manager::instance().make_Database(
                "test", scheduler, 2, parent, destParams, journal_);

            testcase(
                "import into '" + destBackendType + "' from '" +
                srcBackendType + "'");

            // Do the import
            dest->import(*src);

            // Get the results of the import
            fetchCopyOfBatch(*dest, &copy, batch);
        }

        // Canonicalize the source and destination batches
        std::sort(batch.begin(), batch.end(), LessThan{});
        std::sort(copy.begin(), copy.end(), LessThan{});
        BEAST_EXPECT(areBatchesEqual(batch, copy));
    }

    //--------------------------------------------------------------------------

    void
    testNodeStore(
        std::string const& type,
        bool const testPersistence,
        std::int64_t const seedValue,
        int numObjsToTest = 2000)
    {
        DummyScheduler scheduler;
        RootStoppable parent("TestRootStoppable");

        std::string s = "NodeStore backend '" + type + "'";

        testcase(s);

        beast::temp_dir node_db;
        Section nodeParams;
        nodeParams.set("type", type);
        nodeParams.set("path", node_db.path());

        beast::xor_shift_engine rng(seedValue);

        // Create a batch
        auto batch = createPredictableBatch(numObjsToTest, rng());

        {
            // Open the database
            std::unique_ptr<Database> db = Manager::instance().make_Database(
                "test", scheduler, 2, parent, nodeParams, journal_);

            // Write the batch
            storeBatch(*db, batch);

            {
                // Read it back in
                Batch copy;
                fetchCopyOfBatch(*db, &copy, batch);
                BEAST_EXPECT(areBatchesEqual(batch, copy));
            }

            {
                // Reorder and read the copy again
                std::shuffle(batch.begin(), batch.end(), rng);
                Batch copy;
                fetchCopyOfBatch(*db, &copy, batch);
                BEAST_EXPECT(areBatchesEqual(batch, copy));
            }
        }

        if (testPersistence)
        {
            // Re-open the database without the ephemeral DB
            std::unique_ptr<Database> db = Manager::instance().make_Database(
                "test", scheduler, 2, parent, nodeParams, journal_);

            // Read it back in
            Batch copy;
            fetchCopyOfBatch(*db, &copy, batch);

            // Canonicalize the source and destination batches
            std::sort(batch.begin(), batch.end(), LessThan{});
            std::sort(copy.begin(), copy.end(), LessThan{});
            BEAST_EXPECT(areBatchesEqual(batch, copy));
        }

        if (type == "memory")
        {
            // Earliest ledger sequence tests
            {
                // Verify default earliest ledger sequence
                std::unique_ptr<Database> db =
                    Manager::instance().make_Database(
                        "test", scheduler, 2, parent, nodeParams, journal_);
                BEAST_EXPECT(
                    db->earliestLedgerSeq() == XRP_LEDGER_EARLIEST_SEQ);
            }

            // Set an invalid earliest ledger sequence
            try
            {
                nodeParams.set("earliest_seq", "0");
                std::unique_ptr<Database> db =
                    Manager::instance().make_Database(
                        "test", scheduler, 2, parent, nodeParams, journal_);
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::strcmp(e.what(), "Invalid earliest_seq") == 0);
            }

            {
                // Set a valid earliest ledger sequence
                nodeParams.set("earliest_seq", "1");
                std::unique_ptr<Database> db =
                    Manager::instance().make_Database(
                        "test", scheduler, 2, parent, nodeParams, journal_);

                // Verify database uses the earliest ledger sequence setting
                BEAST_EXPECT(db->earliestLedgerSeq() == 1);
            }

            // Create another database that attempts to set the value again
            try
            {
                // Set to default earliest ledger sequence
                nodeParams.set(
                    "earliest_seq", std::to_string(XRP_LEDGER_EARLIEST_SEQ));
                std::unique_ptr<Database> db2 =
                    Manager::instance().make_Database(
                        "test", scheduler, 2, parent, nodeParams, journal_);
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::strcmp(e.what(), "earliest_seq set more than once") ==
                    0);
            }
        }
    }

    //--------------------------------------------------------------------------

    void
    run() override
    {
        std::int64_t const seedValue = 50;

        testNodeStore("memory", false, seedValue);

        // Persistent backend tests
        {
            testNodeStore("nudb", true, seedValue);

#if RIPPLE_ROCKSDB_AVAILABLE
            testNodeStore("rocksdb", true, seedValue);
#endif
        }

        // Import tests
        {
            testImport("nudb", "nudb", seedValue);

#if RIPPLE_ROCKSDB_AVAILABLE
            testImport("rocksdb", "rocksdb", seedValue);
#endif

#if RIPPLE_ENABLE_SQLITE_BACKEND_TESTS
            testImport("sqlite", "sqlite", seedValue);
#endif
        }
    }
};

BEAST_DEFINE_TESTSUITE(Database, NodeStore, ripple);

}  // namespace NodeStore
}  // namespace ripple
