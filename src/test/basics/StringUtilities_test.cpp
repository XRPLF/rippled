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

#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/ToString.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class StringUtilities_test : public beast::unit_test::suite
{
public:
    void testUnHexSuccess (std::string const& strIn, std::string const& strExpected)
    {
        auto rv = strUnHex (strIn);
        BEAST_EXPECT(rv.second);
        BEAST_EXPECT(makeSlice(rv.first) == makeSlice(strExpected));
    }

    void testUnHexFailure (std::string const& strIn)
    {
        auto rv = strUnHex (strIn);
        BEAST_EXPECT(! rv.second);
        BEAST_EXPECT(rv.first.empty());
    }

    void testUnHex ()
    {
        testcase ("strUnHex");

        testUnHexSuccess ("526970706c6544", "RippleD");
        testUnHexSuccess ("A", "\n");
        testUnHexSuccess ("0A", "\n");
        testUnHexSuccess ("D0A", "\r\n");
        testUnHexSuccess ("0D0A", "\r\n");
        testUnHexSuccess ("200D0A", " \r\n");
        testUnHexSuccess ("282A2B2C2D2E2F29", "(*+,-./)");

        // Check for things which contain some or only invalid characters
        testUnHexFailure ("123X");
        testUnHexFailure ("V");
        testUnHexFailure ("XRP");
    }

    void testParseUrl ()
    {
        testcase ("parseUrl");

        parsedURL pUrl;

        BEAST_EXPECT(parseUrl (pUrl, "lower://domain"));
        BEAST_EXPECT(pUrl.scheme == "lower");
        BEAST_EXPECT(pUrl.domain == "domain");
        BEAST_EXPECT(! pUrl.port);
        BEAST_EXPECT(pUrl.path == "");
        BEAST_EXPECT(parseUrl (pUrl, "UPPER://domain:234/"));
        BEAST_EXPECT(pUrl.scheme == "upper");
        BEAST_EXPECT(*pUrl.port == 234);
        BEAST_EXPECT(pUrl.path == "/");
        BEAST_EXPECT(parseUrl (pUrl, "Mixed://domain/path"));
        BEAST_EXPECT(pUrl.scheme == "mixed");
        BEAST_EXPECT(pUrl.path == "/path");
        BEAST_EXPECT(parseUrl (pUrl, "scheme://[::1]:123/path"));
        BEAST_EXPECT(*pUrl.port == 123);
        BEAST_EXPECT(pUrl.domain == "::1");
    }

    void testToString ()
    {
        testcase ("toString");
        auto result = to_string("hello");
        BEAST_EXPECT(result == "hello");
    }

    void run () override
    {
        testParseUrl ();
        testUnHex ();
        testToString ();
    }
};

BEAST_DEFINE_TESTSUITE(StringUtilities, ripple_basics, ripple);

} // ripple
