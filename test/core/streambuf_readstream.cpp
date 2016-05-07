//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/streambuf_readstream.hpp>

#include <beast/core/streambuf.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio.hpp>

namespace beast {

class streambuf_readstream_test : public beast::unit_test::suite
{
public:
    void testSpecial()
    {
        using socket_type = boost::asio::ip::tcp::socket;
        boost::asio::io_service ios;
        {
            streambuf_readstream<socket_type, streambuf> srs(ios);
            streambuf_readstream<socket_type, streambuf> srs2(std::move(srs));
            srs = std::move(srs2);
            expect(&srs.get_io_service() == &ios);
            expect(&srs.get_io_service() == &srs2.get_io_service());
        }
        {
            socket_type sock(ios);
            streambuf_readstream<socket_type&, streambuf> srs(sock);
            streambuf_readstream<socket_type&, streambuf> srs2(std::move(srs));
        }
        pass();
    }

    void run() override
    {
        testSpecial();
    }
};

BEAST_DEFINE_TESTSUITE(streambuf_readstream,core,beast);

} // beast

