//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/read.hpp>

#include "fail_parser.hpp"

#include <beast/http/headers.hpp>
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
    template<bool isRequest>
    void failMatrix(const char* s, yield_context do_yield)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        static std::size_t constexpr limit = 100;
        std::size_t n;
        auto const len = strlen(s);
        for(n = 0; n < limit; ++n)
        {
            streambuf sb;
            sb.commit(buffer_copy(
                sb.prepare(len), buffer(s, len)));
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_stream> fs{fc, ios_, ""};
            fail_parser<isRequest> p(fc);
            error_code ec;
            parse(fs, sb, p, ec);
            if(! ec)
                break;
        }
        expect(n < limit);
        for(n = 0; n < limit; ++n)
        {
            static std::size_t constexpr pre = 10;
            streambuf sb;
            sb.commit(buffer_copy(
                sb.prepare(pre), buffer(s, pre)));
            test::fail_counter fc(n);
            test::fail_stream<test::string_stream> fs{
                fc, ios_, std::string{s + pre, len - pre}};
            fail_parser<isRequest> p(fc);
            error_code ec;
            parse(fs, sb, p, ec);
            if(! ec)
                break;
        }
        expect(n < limit);
        for(n = 0; n < limit; ++n)
        {
            streambuf sb;
            sb.commit(buffer_copy(
                sb.prepare(len), buffer(s, len)));
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_stream> fs{fc, ios_, ""};
            fail_parser<isRequest> p(fc);
            error_code ec;
            async_parse(fs, sb, p, do_yield[ec]);
            if(! ec)
                break;
        }
        expect(n < limit);
        for(n = 0; n < limit; ++n)
        {
            static std::size_t constexpr pre = 10;
            streambuf sb;
            sb.commit(buffer_copy(
                sb.prepare(pre), buffer(s, pre)));
            test::fail_counter fc(n);
            test::fail_stream<test::string_stream> fs{
                fc, ios_, std::string{s + pre, len - pre}};
            fail_parser<isRequest> p(fc);
            error_code ec;
            async_parse(fs, sb, p, do_yield[ec]);
            if(! ec)
                break;
        }
        expect(n < limit);
    }

    void testThrow()
    {
        try
        {
            streambuf sb;
            test::string_stream ss(ios_, "GET / X");
            parser_v1<true, streambuf_body, headers> p;
            parse(ss, sb, p);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
    }

    void testFailures(yield_context do_yield)
    {
        char const* req[] = {
            "GET / HTTP/1.0\r\n"
            "Host: localhost\r\n"
            "User-Agent: test\r\n"
            "Empty:\r\n"
            "\r\n"
            ,
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "User-Agent: test\r\n"
            "Content-Length: 2\r\n"
            "\r\n"
            "**"
            ,
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "User-Agent: test\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "10\r\n"
            "****************\r\n"
            "0\r\n\r\n"
            ,
            nullptr
        };

        char const* res[] = {
            "HTTP/1.0 200 OK\r\n"
            "Server: test\r\n"
            "\r\n"
            ,
            "HTTP/1.0 200 OK\r\n"
            "Server: test\r\n"
            "\r\n"
            "***"
            ,
            "HTTP/1.1 200 OK\r\n"
            "Server: test\r\n"
            "Content-Length: 3\r\n"
            "\r\n"
            "***"
            ,
            "HTTP/1.1 200 OK\r\n"
            "Server: test\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "10\r\n"
            "****************\r\n"
            "0\r\n\r\n"
            ,
            nullptr
        };

        for(std::size_t i = 0; req[i]; ++i)
            failMatrix<true>(req[i], do_yield);

        for(std::size_t i = 0; res[i]; ++i)
            failMatrix<false>(res[i], do_yield);
    }

    void testRead(yield_context do_yield)
    {
        static std::size_t constexpr limit = 100;
        std::size_t n;

        for(n = 0; n < limit; ++n)
        {
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
                streambuf sb;
                read(fs, sb, m);
                break;
            }
            catch(std::exception const&)
            {
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request_v1<streambuf_body> m;
            error_code ec;
            streambuf sb;
            read(fs, sb, m, ec);
            if(! ec)
                break;
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request_v1<streambuf_body> m;
            error_code ec;
            streambuf sb;
            async_read(fs, sb, m, do_yield[ec]);
            if(! ec)
                break;
        }
        expect(n < limit);
    }

    void testEof(yield_context do_yield)
    {
        {
            streambuf sb;
            test::string_stream ss(ios_, "");
            parser_v1<true, streambuf_body, headers> p;
            error_code ec;
            parse(ss, sb, p, ec);
            expect(ec == boost::asio::error::eof);
        }
        {
            streambuf sb;
            test::string_stream ss(ios_, "");
            parser_v1<true, streambuf_body, headers> p;
            error_code ec;
            async_parse(ss, sb, p, do_yield[ec]);
            expect(ec == boost::asio::error::eof);
        }
    }

    void run() override
    {
        testThrow();

        yield_to(std::bind(&read_test::testFailures,
            this, std::placeholders::_1));

        yield_to(std::bind(&read_test::testRead,
            this, std::placeholders::_1));

        yield_to(std::bind(&read_test::testEof,
            this, std::placeholders::_1));
    }
};

BEAST_DEFINE_TESTSUITE(read,http,beast);

} // http
} // beast

