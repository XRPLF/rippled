//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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
#include <beast/nudb.h>
#include <beast/nudb/tests/common.h>
#include <beast/nudb/tests/fail_file.h>
#include <beast/module/core/diagnostic/UnitTestUtilities.h>
#include <beast/module/core/files/File.h>
#include <beast/random/xor_shift_engine.h>
#include <beast/unit_test/suite.h>
#include <cmath>
#include <iomanip>
#include <memory>
#include <random>
#include <utility>

namespace beast {
namespace nudb {
namespace test {

// Basic, single threaded test that verifies the
// correct operation of the store. Load factor is
// set high to ensure that spill records are created,
// exercised, and split.
//
class store_test : public unit_test::suite
{
public:
    void
    do_test (std::size_t N,
        std::size_t block_size, float load_factor)
    {
        testcase (abort_on_fail);
        std::string const path =
            beast::UnitTestUtilities::TempDirectory(
                "test_db").getFullPathName().toStdString();
        auto const dp = path + ".dat";
        auto const kp = path + ".key";
        auto const lp = path + ".log";
        Sequence seq;
        nudb::store db;
        try
        {
            expect (nudb::create (dp, kp, lp, appnum,
                salt, sizeof(key_type), block_size,
                    load_factor), "create");
            expect (db.open(dp, kp, lp,
                arena_alloc_size), "open");
            storage s;
            // insert
            for (std::size_t i = 0; i < N; ++i)
            {
                auto const v = seq[i];
                expect (db.insert(
                    &v.key, v.data, v.size), "insert 1");
            }
            // fetch
            for (std::size_t i = 0; i < N; ++i)
            {
                auto const v = seq[i];
                bool const found = db.fetch (&v.key, s);
                expect (found, "not found");
                expect (s.size() == v.size, "wrong size");
                expect (std::memcmp(s.get(),
                    v.data, v.size) == 0, "not equal");
            }
            // insert duplicates
            for (std::size_t i = 0; i < N; ++i)
            {
                auto const v = seq[i];
                expect (! db.insert(&v.key,
                    v.data, v.size), "insert duplicate");
            }
            // insert/fetch
            for (std::size_t i = 0; i < N; ++i)
            {
                auto v = seq[i];
                bool const found = db.fetch (&v.key, s);
                expect (found, "missing");
                expect (s.size() == v.size, "wrong size");
                expect (memcmp(s.get(),
                    v.data, v.size) == 0, "wrong data");
                v = seq[i + N];
                expect (db.insert(&v.key, v.data, v.size),
                    "insert 2");
            }
            db.close();
            auto const stats = nudb::verify (dp, kp);
            expect (stats.hist[1] > 0, "no splits");
            print (log, stats);
        }
        catch (nudb::store_error const& e)
        {
            fail (e.what());
        }
        catch (std::exception const& e)
        {
            fail (e.what());
        }
        expect (native_file::erase(dp));
        expect (native_file::erase(kp));
        expect (! native_file::erase(lp));
    }

    void
    run() override
    {
        enum
        {
            N =             50000
            ,block_size =   256
        };

        float const load_factor = 0.95f;

        do_test (N, block_size, load_factor);
    }
};

BEAST_DEFINE_TESTSUITE(store,nudb,beast);

} // test
} // nudb
} // beast

