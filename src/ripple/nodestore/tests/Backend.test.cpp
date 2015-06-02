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

#include <BeastConfig.h>
#include <ripple/nodestore/tests/Base.test.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <beast/module/core/diagnostic/UnitTestUtilities.h>

namespace ripple {
namespace NodeStore {

// Tests the Backend interface
//
class Backend_test : public TestBase
{
public:
    void testBackend (std::string const& type, std::int64_t const seedValue,
                      int numObjectsToTest = 2000)
    {
        DummyScheduler scheduler;

        testcase ("Backend type=" + type);

        Section params;
        beast::UnitTestUtilities::TempDirectory path ("node_db");
        params.set ("type", type);
        params.set ("path", path.getFullPathName ().toStdString ());

        // Create a batch
        Batch batch;
        createPredictableBatch (batch, numObjectsToTest, seedValue);

        beast::Journal j;

        {
            // Open the backend
            std::unique_ptr <Backend> backend =
                Manager::instance().make_Backend (params, scheduler, j);

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
                beast::UnitTestUtilities::repeatableShuffle (batch.size (), batch, seedValue);
                fetchCopyOfBatch (*backend, &copy, batch);
                expect (areBatchesEqual (batch, copy), "Should be equal");
            }
        }

        {
            // Re-open the backend
            std::unique_ptr <Backend> backend = Manager::instance().make_Backend (
                params, scheduler, j);

            // Read it back in
            Batch copy;
            fetchCopyOfBatch (*backend, &copy, batch);
            // Canonicalize the source and destination batches
            std::sort (batch.begin (), batch.end (), LessThan{});
            std::sort (copy.begin (), copy.end (), LessThan{});
            expect (areBatchesEqual (batch, copy), "Should be equal");
        }
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        int const seedValue = 50;

        testBackend ("nudb", seedValue);

    #if RIPPLE_ROCKSDB_AVAILABLE
        testBackend ("rocksdb", seedValue);
    #endif

    #ifdef RIPPLE_ENABLE_SQLITE_BACKEND_TESTS
        testBackend ("sqlite", seedValue);
    #endif
    }
};

BEAST_DEFINE_TESTSUITE(Backend,ripple_core,ripple);

}
}
