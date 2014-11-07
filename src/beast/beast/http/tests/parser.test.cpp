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
namespace http {

class message_test : public beast::unit_test::suite
{
public:
    std::pair <message, bool>
    request (std::string const& text)
    {
        message m;
        body b;
        parser p (m, b, true);
        auto result (p.write (boost::asio::buffer(text)));
        p.write_eof();
        return std::make_pair (std::move(m), result.first);
    }

    void
    dump()
    {
        auto const result = request (
            "GET / HTTP/1.1\r\n"
            //"Connection: Upgrade\r\n"
            //"Upgrade: Ripple\r\n"
            "Field: \t Value \t \r\n"
            "Blib: Continu\r\n"
            "  ation\r\n"
            "Field: Hey\r\n"
            "Content-Length: 1\r\n"
            "\r\n"
            "x"
            );
        log << result.first.headers;
        log << "|" << result.first.headers["Field"] << "|";
    }

    void
    test_headers()
    {
        headers h;
        h.append("Field", "Value");
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
            message m;
            body b;
            parser p (m, b, true);
            auto result (p.write (boost::asio::buffer(text)));
            expect (! result.first);
            auto result2 (p.write_eof());
            expect (! result2);
            expect (p.complete());
        }

        {
            // malformed
            std::string const text =
                "GET\r\n"
                "\r\n"
                ;
            message m;
            body b;
            parser p (m, b, true);
            auto result = p.write (boost::asio::buffer(text));
            if (expect (result.first))
                expect (result.first.message() == "invalid HTTP method");
        }
    }
};

BEAST_DEFINE_TESTSUITE(message,http,beast);

} // http
} // beast
