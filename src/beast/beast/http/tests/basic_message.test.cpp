//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <beast/http/basic_message.h>
#include <beast/unit_test/suite.h>

namespace beast {
namespace http {

class basic_message_test : public beast::unit_test::suite
{
public:
    std::pair <basic_message, bool>
    request (std::string const& text)
    {
        basic_message m;
        basic_message::parser p (m, true);
        auto result (p.write (boost::asio::buffer(text)));
        auto result2 (p.eof());
        return std::make_pair (std::move(m), result.first);
    }

    void
    run()
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

        log << "|" << result.first.headers["Field"] << "|";

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(basic_message,http,beast);

} // http
} // beast
