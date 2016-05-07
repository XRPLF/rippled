//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/write_streambuf.hpp>
#include <beast/streambuf.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {

class write_streambuf_test : public beast::unit_test::suite
{
public:
    void run() override
    {
        streambuf sb;
        std::string s;
        write(sb, boost::asio::const_buffer{"", 0});
        write(sb, boost::asio::mutable_buffer{nullptr, 0});
        write(sb, boost::asio::null_buffers{});
        write(sb, boost::asio::const_buffers_1{"", 0});
        write(sb, boost::asio::mutable_buffers_1{nullptr, 0});
        write(sb, s);
        write(sb, 23);
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(write_streambuf,core,beast);

} // beast
