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

#include <beast/http/URL.h>

#include <beast/unit_test/suite.h>

namespace beast {

class URL_test : public unit_test::suite
{
public:
    void check_url_parsing (std::string const& url, bool expected)
    {
        auto result = parse_URL (url);

        expect (result.first == expected,
            (expected ? "Failed to parse " : "Succeeded in parsing ") + url);
        expect (to_string (result.second) == url);
    }

    void test_url_parsing ()
    {
        char const* const urls[] = 
        {
            "http://en.wikipedia.org/wiki/URI#Examples_of_URI_references",
            "ftp://ftp.funet.fi/pub/standards/RFC/rfc959.txt"
            "ftp://test:test@example.com:21/path/specifier/is/here"
            "http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html",
            "foo://username:password@example.com:8042/over/there/index.dtb?type=animal&name=narwhal#nose",
        };

        testcase ("URL parsing");

        for (auto url : urls)
            check_url_parsing (url, true);
    }

    void
    run ()
    {
        test_url_parsing ();
    }
};

BEAST_DEFINE_TESTSUITE(URL,http,beast);

}
