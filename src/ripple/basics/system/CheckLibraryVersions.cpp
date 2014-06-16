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

#include <sstream>
#include <vector>

#include <beast/unit_test/suite.h>
#include <beast/module/core/diagnostic/SemanticVersion.h>
#include <boost/version.hpp>
#include <openssl/opensslv.h>

#include <ripple/basics/system/CheckLibraryVersions.h>

namespace ripple {
namespace version {

/** Both Boost and OpenSSL have integral version numbers. */
typedef unsigned long long VersionNumber;

/** Minimal required boost version. */
static const char boostMinimal[] = "1.55.0";

/** Minimal required OpenSSL version. */
static const char openSSLMinimal[] = "1.0.1-g";

std::string boostVersion(VersionNumber boostVersion = BOOST_VERSION)
{
    std::stringstream ss;
    ss << (boostVersion / 100000) << "."
       << (boostVersion / 100 % 1000) << "."
       << (boostVersion % 100);
    return ss.str();
}

std::string openSSLVersion(VersionNumber openSSLVersion = OPENSSL_VERSION_NUMBER)
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
    if (!r.parse(required)) {
        throw std::runtime_error("Didn't understand required version of " +
                                 name + ": " + required);
    }
    if (!a.parse(actual)) {
        throw std::runtime_error("Didn't understand actual version of " +
                                 name + ": " + required);
    }

    if (a < r) {
        throw std::runtime_error("Your " + name + " library is out of date.\n" +
                                 "Your version: " + actual + "\n" +
                                 "Required version: " +  "\n");
    }
}

void checkBoost(std::string version = boostVersion())
{
    checkVersion("Boost", boostMinimal, version);
}

void checkOpenSSL(std::string version = openSSLVersion())
{
    checkVersion("OpenSSL", openSSLMinimal, version);
}

void checkLibraryVersions()
{
    checkBoost();
    checkOpenSSL();
}

struct CheckLibraryVersions_test : beast::unit_test::suite
{
    void print_message()
    {
        log << "ssl minimal: " << openSSLMinimal << "\n"
            << "ssl actual:  " << openSSLVersion() << "\n"
            << "boost minimal: " << boostMinimal << "\n"
            << "boost actual:  " << boostVersion() << "\n"
            << std::flush;
    }

    void test_bad_ssl()
    {
        std::string error;
        try {
            checkOpenSSL(openSSLVersion(0x0090819fL));
        } catch (std::runtime_error& e) {
            error = e.what();
        }
        auto expectedError = "Your OpenSSL library is out of date.\n"
          "Your version: 0.9.8-o\n"
          "Required version: ";
        unexpected(error.find(expectedError) != 0, error);
    }

    void test_bad_boost()
    {
        std::string error;
        try {
            checkBoost(boostVersion(105400));
        } catch (std::runtime_error& e) {
            error = e.what();
        }
        auto expectedError = "Your Boost library is out of date.\n"
          "Your version: 1.54.0\n"
          "Required version: ";
        unexpected(error.find(expectedError) != 0, error);
    }


    void run()
    {
        print_message();
        checkLibraryVersions();

        test_bad_ssl();
        test_bad_boost();
    }
};

BEAST_DEFINE_TESTSUITE(CheckLibraryVersions, ripple_basics, ripple);

}  // namespace version
}  // namespace ripple
