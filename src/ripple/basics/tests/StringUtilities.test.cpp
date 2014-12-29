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

#include <BeastConfig.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/ToString.h>
#include <beast/unit_test/suite.h>

namespace ripple {

class StringUtilities_test : public beast::unit_test::suite
{
public:
    void testUnHexSuccess (std::string strIn, std::string strExpected)
    {
        std::string strOut;

        expect (strUnHex (strOut, strIn) == strExpected.length (),
            "strUnHex: parsing correct input failed");

        expect (strOut == strExpected,
            "strUnHex: parsing doesn't produce expected result");
    }

    void testUnHexFailure (std::string strIn)
    {
        std::string strOut;

        expect (strUnHex (strOut, strIn) == -1,
            "strUnHex: parsing incorrect input succeeded");

        expect (strOut.empty (),
            "strUnHex: parsing incorrect input returned data");
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

        std::string strScheme;
        std::string strDomain;
        int         iPort;
        std::string strPath;

        unexpected (!parseUrl ("lower://domain", strScheme, strDomain, iPort, strPath),
            "parseUrl: lower://domain failed");

        unexpected (strScheme != "lower",
            "parseUrl: lower://domain : scheme failed");

        unexpected (strDomain != "domain",
            "parseUrl: lower://domain : domain failed");

        unexpected (iPort != -1,
            "parseUrl: lower://domain : port failed");

        unexpected (strPath != "",
            "parseUrl: lower://domain : path failed");

        unexpected (!parseUrl ("UPPER://domain:234/", strScheme, strDomain, iPort, strPath),
            "parseUrl: UPPER://domain:234/ failed");

        unexpected (strScheme != "upper",
            "parseUrl: UPPER://domain:234/ : scheme failed");

        unexpected (iPort != 234,
            boost::str (boost::format ("parseUrl: UPPER://domain:234/ : port failed: %d") % iPort));

        unexpected (strPath != "/",
            "parseUrl: UPPER://domain:234/ : path failed");

        unexpected (!parseUrl ("Mixed://domain/path", strScheme, strDomain, iPort, strPath),
            "parseUrl: Mixed://domain/path failed");

        unexpected (strScheme != "mixed",
            "parseUrl: Mixed://domain/path tolower failed");

        unexpected (strPath != "/path",
            "parseUrl: Mixed://domain/path path failed");
    }

    void testToString ()
    {
        testcase ("toString");
        auto result = to_string("hello");
        expect(result == "hello", result);
    }

    void run ()
    {
        testParseUrl ();
        testUnHex ();
        testToString ();
    }
};

BEAST_DEFINE_TESTSUITE(StringUtilities, ripple_basics, ripple);

} // ripple
