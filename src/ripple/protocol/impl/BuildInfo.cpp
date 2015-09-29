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
#include <ripple/protocol/BuildInfo.h>
#include <beast/unit_test/suite.h>
#include <beast/module/core/diagnostic/FatalError.h>
#include <beast/module/core/diagnostic/SemanticVersion.h>

namespace ripple {

namespace BuildInfo {

char const* getRawVersionString ()
{
    static char const* const rawText =

    //--------------------------------------------------------------------------
    //
    //  The build version number (edit this for each release)
    //
        "0.30.0-b1"
    //
    //  Must follow the format described here:
    //
    //  http://semver.org/
    //
#ifdef DEBUG
        "+DEBUG"
#endif
    //--------------------------------------------------------------------------
    ;

    return rawText;
}

ProtocolVersion const&
getCurrentProtocol ()
{
    static ProtocolVersion currentProtocol (
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

ProtocolVersion const&
getMinimumProtocol ()
{
    static ProtocolVersion minimumProtocol (

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

std::string const&
getVersionString ()
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

        std::string versionString;
    };

    static SanityChecker const value;

    return value.versionString;
}

std::string const& getFullVersionString ()
{
    struct PrettyPrinter
    {
        PrettyPrinter ()
        {
            fullVersionString = "rippled-" + getVersionString ();
        }

        std::string fullVersionString;
    };

    static PrettyPrinter const value;

    return value.fullVersionString;
}

ProtocolVersion
make_protocol (std::uint32_t version)
{
    return ProtocolVersion(
        static_cast<std::uint16_t> ((version >> 16) & 0xffff),
        static_cast<std::uint16_t> (version & 0xffff));
}

}

std::string
to_string (ProtocolVersion const& p)
{
    return std::to_string (p.first) + "." + std::to_string (p.second);
}

std::uint32_t
to_packed (ProtocolVersion const& p)
{
    return (static_cast<std::uint32_t> (p.first) << 16) + p.second;
}

} // ripple
