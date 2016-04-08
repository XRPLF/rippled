//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <beast/http/message.h>
#include <beast/http/parser.h>
#include <beast/unit_test/suite.h>
#include <utility>

namespace beast {
namespace deprecated_http {
namespace test {

class parser_test : public beast::unit_test::suite
{
public:
    message
    request(std::string const& text)
    {
        body b;
        message m;
        parser p(m, b, true);
        auto const used =
            p.write(boost::asio::buffer(text));
        expect(used == text.size());
        p.write_eof();
        return m;
    }

    void
    test_headers()
    {
        beast::http::headers<std::allocator<char>> h;
        h.insert("Field", "Value");
        expect (h.erase("Field") == 1);
    }

    void
    run()
    {
        test_headers();

        {
            std::string const text =
                "GET / HTTP/1.1\r\n"
                "\r\n"
                ;
            body b;
            message m;
            parser p (m, b, true);
            auto const used = p.write(
                boost::asio::buffer(text));
            expect(used == text.size());
            p.write_eof();
            expect(p.complete());
        }

        {
            // malformed
            std::string const text =
                "GET\r\n"
                "\r\n"
                ;
            body b;
            message m;
            parser p(m, b, true);
            boost::system::error_code ec;
            p.write(boost::asio::buffer(text), ec);
            if(expect(ec))
                expect(ec.message() == "invalid HTTP method");
        }
    }
};

BEAST_DEFINE_TESTSUITE(parser,http,beast);

} // test
} // deprecated_http
} // beast
