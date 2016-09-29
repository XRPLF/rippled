//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/rekey.hpp>

#include <nudb/test/fail_file.hpp>
#include <nudb/test/test_store.hpp>
#include <nudb/progress.hpp>
#include <nudb/verify.hpp>
#include <beast/unit_test/suite.hpp>

namespace nudb {
namespace test {

// Simple test to check that rekey works, and
// also to exercise all its failure paths.
//
class rekey_test : public beast::unit_test::suite
{
public:
    void
    do_recover(
        std::size_t N, nsize_t blockSize, float loadFactor)
    {
        using key_type = std::uint32_t;

        auto const keys = static_cast<std::size_t>(
            loadFactor * detail::bucket_capacity(blockSize));
        std::size_t const bufferSize =
            (blockSize * (1 + ((N + keys - 1) / keys)))
                / 2;
        error_code ec;
        test_store ts{sizeof(key_type), blockSize, loadFactor};
        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ts.open(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // Insert
        for(std::size_t i = 0; i < N; ++i)
        {
            auto const item = ts[i];
            ts.db.insert(item.key, item.data, item.size, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
        ts.close(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // Verify
        verify_info info;
        verify<xxhasher>(
            info, ts.dp, ts.kp, bufferSize, no_progress{}, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        if(! BEAST_EXPECT(info.value_count == N))
            return;
        if(! BEAST_EXPECT(info.spill_count > 0))
            return;
        // Rekey
        auto const kp2 = ts.kp + "2";
        for(std::size_t n = 1;; ++n)
        {
            fail_counter fc{n};
            rekey<xxhasher, fail_file<native_file>>(
                ts.dp, kp2, ts.lp, blockSize, loadFactor,
                    N, bufferSize, ec, no_progress{}, fc);
            if(! ec)
                break;
            if(! BEAST_EXPECTS(ec ==
                    test::test_error::failure, ec.message()))
                return;
            ec = {};
            recover<xxhasher, native_file>(
                ts.dp, kp2, ts.lp, ec);
            if(ec == error::no_key_file ||
                ec == errc::no_such_file_or_directory)
            {
                ec = {};
                continue;
            }
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            native_file::erase(kp2, ec);
            if(ec == errc::no_such_file_or_directory)
                ec = {};
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            // Verify
            verify<xxhasher>(info, ts.dp, ts.kp,
                bufferSize, no_progress{}, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            if(! BEAST_EXPECT(info.value_count == N))
                return;
        }
        // Verify
        verify<xxhasher>(info, ts.dp, ts.kp,
            bufferSize, no_progress{}, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        if(! BEAST_EXPECT(info.value_count == N))
            return;
        verify<xxhasher>(info, ts.dp, kp2,
            bufferSize, no_progress{}, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        if(! BEAST_EXPECT(info.value_count == N))
            return;
    }

    void
    run() override
    {
        enum
        {
            N =         50000,
            blockSize = 256
        };

        float const loadFactor = 0.95f;

        do_recover(N, blockSize, loadFactor);
    }
};

BEAST_DEFINE_TESTSUITE(rekey, test, nudb);

} // test
} // nudb
