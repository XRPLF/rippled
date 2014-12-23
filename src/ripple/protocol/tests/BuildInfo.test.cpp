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

#include <ripple/protocol/BuildInfo.h>
#include <beast/unit_test/suite.h>

namespace ripple {

class BuildInfo_test : public beast::unit_test::suite
{
public:
    void testVersion ()
    {
        testcase ("version");

        beast::SemanticVersion v;

        expect (v.parse (BuildInfo::getRawVersionString ()));
    }


    ProtocolVersion
    from_version (std::uint16_t major, std::uint16_t minor)
    {
        return ProtocolVersion (major, minor);
    }

    void testValues ()
    {
        testcase ("comparison");

        expect (from_version (1,2) == from_version (1,2));
        expect (from_version (3,4) >= from_version (3,4));
        expect (from_version (5,6) <= from_version (5,6));
        expect (from_version (7,8) >  from_version (6,7));
        expect (from_version (7,8) <  from_version (8,9));
        expect (from_version (65535,0) <  from_version (65535,65535));
        expect (from_version (65535,65535) >= from_version (65535,65535));
    }

    void testStringVersion ()
    {
        testcase ("string version");

        for (std::uint16_t major = 0; major < 8; major++)
        {
            for (std::uint16_t minor = 0; minor < 8; minor++)
            {
                expect (to_string (from_version (major, minor)) ==
                    std::to_string (major) + "." + std::to_string (minor));
            }
        }
    }

    void testVersionPacking ()
    {
        testcase ("version packing");

        expect (to_packed (from_version (0, 0)) == 0);
        expect (to_packed (from_version (0, 1)) == 1);
        expect (to_packed (from_version (0, 255)) == 255);
        expect (to_packed (from_version (0, 65535)) == 65535);

        expect (to_packed (from_version (1, 0)) == 65536);
        expect (to_packed (from_version (1, 1)) == 65537);
        expect (to_packed (from_version (1, 255)) == 65791);
        expect (to_packed (from_version (1, 65535)) == 131071);

        expect (to_packed (from_version (255, 0)) == 16711680);
        expect (to_packed (from_version (255, 1)) == 16711681);
        expect (to_packed (from_version (255, 255)) == 16711935);
        expect (to_packed (from_version (255, 65535)) == 16777215);

        expect (to_packed (from_version (65535, 0)) == 4294901760);
        expect (to_packed (from_version (65535, 1)) == 4294901761);
        expect (to_packed (from_version (65535, 255)) == 4294902015);
        expect (to_packed (from_version (65535, 65535)) == 4294967295);
    }

    void run ()
    {
        testVersion ();
        testValues ();
        testStringVersion ();
        testVersionPacking ();

        auto const current_protocol = BuildInfo::getCurrentProtocol ();
        auto const minimum_protocol = BuildInfo::getMinimumProtocol ();

        expect (current_protocol >= minimum_protocol);

        log << "   Ripple Version: " << BuildInfo::getVersionString();
        log << " Protocol Version: " << to_string (current_protocol);
    }
};

BEAST_DEFINE_TESTSUITE(BuildInfo,ripple_data,ripple);

} // ripple
