//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/create.hpp>

#include <nudb/test/test_store.hpp>
#include <nudb/create.hpp>
#include <beast/unit_test/suite.hpp>

namespace nudb {
namespace test {

class create_test : public beast::unit_test::suite
{
public:
    void
    test_create()
    {
        std::size_t const keySize = 8;
        std::size_t const blockSize = 256;
        float const loadFactor = 0.5f;

        error_code ec;
        test_store ts{keySize, blockSize, loadFactor};
        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ts.create(ec);
        if(! BEAST_EXPECTS(
                ec == errc::file_exists, ec.message()))
            return;
    }

    void
    run() override
    {
        test_create();
    }
};

BEAST_DEFINE_TESTSUITE(create, test, nudb);

} // test
} // nudb
