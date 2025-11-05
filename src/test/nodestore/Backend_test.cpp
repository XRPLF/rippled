#include <test/nodestore/TestBase.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/rocksdb.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/nodestore/DummyScheduler.h>
#include <xrpl/nodestore/Manager.h>

#include <algorithm>

namespace ripple {

namespace NodeStore {

// Tests the Backend interface
//
class Backend_test : public TestBase
{
public:
    void
    testBackend(
        std::string const& type,
        std::uint64_t const seedValue,
        int numObjsToTest = 2000)
    {
        DummyScheduler scheduler;

        testcase("Backend type=" + type);

        Section params;
        beast::temp_dir tempDir;
        params.set("type", type);
        params.set("path", tempDir.path());

        beast::xor_shift_engine rng(seedValue);

        // Create a batch
        auto batch = createPredictableBatch(numObjsToTest, rng());

        using namespace beast::severities;
        test::SuiteJournal journal("Backend_test", *this);

        {
            // Open the backend
            std::unique_ptr<Backend> backend = Manager::instance().make_Backend(
                params, megabytes(4), scheduler, journal);
            backend->open();

            // Write the batch
            storeBatch(*backend, batch);

            {
                // Read it back in
                Batch copy;
                fetchCopyOfBatch(*backend, &copy, batch);
                BEAST_EXPECT(areBatchesEqual(batch, copy));
            }

            {
                // Reorder and read the copy again
                std::shuffle(batch.begin(), batch.end(), rng);
                Batch copy;
                fetchCopyOfBatch(*backend, &copy, batch);
                BEAST_EXPECT(areBatchesEqual(batch, copy));
            }
        }

        {
            // Re-open the backend
            std::unique_ptr<Backend> backend = Manager::instance().make_Backend(
                params, megabytes(4), scheduler, journal);
            backend->open();

            // Read it back in
            Batch copy;
            fetchCopyOfBatch(*backend, &copy, batch);
            // Canonicalize the source and destination batches
            std::sort(batch.begin(), batch.end(), LessThan{});
            std::sort(copy.begin(), copy.end(), LessThan{});
            BEAST_EXPECT(areBatchesEqual(batch, copy));
        }
    }

    //--------------------------------------------------------------------------

    void
    run() override
    {
        std::uint64_t const seedValue = 50;

        testBackend("nudb", seedValue);

#if XRPL_ROCKSDB_AVAILABLE
        testBackend("rocksdb", seedValue);
#endif

#ifdef XRPL_ENABLE_SQLITE_BACKEND_TESTS
        testBackend("sqlite", seedValue);
#endif
    }
};

BEAST_DEFINE_TESTSUITE(Backend, nodestore, ripple);

}  // namespace NodeStore
}  // namespace ripple
