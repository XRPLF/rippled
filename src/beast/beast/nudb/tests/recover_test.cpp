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

#include <beast/nudb/store.h>
#include <beast/nudb/recover.h>
#include <beast/nudb/tests/common.h>
#include <beast/nudb/tests/fail_file.h>
#include <beast/module/core/files/File.h>
#include <beast/random/xor_shift_engine.h>
#include <beast/unit_test/suite.h>
#include <cmath>
#include <memory>
#include <random>
#include <utility>

namespace beast {
namespace nudb {
namespace test {

class basic_recover_test : public unit_test::suite
{
public:
    // Creates and opens a database, performs a bunch
    // of inserts, then fetches all of them to make sure
    // they are there. Uses a fail_file that causes the n-th
    // I/O to fail, causing an exception.
    void
    do_work (std::size_t n, std::size_t count,
        float load_factor, nudb::path_type const& path)
    {
        auto const dp = path + ".dat";
        auto const kp = path + ".key";
        auto const lp = path + ".log";
        nudb::fail_counter c(0);
        nudb::create (dp, kp, lp, appnum, salt,
            sizeof(key_type), block_size(path),
                load_factor);
        fail_store db;
        if (! expect(db.open(dp, kp, lp,
            arena_alloc_size, c), "open"))
        {
            // VFALCO open should never fail here, we need
            //        to report this and terminate the test.
        }
        expect (db.appnum() == appnum, "appnum");
        c.reset(n);
        Sequence seq;
        for (std::size_t i = 0; i < count; ++i)
        {
            auto const v = seq[i];
            db.insert(&v.key, v.data, v.size);
        }
        std::size_t capacity = 0;
        std::unique_ptr<std::uint8_t[]> data;
        for (std::size_t i = 0; i < count; ++i)
        {
            auto const v = seq[i];
            std::size_t bytes;
            bool const found = db.fetch (&v.key,
                [&](std::size_t n)
                {
                    if (capacity < n)
                    {
                        capacity = nudb::detail::ceil_pow2(n);
                        data.reset (
                            new std::uint8_t[capacity]);
                    }
                    bytes = n;
                    return data.get();
                });
            if (! expect(found, "found"))
                break;
            if (! expect(bytes == v.size, "size"))
                break;
            if (! expect(memcmp(data.get(),
                    v.data, v.size) == 0, "data"))
                break;
        }
        db.close();
    #ifndef NDEBUG
        print(log, verify(dp, kp));
        verify(dp, kp);
    #endif
        nudb::native_file::erase (dp);
        nudb::native_file::erase (kp);
        nudb::native_file::erase (lp);
    }

    void
    do_recover (path_type const& path)
    {
        auto const dp = path + ".dat";
        auto const kp = path + ".key";
        auto const lp = path + ".log";
        recover(dp, kp, lp);
        verify(dp, kp);
        nudb::native_file::erase (dp);
        nudb::native_file::erase (kp);
        nudb::native_file::erase (lp);
    }

    void
    test_recover (float load_factor, std::size_t count)
    {
        testcase << count << " inserts";
        path_type const path =
            beast::UnitTestUtilities::TempDirectory(
                "nudb").getFullPathName().toStdString();
        for (std::size_t n = 1;;++n)
        {
            try
            {
                do_work (n, count, load_factor, path);
                break;
            }
            catch (nudb::fail_error const&)
            {
                do_recover (path);
            }
        }
    }
};

class recover_test : public basic_recover_test
{
public:
    void
    run() override
    {
        float lf = 0.75f;
        test_recover (lf, 0);
        test_recover (lf, 10);
        test_recover (lf, 100);
        test_recover (lf, 1000);
    }
};

BEAST_DEFINE_TESTSUITE(recover,nudb,beast);

class recover_big_test : public basic_recover_test
{
public:
    void
    run() override
    {
        float lf = 0.90f;
        test_recover (lf, 100000);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(recover_big,nudb,beast);

} // test
} // nudb
} // beast

