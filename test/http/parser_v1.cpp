//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/parser_v1.hpp>

#include <beast/http/headers.hpp>
#include <beast/http/string_body.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {
namespace http {

class parser_v1_test : public beast::unit_test::suite
{
public:
    void run() override
    {
        using boost::asio::buffer;
        {
            error_code ec;
            parser_v1<true, string_body,
                basic_headers<std::allocator<char>>> p;
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
            BEAST_EXPECT(m.headers["User-Agent"] == "test");
            BEAST_EXPECT(m.body == "*");
        }
        {
            error_code ec;
            parser_v1<false, string_body,
                basic_headers<std::allocator<char>>> p;
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
            BEAST_EXPECT(m.headers["Server"] == "test");
            BEAST_EXPECT(m.body == "*");
        }
        // skip body
        {
            error_code ec;
            parser_v1<false, string_body, headers> p;
            std::string const s =
                "HTTP/1.1 200 Connection Established\r\n"
                "Proxy-Agent: Zscaler/5.1\r\n"
                "\r\n";
            p.set_option(skip_body{true});
            p.write(buffer(s), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
        }
    }
};

BEAST_DEFINE_TESTSUITE(parser_v1,http,beast);

} // http
} // beast
