//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <nudb/test/test_store.hpp>
#include <beast/unit_test/suite.hpp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace nudb {
namespace test {

// This test is designed for callgrind runs to find hotspots

class callgrind_test : public beast::unit_test::suite
{
public:
    // Creates and opens a database, performs a bunch
    // of inserts, then alternates fetching all the keys
    // with keys not present.
    //
    void
    testCallgrind(std::size_t N)
    {
        using key_type = std::uint64_t;
        std::size_t const blockSize = 4096;
        float const loadFactor = 0.5;

        error_code ec;
        test_store ts{sizeof(key_type), blockSize, loadFactor};
        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ts.open(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        for(std::size_t i = 0; i < N; ++i)
        {
            auto const item = ts[i];
            ts.db.insert(item.key, item.data, item.size, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
        Buffer b;
        for(std::size_t i = 0; i < N * 2; ++i)
        {
            if(! (i%2))
            {
                auto const item = ts[i/2];
                ts.db.fetch(item.key, b, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    return;
                if(! BEAST_EXPECT(b.size() == item.size))
                    return;
                if(! BEAST_EXPECT(std::memcmp(b.data(),
                        item.data, item.size) == 0))
                    return;
            }
            else
            {
                auto const item = ts[N + i/2];
                ts.db.fetch(item.key, b, ec);
                if(! BEAST_EXPECTS(ec ==
                        error::key_not_found, ec.message()))
                    return;
                ec = {};
            }
        }
        ts.close(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
    }

    void run()
    {
        // higher numbers, more pain
        std::size_t constexpr N = 100000;

        testCallgrind(N);
    }
};

BEAST_DEFINE_TESTSUITE(callgrind, test, nudb);

} // test
} // nudb
