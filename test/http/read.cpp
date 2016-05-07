//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/read.hpp>

#include <beast/http/streambuf_body.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/string_stream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/spawn.hpp>

namespace beast {
namespace http {

class read_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    void testRead(yield_context do_yield)
    {
        static std::size_t constexpr limit = 100;
        std::size_t n;
        for(n = 1; n < limit; ++n)
        {
            streambuf sb;
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request_v1<streambuf_body> m;
            try
            {
                read(fs, sb, m);
                break;
            }
            catch(std::exception const&)
            {
            }
        }
        expect(n < limit);
        for(n = 1; n < limit; ++n)
        {
            streambuf sb;
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request_v1<streambuf_body> m;
            error_code ec;
            read(fs, sb, m, ec);
            if(! ec)
                break;
        }
        expect(n < limit);
        ios_.post(
            [&]{
                n = 1;
            });
        for(n = 1; n < limit; ++n)
        {
            streambuf sb;
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request_v1<streambuf_body> m;
            error_code ec;
            async_read(fs, sb, m, do_yield[ec]);
            if(! ec)
                break;
        }
        expect(n < limit);
    }

    void run() override
    {
        yield_to(std::bind(&read_test::testRead,
            this, std::placeholders::_1));
    }
};

BEAST_DEFINE_TESTSUITE(read,http,beast);

} // http
} // beast

