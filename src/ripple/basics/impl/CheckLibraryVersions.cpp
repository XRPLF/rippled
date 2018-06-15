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
#include <ripple/basics/impl/CheckLibraryVersionsImpl.h>
#include <ripple/beast/core/SemanticVersion.h>
#include <boost/version.hpp>
#include <openssl/opensslv.h>
#include <sstream>
#include <vector>

namespace ripple {
namespace version {

std::string
boostVersion(VersionNumber boostVersion)
{
    std::stringstream ss;
    ss << (boostVersion / 100000) << "."
       << (boostVersion / 100 % 1000) << "."
       << (boostVersion % 100);
    return ss.str();
}

std::string
openSSLVersion(VersionNumber openSSLVersion)
{
    std::stringstream ss;
    ss << (openSSLVersion / 0x10000000L) << "."
       << (openSSLVersion / 0x100000 % 0x100) << "."
       << (openSSLVersion / 0x1000 % 0x100);
    auto patchNo = openSSLVersion % 0x10;
    if (patchNo)
        ss << '-' << char('a' + patchNo - 1);
    return ss.str();
}

void checkVersion(std::string name, std::string required, std::string actual)
{
    beast::SemanticVersion r, a;
    if (! r.parse(required))
    {
        Throw<std::runtime_error> (
            "Didn't understand required version of " + name + ": " + required);
    }

    if (! a.parse(actual))
    {
        Throw<std::runtime_error> (
            "Didn't understand actual version of " + name + ": " + required);
    }

    if (a < r)
    {
        Throw<std::runtime_error> (
            "Your " + name + " library is out of date.\n" + "Your version: " +
                actual + "\nRequired version: " + required + "\n");
    }
}

void checkBoost(std::string version)
{
    const char* boostMinimal = "1.67.0";
    checkVersion("Boost", boostMinimal, version);
}

void checkOpenSSL(std::string version)
{
    // The minimal version depends on whether we're linking
    // against 1.0.1 or later versions:
    beast::SemanticVersion v;

    char const* openSSLMinimal101 = "1.0.1-g";
    char const* openSSLMinimal102 = "1.0.2-j";

    if (v.parse (version) &&
            v.majorVersion == 1 &&
                v.minorVersion == 0 &&
                    v.patchVersion == 1)
    {
        // Use of the 1.0.1 series should be dropped as soon
        // as possible since as of January 2, 2017 it is no
        // longer supported. Unfortunately, a number of
        // platforms officially supported by Ripple still
        // use the 1.0.1 branch.
        //
        // Additionally, requiring 1.0.1u (the latest) is
        // similarly not possible, since those officially
        // supported platforms use older releases and
        // backport important fixes.
        checkVersion ("OpenSSL", openSSLMinimal101, version);
        return;
    }

    checkVersion ("OpenSSL", openSSLMinimal102, version);
}

void checkLibraryVersions()
{
    checkBoost(boostVersion(BOOST_VERSION));
    checkOpenSSL(openSSLVersion(OPENSSL_VERSION_NUMBER));
}

}  // namespace version
}  // namespace ripple
