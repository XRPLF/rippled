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
#include <ripple/basics/CheckLibraryVersions.h>
#include <ripple/basics/impl/CheckLibraryVersionsImpl.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace version {

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
