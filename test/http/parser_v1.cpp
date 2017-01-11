//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/parser_v1.hpp>

#include <beast/core/streambuf.hpp>
#include <beast/http/fields.hpp>
#include <beast/http/header_parser_v1.hpp>
#include <beast/http/parse.hpp>
#include <beast/http/string_body.hpp>
#include <beast/test/string_stream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {
namespace http {

class parser_v1_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    void testRegressions()
    {
        using boost::asio::buffer;

        // consecutive empty header values
        {
            error_code ec;
            parser_v1<true, string_body, fields> p;
            std::string const s =
                "GET / HTTP/1.1\r\n"
                "X1:\r\n"
                "X2:\r\n"
                "X3:x\r\n"
                "\r\n";
            p.write(buffer(s), ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(p.complete());
            auto const msg = p.release();
            BEAST_EXPECT(msg.fields.exists("X1"));
            BEAST_EXPECT(msg.fields["X1"] == "");
            BEAST_EXPECT(msg.fields.exists("X2"));
            BEAST_EXPECT(msg.fields["X2"] == "");
            BEAST_EXPECT(msg.fields.exists("X3"));
            BEAST_EXPECT(msg.fields["X3"] == "x");
        }
    }

    void testWithBody()
    {
        test::string_stream ss{ios_,
            "GET / HTTP/1.1\r\n"
            "User-Agent: test\r\n"
            "Content-Length: 1\r\n"
            "\r\n"
            "*"};
        streambuf rb;
        header_parser_v1<true, fields> p0;
        parse(ss, rb, p0);
        request_header const& reqh = p0.get();
        BEAST_EXPECT(reqh.method == "GET");
        BEAST_EXPECT(reqh.url == "/");
        BEAST_EXPECT(reqh.version == 11);
        BEAST_EXPECT(reqh.fields["User-Agent"] == "test");
        BEAST_EXPECT(reqh.fields["Content-Length"] == "1");
        parser_v1<true, string_body, fields> p =
            with_body<string_body>(p0);
        BEAST_EXPECT(p.get().method == "GET");
        BEAST_EXPECT(p.get().url == "/");
        BEAST_EXPECT(p.get().version == 11);
        BEAST_EXPECT(p.get().fields["User-Agent"] == "test");
        BEAST_EXPECT(p.get().fields["Content-Length"] == "1");
        parse(ss, rb, p);
        request<string_body, fields> req = p.release();
        BEAST_EXPECT(req.body == "*");
    }

    void run() override
    {
        using boost::asio::buffer;
        {
            error_code ec;
            parser_v1<true, string_body,
                basic_fields<std::allocator<char>>> p;
            std::string const s =
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            p.write(buffer(s), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
            auto m = p.release();
            BEAST_EXPECT(m.method == "GET");
            BEAST_EXPECT(m.url == "/");
            BEAST_EXPECT(m.version == 11);
            BEAST_EXPECT(m.fields["User-Agent"] == "test");
            BEAST_EXPECT(m.body == "*");
        }
        {
            error_code ec;
            parser_v1<false, string_body,
                basic_fields<std::allocator<char>>> p;
            std::string const s =
                "HTTP/1.1 200 OK\r\n"
                "Server: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            p.write(buffer(s), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
            auto m = p.release();
            BEAST_EXPECT(m.status == 200);
            BEAST_EXPECT(m.reason == "OK");
            BEAST_EXPECT(m.version == 11);
            BEAST_EXPECT(m.fields["Server"] == "test");
            BEAST_EXPECT(m.body == "*");
        }
        // skip body
        {
            error_code ec;
            parser_v1<false, string_body, fields> p;
            std::string const s =
                "HTTP/1.1 200 Connection Established\r\n"
                "Proxy-Agent: Zscaler/5.1\r\n"
                "\r\n";
            p.set_option(skip_body{true});
            p.write(buffer(s), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
        }

        testRegressions();
        testWithBody();
    }
};

BEAST_DEFINE_TESTSUITE(parser_v1,http,beast);

} // http
} // beast
