//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/parser.hpp>

#include <beast/http/headers.hpp>
#include <beast/http/string_body.hpp>
#include <beast/detail/unit_test/suite.hpp>

namespace beast {
namespace http {

class parser_test : public beast::detail::unit_test::suite
{
public:
    void run() override
    {
        {
            error_code ec;
            parser<true, string_body,
                basic_headers<std::allocator<char>>> p;
            std::string const s =
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            p.write(s.data(), s.size(), ec);
            expect(! ec);
            expect(p.complete());
            auto m = p.release();
            expect(m.method == method_t::http_get);
            expect(m.url == "/");
            expect(m.version == 11);
            expect(m.headers["User-Agent"] == "test");
            expect(m.body == "*");
        }
        {
            error_code ec;
            parser<false, string_body,
                basic_headers<std::allocator<char>>> p;
            std::string const s =
                "HTTP/1.1 200 OK\r\n"
                "Server: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            p.write(s.data(), s.size(), ec);
            expect(! ec);
            expect(p.complete());
            auto m = p.release();
            expect(m.status == 200);
            expect(m.reason == "OK");
            expect(m.version == 11);
            expect(m.headers["Server"] == "test");
            expect(m.body == "*");
        }
    }
};

BEAST_DEFINE_TESTSUITE(parser,http,beast);

} // http
} // beast
