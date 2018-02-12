//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/read.hpp>

#include "test_parser.hpp"

#include <beast/core/ostream.hpp>
#include <beast/core/static_buffer.hpp>
#include <beast/http/fields.hpp>
#include <beast/http/dynamic_body.hpp>
#include <beast/http/parser.hpp>
#include <beast/http/string_body.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/pipe_stream.hpp>
#include <beast/test/string_istream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/spawn.hpp>
#include <atomic>

namespace beast {
namespace http {

class read_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    template<bool isRequest>
    void
    failMatrix(char const* s, yield_context do_yield)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        static std::size_t constexpr limit = 100;
        std::size_t n;
        auto const len = strlen(s);
        for(n = 0; n < limit; ++n)
        {
            multi_buffer b;
            b.commit(buffer_copy(
                b.prepare(len), buffer(s, len)));
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_istream> fs{fc, ios_, ""};
            test_parser<isRequest> p(fc);
            error_code ec = test::error::fail_error;
            read(fs, b, p, ec);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
        for(n = 0; n < limit; ++n)
        {
            static std::size_t constexpr pre = 10;
            multi_buffer b;
            b.commit(buffer_copy(
                b.prepare(pre), buffer(s, pre)));
            test::fail_counter fc(n);
            test::fail_stream<test::string_istream> fs{
                fc, ios_, std::string{s + pre, len - pre}};
            test_parser<isRequest> p(fc);
            error_code ec = test::error::fail_error;
            read(fs, b, p, ec);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
        for(n = 0; n < limit; ++n)
        {
            multi_buffer b;
            b.commit(buffer_copy(
                b.prepare(len), buffer(s, len)));
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_istream> fs{fc, ios_, ""};
            test_parser<isRequest> p(fc);
            error_code ec = test::error::fail_error;
            async_read(fs, b, p, do_yield[ec]);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
        for(n = 0; n < limit; ++n)
        {
            static std::size_t constexpr pre = 10;
            multi_buffer b;
            b.commit(buffer_copy(
                b.prepare(pre), buffer(s, pre)));
            test::fail_counter fc(n);
            test::fail_stream<test::string_istream> fs{
                fc, ios_, std::string{s + pre, len - pre}};
            test_parser<isRequest> p(fc);
            error_code ec = test::error::fail_error;
            async_read(fs, b, p, do_yield[ec]);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
    }

    void testThrow()
    {
        try
        {
            multi_buffer b;
            test::string_istream ss(ios_, "GET / X");
            request_parser<dynamic_body> p;
            read(ss, b, p);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
    }

    void
    testBufferOverflow()
    {
        {
            test::pipe p{ios_};
            ostream(p.server.buffer) <<
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                "10\r\n"
                "****************\r\n"
                "0\r\n\r\n";
            static_buffer_n<1024> b;
            request<string_body> req;
            try
            {
                read(p.server, b, req);
                pass();
            }
            catch(std::exception const& e)
            {
                fail(e.what(), __FILE__, __LINE__);
            }
        }
        {
            test::pipe p{ios_};
            ostream(p.server.buffer) <<
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                "10\r\n"
                "****************\r\n"
                "0\r\n\r\n";
            error_code ec = test::error::fail_error;
            static_buffer_n<10> b;
            request<string_body> req;
            read(p.server, b, req, ec);
            BEAST_EXPECTS(ec == error::buffer_overflow,
                ec.message());
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
            test::fail_stream<test::string_istream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request<dynamic_body> m;
            try
            {
                multi_buffer b;
                read(fs, b, m);
                break;
            }
            catch(std::exception const&)
            {
            }
        }
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_istream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request<dynamic_body> m;
            error_code ec = test::error::fail_error;
            multi_buffer b;
            read(fs, b, m, ec);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<test::string_istream> fs(n, ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 0\r\n"
                "\r\n"
            );
            request<dynamic_body> m;
            error_code ec = test::error::fail_error;
            multi_buffer b;
            async_read(fs, b, m, do_yield[ec]);
            if(! ec)
                break;
        }
        BEAST_EXPECT(n < limit);
    }

    void
    testEof(yield_context do_yield)
    {
        {
            multi_buffer b;
            test::string_istream ss(ios_, "");
            request_parser<dynamic_body> p;
            error_code ec;
            read(ss, b, p, ec);
            BEAST_EXPECT(ec == http::error::end_of_stream);
        }
        {
            multi_buffer b;
            test::string_istream ss(ios_, "");
            request_parser<dynamic_body> p;
            error_code ec;
            async_read(ss, b, p, do_yield[ec]);
            BEAST_EXPECT(ec == http::error::end_of_stream);
        }
    }

    // Ensure completion handlers are not leaked
    struct handler
    {
        static std::atomic<std::size_t>&
        count() { static std::atomic<std::size_t> n; return n; }
        handler() { ++count(); }
        ~handler() { --count(); }
        handler(handler const&) { ++count(); }
        void operator()(error_code const&) const {}
    };

    void
    testIoService()
    {
        {
            // Make sure handlers are not destroyed
            // after calling io_service::stop
            boost::asio::io_service ios;
            test::string_istream is{ios,
                "GET / HTTP/1.1\r\n\r\n"};
            BEAST_EXPECT(handler::count() == 0);
            multi_buffer b;
            request<dynamic_body> m;
            async_read(is, b, m, handler{});
            BEAST_EXPECT(handler::count() > 0);
            ios.stop();
            BEAST_EXPECT(handler::count() > 0);
            ios.reset();
            BEAST_EXPECT(handler::count() > 0);
            ios.run_one();
            BEAST_EXPECT(handler::count() == 0);
        }
        {
            // Make sure uninvoked handlers are
            // destroyed when calling ~io_service
            {
                boost::asio::io_service ios;
                test::string_istream is{ios,
                    "GET / HTTP/1.1\r\n\r\n"};
                BEAST_EXPECT(handler::count() == 0);
                multi_buffer b;
                request<dynamic_body> m;
                async_read(is, b, m, handler{});
                BEAST_EXPECT(handler::count() > 0);
            }
            BEAST_EXPECT(handler::count() == 0);
        }
    }

    // https://github.com/vinniefalco/Beast/issues/430
    void
    testRegression430()
    {
        test::pipe c{ios_};
        c.server.read_size(1);
        ostream(c.server.buffer) <<
          "HTTP/1.1 200 OK\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Content-Type: application/octet-stream\r\n"
          "\r\n"
          "4\r\nabcd\r\n"
          "0\r\n\r\n";
        error_code ec;
        flat_buffer fb;
        parser<false, dynamic_body> p;
        read(c.server, fb, p, ec);
        BEAST_EXPECTS(! ec, ec.message());
    }

    //--------------------------------------------------------------------------

    template<class Parser, class Pred>
    void
    readgrind(string_view s, Pred&& pred)
    {
        using boost::asio::buffer;
        for(std::size_t n = 1; n < s.size() - 1; ++n)
        {
            Parser p;
            error_code ec = test::error::fail_error;
            flat_buffer b;
            test::pipe c{ios_};
            ostream(c.server.buffer) << s;
            c.server.read_size(n);
            read(c.server, b, p, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                continue;
            pred(p);
        }
    }

    void
    testReadGrind()
    {
        readgrind<test_parser<false>>(
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Content-Type: application/octet-stream\r\n"
            "\r\n"
            "4\r\nabcd\r\n"
            "0\r\n\r\n"
            ,[&](test_parser<false> const& p)
            {
                BEAST_EXPECT(p.body == "abcd");
            });
        readgrind<test_parser<false>>(
            "HTTP/1.1 200 OK\r\n"
            "Server: test\r\n"
            "Expect: Expires, MD5-Fingerprint\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\n"
            "*****\r\n"
            "2;a;b=1;c=\"2\"\r\n"
            "--\r\n"
            "0;d;e=3;f=\"4\"\r\n"
            "Expires: never\r\n"
            "MD5-Fingerprint: -\r\n"
            "\r\n"
            ,[&](test_parser<false> const& p)
            {
                BEAST_EXPECT(p.body == "*****--");
            });
    }

    void
    run() override
    {
        testThrow();
        testBufferOverflow();

        yield_to([&](yield_context yield){
            testFailures(yield); });
        yield_to([&](yield_context yield){
            testRead(yield); });
        yield_to([&](yield_context yield){
            testEof(yield); });

        testIoService();
        testRegression430();
        testReadGrind();
    }
};

BEAST_DEFINE_TESTSUITE(read,http,beast);

} // http
} // beast
