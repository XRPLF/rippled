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
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/protocol/BuildInfo.h>
#include <boost/preprocessor/stringize.hpp>
#include <algorithm>

namespace ripple {

namespace BuildInfo {

//--------------------------------------------------------------------------
//  The build version number. You must edit this for each release
//  and follow the format described at http://semver.org/
//------------------------------------------------------------------------------
// clang-format off
char const* const versionString = "1.7.0-b3"
// clang-format on

#if defined(DEBUG) || defined(SANITIZER)
    "+"
#ifdef DEBUG
    "DEBUG"
#ifdef SANITIZER
    "."
#endif
#endif

#ifdef SANITIZER
    BOOST_PP_STRINGIZE(SANITIZER)
#endif
#endif

    //--------------------------------------------------------------------------
    ;

//
// Don't touch anything below this line
//

std::string const&
getVersionString()
{
    static std::string const value = [] {
        std::string const s = versionString;
        beast::SemanticVersion v;
        if (!v.parse(s) || v.print() != s)
            LogicError(s + ": Bad server version string");
        return s;
    }();
    return value;
}

std::string const&
getFullVersionString()
{
    static std::string const value = "rippled-" + getVersionString();
    return value;
}

static constexpr std::uint64_t implementationVersionIdentifier =
    0x183B'0000'0000'0000LLU;
static constexpr std::uint64_t implementationVersionIdentifierMask =
    0xFFFF'0000'0000'0000LLU;

std::uint64_t
encodeSoftwareVersion(char const* const versionStr)
{
    std::uint64_t c = implementationVersionIdentifier;

    beast::SemanticVersion v;

    if (v.parse(std::string(versionStr)))
    {
        if (v.majorVersion >= 0 && v.majorVersion <= 255)
            c |= static_cast<std::uint64_t>(v.majorVersion) << 40;

        if (v.minorVersion >= 0 && v.minorVersion <= 255)
            c |= static_cast<std::uint64_t>(v.minorVersion) << 32;

        if (v.patchVersion >= 0 && v.patchVersion <= 255)
            c |= static_cast<std::uint64_t>(v.patchVersion) << 24;

        if (!v.isPreRelease())
            c |= static_cast<std::uint64_t>(0xC00000);

        if (v.isPreRelease())
        {
            std::uint8_t x = 0;

            for (auto id : v.preReleaseIdentifiers)
            {
                auto parsePreRelease = [](std::string_view identifier,
                                          std::string_view prefix,
                                          std::uint8_t key,
                                          std::uint8_t lok,
                                          std::uint8_t hik) -> std::uint8_t {
                    std::uint8_t ret = 0;

                    if (prefix != identifier.substr(0, prefix.length()))
                        return 0;

                    if (!beast::lexicalCastChecked(
                            ret,
                            std::string(identifier.substr(prefix.length()))))
                        return 0;

                    if (std::clamp(ret, lok, hik) != ret)
                        return 0;

                    return ret + key;
                };

                x = parsePreRelease(id, "rc", 0x80, 0, 63);

                if (x == 0)
                    x = parsePreRelease(id, "b", 0x40, 0, 63);

                if (x & 0xC0)
                {
                    c |= static_cast<std::uint64_t>(x) << 16;
                    break;
                }
            }
        }
    }

    return c;
}

std::uint64_t
getEncodedVersion()
{
    static std::uint64_t const cookie = {encodeSoftwareVersion(versionString)};
    return cookie;
}

bool
isRippledVersion(std::uint64_t version)
{
    return (version & implementationVersionIdentifierMask) ==
        implementationVersionIdentifier;
}

bool
isNewerVersion(std::uint64_t version)
{
    if (isRippledVersion(version))
        return version > getEncodedVersion();
    return false;
}

}  // namespace BuildInfo

}  // namespace ripple
