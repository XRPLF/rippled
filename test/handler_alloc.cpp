//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/to_string.hpp>

#include <beast/detail/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>

namespace beast {

class to_string_test : public beast::detail::unit_test::suite
{
public:
    void run()
    {
        expect(to_string(boost::asio::const_buffers_1("x", 1)) == "x");
    }
};

BEAST_DEFINE_TESTSUITE(to_string,core,beast);

} // beast

