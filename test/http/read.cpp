//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/read.hpp>

#include "fail_parser.hpp"

#include <beast/http/fields.hpp>
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
    struct fail_body
    {
        class reader;

        class value_type
        {
            friend class reader;

            std::string s_;
            test::fail_counter& fc_;

        public:
            explicit
            value_type(test::fail_counter& fc)
                : fc_(fc)
            {
            }

            value_type&
            operator=(std::string s)
            {
                s_ = std::move(s);
                return *this;
            }
        };

        class reader
        {
            value_type& body_;

        public:
            template<bool isRequest, class Allocator>
            explicit
            reader(message<isRequest, fail_body, Allocator>& msg) noexcept
                : body_(msg.body)
            {
            }

            void
            init(error_code& ec) noexcept
            {
                body_.fc_.fail(ec);
            }

            void
            write(void const* data,
                std::size_t size, error_code& ec) noexcept
            {
                if(body_.fc_.fail(ec))
                    return;
            }
        };
    };

    template<bool isRequest>
    void failMatrix(char const* s, yield_context do_yield)
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
        BEAST_EXPECT(n < limit);
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
        BEAST_EXPECT(n < limit);
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
        BEAST_EXPECT(n < limit);
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
        BEAST_EXPECT(n < limit);
        for(n = 0; n < limit; ++n)
        {
            streambuf sb;
            sb.commit(buffer_copy(
                sb.prepare(len), buffer(s, len)));
            test::fail_counter fc{n};
            test::string_stream ss{ios_, s};
            parser_v1<isRequest, fail_body, fields> p{fc};
            error_code ec;
            parse(ss, sb, p, ec);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
    }

    void testThrow()
    {
        try
        {
            streambuf sb;
            test::string_stream ss(ios_, "GET / X");
            parser_v1<true, streambuf_body, fields> p;
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

    void testReadHeaders(yield_context do_yield)
    {
        static std::size_t constexpr limit = 100;
        std::size_t n;

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_stream> fs{n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
            };
            request_header m;
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
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request_header m;
            error_code ec;
            streambuf sb;
            async_read(fs, sb, m, do_yield[ec]);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
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
            request<streambuf_body> m;
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
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request<streambuf_body> m;
            error_code ec;
            streambuf sb;
            read(fs, sb, m, ec);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_stream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request<streambuf_body> m;
            error_code ec;
            streambuf sb;
            async_read(fs, sb, m, do_yield[ec]);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
    }

    void testEof(yield_context do_yield)
    {
        {
            streambuf sb;
            test::string_stream ss(ios_, "");
            parser_v1<true, streambuf_body, fields> p;
            error_code ec;
            parse(ss, sb, p, ec);
            BEAST_EXPECT(ec == boost::asio::error::eof);
        }
        {
            streambuf sb;
            test::string_stream ss(ios_, "");
            parser_v1<true, streambuf_body, fields> p;
            error_code ec;
            async_parse(ss, sb, p, do_yield[ec]);
            BEAST_EXPECT(ec == boost::asio::error::eof);
        }
    }

    void run() override
    {
        testThrow();

        yield_to(std::bind(&read_test::testFailures,
            this, std::placeholders::_1));

        yield_to(std::bind(&read_test::testReadHeaders,
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

