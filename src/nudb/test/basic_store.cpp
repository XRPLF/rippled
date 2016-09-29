//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/basic_store.hpp>

#include <nudb/test/test_store.hpp>
#include <nudb/detail/arena.hpp>
#include <nudb/detail/cache.hpp>
#include <nudb/detail/pool.hpp>
#include <nudb/progress.hpp>
#include <nudb/verify.hpp>
#include <beast/unit_test/suite.hpp>
#include <limits>
#include <type_traits>

namespace nudb {

namespace detail {

static_assert(!std::is_copy_constructible   <arena>{}, "");
static_assert(!std::is_copy_assignable      <arena>{}, "");
static_assert( std::is_move_constructible   <arena>{}, "");
static_assert(!std::is_move_assignable      <arena>{}, "");

static_assert(!std::is_copy_constructible   <cache>{}, "");
static_assert(!std::is_copy_assignable      <cache>{}, "");
static_assert( std::is_move_constructible   <cache>{}, "");
static_assert(!std::is_move_assignable      <cache>{}, "");

static_assert(!std::is_copy_constructible   <pool>{}, "");
static_assert(!std::is_copy_assignable      <pool>{}, "");
static_assert( std::is_move_constructible   <pool>{}, "");
static_assert(!std::is_move_assignable      <pool>{}, "");

} // detail

namespace test {

class basic_store_test : public beast::unit_test::suite
{
public:
    void
    test_members()
    {
        std::size_t const keySize = 4;
        std::size_t const blockSize = 4096;
        float loadFactor = 0.5f;

        error_code ec;
        test_store ts{keySize, blockSize, loadFactor};

        // Files not found
        ts.open(ec);
        if(! BEAST_EXPECTS(ec ==
                errc::no_such_file_or_directory, ec.message()))
            return;
        ec = {};
        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ts.open(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        BEAST_EXPECT(ts.db.dat_path() == ts.dp);
        BEAST_EXPECT(ts.db.key_path() == ts.kp);
        BEAST_EXPECT(ts.db.log_path() == ts.lp);
        BEAST_EXPECT(ts.db.appnum() == ts.appnum);
        BEAST_EXPECT(ts.db.key_size() == ts.keySize);
        BEAST_EXPECT(ts.db.block_size() == ts.blockSize);
    }

    // Inserts a bunch of values then fetches them
    void
    do_insert_fetch(
        std::size_t N,
        std::size_t keySize,
        std::size_t blockSize,
        float loadFactor,
        bool sleep)
    {
        testcase <<
            "N=" << N << ", "
            "keySize=" << keySize << ", "
            "blockSize=" << blockSize;
        error_code ec;
        test_store ts{keySize, blockSize, loadFactor};
        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ts.open(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // Insert
        for(std::size_t n = 0; n < N; ++n)
        {
            auto const item = ts[n];
            ts.db.insert(item.key, item.data, item.size, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
        // Fetch
        for(std::size_t n = 0; n < N; ++n)
        {
            auto const item = ts[n];
            ts.db.fetch(item.key,
                [&](void const* data, std::size_t size)
                {
                    if(! BEAST_EXPECT(size == item.size))
                        return;
                    BEAST_EXPECT(
                        std::memcmp(data, item.data, size) == 0);
                }, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
        // Insert Duplicate
        for(std::size_t n = 0; n < N; ++n)
        {
            auto const item = ts[n];
            ts.db.insert(item.key, item.data, item.size, ec);
            if(! BEAST_EXPECTS(
                    ec == error::key_exists, ec.message()))
                return;
            ec = {};
        }
        // Insert and Fetch
        if(keySize > 1)
        {
            for(std::size_t n = 0; n < N; ++n)
            {
                auto item = ts[n];
                ts.db.fetch(item.key,
                    [&](void const* data, std::size_t size)
                    {
                        if(! BEAST_EXPECT(size == item.size))
                            return;
                        BEAST_EXPECT(
                            std::memcmp(data, item.data, size) == 0);
                    }, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    return;
                item = ts[N + n];
                ts.db.insert(item.key, item.data, item.size, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    return;
                ts.db.fetch(item.key,
                    [&](void const* data, std::size_t size)
                    {
                        if(! BEAST_EXPECT(size == item.size))
                            return;
                        BEAST_EXPECT(
                            std::memcmp(data, item.data, size) == 0);
                    }, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    return;
            }
        }
        if(sleep)
        {
            // Make sure we run periodic activity
            std::this_thread::sleep_for(
                std::chrono::milliseconds{3000});
        }
        ts.close(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
    }

    // Perform insert/fetch test across a range of parameters
    void
    test_insert_fetch()
    {
        for(auto const keySize : {
            1, 2, 3, 31, 32, 33, 63, 64, 65, 95, 96, 97 })
        {
            std::size_t N;
            std::size_t constexpr blockSize = 4096;
            float loadFactor = 0.95f;
            switch(keySize)
            {
            case 1: N = 10; break;
            case 2: N = 100; break;
            case 3: N = 250; break;
            default:
                N = 5000;
                break;
            };
            do_insert_fetch(N, keySize, blockSize, loadFactor,
                keySize == 97);
        }
    }

    void
    test_bulk_insert(std::size_t N, std::size_t keySize,
        std::size_t blockSize, float loadFactor)
    {
        testcase <<
            "bulk_insert N=" << N << ", "
            "keySize=" << keySize << ", "
            "blockSize=" << blockSize;
        error_code ec;
        test_store ts{keySize, blockSize, loadFactor};
        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ts.open(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // Insert
        for(std::size_t n = 0; n < N; ++n)
        {
            auto const item = ts[n];
            ts.db.insert(item.key, item.data, item.size, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
        ts.close(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        verify_info info;
        verify<xxhasher>(info, ts.dp, ts.kp,
            64 * 1024 * 1024 , no_progress{}, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        log << info;
    }

    void
    run() override
    {
#if 1
        test_members();
        test_insert_fetch();
#else
        // bulk-insert performance test
        test_bulk_insert(10000000, 8, 4096, 0.5f);
#endif
    }
};

BEAST_DEFINE_TESTSUITE(basic_store, test, nudb);

} // test
} // nudb

