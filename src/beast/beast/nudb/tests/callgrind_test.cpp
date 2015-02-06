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

#include <beast/nudb/tests/common.h>
#include <beast/module/core/diagnostic/UnitTestUtilities.h>
#include <beast/module/core/files/File.h>
#include <beast/random/xor_shift_engine.h>
#include <beast/unit_test/suite.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <utility>

namespace beast {
namespace nudb {
namespace test {

// This test is designed for callgrind runs to find hotspots
class callgrind_test : public unit_test::suite
{
public:
    // Creates and opens a database, performs a bunch
    // of inserts, then alternates fetching all the keys
    // with keys not present.
    void
    do_test (std::size_t count,
        path_type const& path)
    {
        auto const dp = path + ".dat";
        auto const kp = path + ".key";
        auto const lp = path + ".log";
        test_api::create (dp, kp, lp,
            appnum,
            salt,
            sizeof(nudb::test::key_type),
            nudb::block_size(path),
            0.50);
        test_api::store db;
        if (! expect (db.open(dp, kp, lp,
                arena_alloc_size), "open"))
            return;
        expect (db.appnum() == appnum, "appnum");
        Sequence seq;
        for (std::size_t i = 0; i < count; ++i)
        {
            auto const v = seq[i];
            expect (db.insert(&v.key, v.data, v.size),
                "insert");
        }
        Storage s;
        for (std::size_t i = 0; i < count * 2; ++i)
        {
            if (! (i%2))
            {
                auto const v = seq[i/2];
                expect (db.fetch (&v.key, s), "fetch");
                expect (s.size() == v.size, "size");
                expect (std::memcmp(s.get(),
                    v.data, v.size) == 0, "data");
            }
            else
            {
                auto const v = seq[count + i/2];
                expect (! db.fetch (&v.key, s),
                    "fetch missing");
            }
        }
        db.close();
        nudb::native_file::erase (dp);
        nudb::native_file::erase (kp);
        nudb::native_file::erase (lp);
    }

    void
    run() override
    {
        enum
        {
            // higher numbers, more pain
            N = 100000
        };

        testcase (abort_on_fail);
        path_type const path =
            beast::UnitTestUtilities::TempDirectory(
                "nudb").getFullPathName().toStdString();
        do_test (N, path);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(callgrind,nudb,beast);

} // test
} // nudb
} // beast

