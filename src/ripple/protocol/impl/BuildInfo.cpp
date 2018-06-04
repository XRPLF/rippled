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

#include <ripple/basics/contract.h>
#include <ripple/beast/core/PlatformConfig.h>
#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/protocol/BuildInfo.h>

namespace ripple {

namespace BuildInfo {

//------------------------------------------------------------------------------
char const* const versionString =

    //--------------------------------------------------------------------------
    //  The build version number. You must edit this for each release
    //  and follow the format described at http://semver.org/
    //
        "1.0.1"

#if defined(DEBUG) || defined(SANITIZER)
       "+"
#ifdef DEBUG
        "DEBUG"
#ifdef SANITIZER
        "."
#endif
#endif

#ifdef SANITIZER
        BEAST_PP_STR1_(SANITIZER)
#endif
#endif

    //--------------------------------------------------------------------------
    ;

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
    static std::string const value = [] {
        std::string const s = versionString;
        beast::SemanticVersion v;
        if (!v.parse (s) || v.print () != s)
            LogicError (s + ": Bad server version string");
        return s;
    }();
    return value;
}

std::string const& getFullVersionString ()
{
    static std::string const value =
        "rippled-" + getVersionString();
    return value;
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
