//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/to_string.hpp>

#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>

namespace beast {

class to_string_test : public beast::unit_test::suite
{
public:
    void run()
    {
        BEAST_EXPECT(to_string(boost::asio::const_buffers_1("x", 1)) == "x");
    }
};

BEAST_DEFINE_TESTSUITE(to_string,core,beast);

} // beast

