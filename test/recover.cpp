//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/recover.hpp>

#include <nudb/test/fail_file.hpp>
#include <nudb/test/test_store.hpp>
#include <nudb/progress.hpp>
#include <beast/unit_test/suite.hpp>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <utility>

namespace nudb {
namespace test {

class basic_recover_test : public beast::unit_test::suite
{
public:
    using key_type = std::uint32_t;

    void
    test_ok()
    {
        std::size_t const keySize = 8;
        std::size_t const blockSize = 256;
        float const loadFactor = 0.5f;
        
        error_code ec;
        test_store ts{keySize, blockSize, loadFactor};
        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        recover<xxhasher>(ts.dp, ts.kp, ts.lp, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
    }

    // Creates and opens a database, performs a bunch
    // of inserts, then fetches all of them to make sure
    // they are there. Uses a fail_file that causes the n-th
    // I/O to fail, causing an exception.
    void
    do_work(
        test_store& ts,
        std::size_t N,
        fail_counter& c,
        error_code& ec)
    {
        ts.create(ec);
        if(ec)
            return;
        basic_store<xxhasher, fail_file<native_file>> db;
        db.open(ts.dp, ts.kp, ts.lp, ec, c);
        if(ec)
            return;
        if(! BEAST_EXPECT(db.appnum() == ts.appnum))
            return;
        // Insert
        for(std::size_t i = 0; i < N; ++i)
        {
            auto const item = ts[i];
            db.insert(item.key, item.data, item.size, ec);
            if(ec == test_error::failure)
                return;
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
        // Fetch
        Buffer b;
        for(std::size_t i = 0; i < N; ++i)
        {
            auto const item = ts[i];
            db.fetch(item.key, b, ec);
            if(ec == test_error::failure)
                return;
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            if(! BEAST_EXPECT(b.size() == item.size))
                return;
            if(! BEAST_EXPECT(std::memcmp(b.data(),
                    item.data, item.size) == 0))
                return;
        }
        db.close(ec);
        if(ec == test_error::failure)
            return;
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // Verify
        verify_info info;
        verify<xxhasher>(info, ts.dp, ts.kp,
            0, no_progress{}, ec);
        if(ec)
        {
            log << info;
            return;
        }
    }

    void
    do_recover(
        test_store& ts, fail_counter& c, error_code& ec)
    {
        recover<xxhasher, fail_file<native_file>>(
            ts.dp, ts.kp, ts.lp, ec, c);
        if(ec)
            return;
        // Verify
        verify_info info;
        verify<xxhasher>(info, ts.dp, ts.kp,
            0, no_progress{}, ec);
        if(ec)
            return;
        ts.erase();
    }

    void
    test_recover(std::size_t blockSize,
        float loadFactor, std::size_t N)
    {
        testcase(std::to_string(N) + " inserts",
            beast::unit_test::abort_on_fail);
        test_store ts{sizeof(key_type), blockSize, loadFactor};
        for(std::size_t n = 1;; ++n)
        {
            {
                error_code ec;
                fail_counter c{n};
                do_work(ts, N, c, ec);
                if(! ec)
                {
                    ts.close(ec);
                    ts.erase();
                    break;
                }
                if(! BEAST_EXPECTS(ec ==
                        test::test_error::failure, ec.message()))
                    return;
            }
            for(std::size_t m = 1;; ++m)
            {
                error_code ec;
                fail_counter c{m};
                do_recover(ts, c, ec);
                if(! ec)
                    break;
                if(! BEAST_EXPECTS(ec ==
                        test::test_error::failure, ec.message()))
                    return;
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
        test_ok();
        test_recover(128, 0.55f, 0);
        test_recover(128, 0.55f, 10);
        test_recover(128, 0.55f, 100);
    }
};

class recover_big_test : public basic_recover_test
{
public:
    void
    run() override
    {
        test_recover(256, 0.55f, 1000);
        test_recover(256, 0.90f, 10000);
    }
};

BEAST_DEFINE_TESTSUITE(recover, test, nudb);
//BEAST_DEFINE_TESTSUITE_MANUAL(recover_big, test, nudb);

} // test
} // nudb
