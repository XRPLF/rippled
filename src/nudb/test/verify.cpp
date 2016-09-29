//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/verify.hpp>

#include <nudb/test/test_store.hpp>
#include <nudb/progress.hpp>
#include <nudb/verify.hpp>
#include <beast/unit_test/suite.hpp>

namespace nudb {
namespace test {

class verify_test : public beast::unit_test::suite
{
public:
    // File doesn't exist
    void
    test_missing()
    {
        error_code ec;
        test_store ts{4, 4096, 0.5f};
        verify_info info;
        verify<xxhasher>(info,
            ts.dp, ts.kp, 0, no_progress{}, ec);
        BEAST_EXPECTS(ec ==
            errc::no_such_file_or_directory, ec.message());
    }

    void
    test_verify(
        std::size_t N,
        std::size_t keySize,
        std::size_t blockSize,
        float loadFactor)
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
        ts.close(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;

        // Verify
        verify_info info;
        verify<xxhasher>(info, ts.dp, ts.kp,
            0, no_progress{}, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        BEAST_EXPECT(info.hist[1] > 0);

        // Verify fast
        verify<xxhasher>(info, ts.dp, ts.kp,
            10 * 1024 * 1024, no_progress{}, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        BEAST_EXPECT(info.hist[1] > 0);
    }

    void
    run() override
    {
        float const loadFactor = 0.95f;
        test_missing();
        test_verify(5000, 4, 256, loadFactor);
    }
};

BEAST_DEFINE_TESTSUITE(verify, test, nudb);

} // test
} // nudb
