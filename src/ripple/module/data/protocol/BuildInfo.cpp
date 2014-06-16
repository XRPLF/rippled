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

#include <beast/unit_test/suite.h>
#include <beast/module/core/diagnostic/FatalError.h>
#include <beast/module/core/diagnostic/SemanticVersion.h>

namespace ripple {

char const* BuildInfo::getRawVersionString ()
{
    static char const* const rawText =

    //--------------------------------------------------------------------------
    //
    //  The build version number (edit this for each release)
    //
        "0.25.1"
    //
    //  Must follow the format described here:
    //
    //  http://semver.org/
    //
    //--------------------------------------------------------------------------
    ;

    return rawText;
}

BuildInfo::Protocol const& BuildInfo::getCurrentProtocol ()
{
    static Protocol currentProtocol (
    
    //--------------------------------------------------------------------------
    //
    // The protocol version we speak and prefer (edit this if necessary)
    //
        1,  // major
        2   // minor
    //
    //--------------------------------------------------------------------------
    );

    return currentProtocol;
}

BuildInfo::Protocol const& BuildInfo::getMinimumProtocol ()
{
    static Protocol minimumProtocol (

    //--------------------------------------------------------------------------
    //
    // The oldest protocol version we will accept. (edit this if necessary)
    //
        1,  // major
        2   // minor
    //
    //--------------------------------------------------------------------------
    );

    return minimumProtocol;
}

//
//
// Don't touch anything below this line
//
//------------------------------------------------------------------------------

beast::String const& BuildInfo::getVersionString ()
{
    struct SanityChecker
    {
        SanityChecker ()
        {
            beast::SemanticVersion v;

            char const* const rawText = getRawVersionString ();

            if (! v.parse (rawText) || v.print () != rawText)
                beast::FatalError ("Bad server version string", __FILE__, __LINE__);

            versionString = rawText;
        }

        beast::String versionString;
    };

    static SanityChecker value;

    return value.versionString;
}

char const* BuildInfo::getFullVersionString ()
{
    struct PrettyPrinter
    {
        PrettyPrinter ()
        {
            beast::String s;
            
            s << "Ripple-" << getVersionString ();

            fullVersionString = s.toStdString ();
        }

        std::string fullVersionString;
    };

    static PrettyPrinter value;

    return value.fullVersionString.c_str ();
}

//------------------------------------------------------------------------------

BuildInfo::Protocol::Protocol ()
    : vmajor (0)
    , vminor (0)
{
}

BuildInfo::Protocol::Protocol (unsigned short major_, unsigned short minor_)
    : vmajor (major_)
    , vminor (minor_)
{
}

BuildInfo::Protocol::Protocol (PackedFormat packedVersion)
{
    vmajor = (packedVersion >> 16) & 0xffff;
    vminor = (packedVersion & 0xffff);
}

BuildInfo::Protocol::PackedFormat BuildInfo::Protocol::toPacked () const noexcept
{
    return ((vmajor << 16) & 0xffff0000) | (vminor & 0xffff);
}

std::string BuildInfo::Protocol::toStdString () const noexcept
{
    beast::String s;

    s << beast::String (vmajor) << "." << beast::String (vminor);

    return s.toStdString ();
}

//------------------------------------------------------------------------------

class BuildInfo_test : public beast::unit_test::suite
{
public:
    void testVersion ()
    {
        testcase ("version");

        beast::SemanticVersion v;

        expect (v.parse (BuildInfo::getRawVersionString ()));
    }

    void checkProtcol (unsigned short vmajor, unsigned short vminor)
    {
        typedef BuildInfo::Protocol P;

        expect (P (P (vmajor, vminor).toPacked ()) == P (vmajor, vminor));
    }

    void testProtocol ()
    {
        typedef BuildInfo::Protocol P;

        testcase ("protocol");

        expect (P (0, 0).toPacked () == 0);
        expect (P (0, 1).toPacked () == 1);
        expect (P (0, 65535).toPacked () == 65535);

        checkProtcol (0, 0);
        checkProtcol (0, 1);
        checkProtcol (0, 255);
        checkProtcol (0, 65535);
        checkProtcol (1, 0);
        checkProtcol (1, 65535);
        checkProtcol (65535, 65535);
    }

    void testValues ()
    {
        testcase ("comparison");

        typedef BuildInfo::Protocol P;

        expect (P(1,2) == P(1,2));
        expect (P(3,4) >= P(3,4));
        expect (P(5,6) <= P(5,6));
        expect (P(7,8) >  P(6,7));
        expect (P(7,8) <  P(8,9));
        expect (P(65535,0) <  P(65535,65535));
        expect (P(65535,65535) >= P(65535,65535));

        expect (BuildInfo::getCurrentProtocol () >= BuildInfo::getMinimumProtocol ());
    }

    void run ()
    {
        testVersion ();
        testProtocol ();
        testValues ();

        log <<
            "  Ripple version: " <<
            BuildInfo::getVersionString().toStdString();
    }
};

BEAST_DEFINE_TESTSUITE(BuildInfo,ripple_data,ripple);

} // ripple
