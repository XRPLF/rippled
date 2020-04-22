//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Dev Null Productions, LLC

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

#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <string>

namespace ripple {
namespace test {

class ManifestRPC_test : public beast::unit_test::suite
{
public:
    void
    testErrors()
    {
        testcase("Errors");

        using namespace jtx;
        Env env(*this);
        {
            // manifest with no public key
            auto const info = env.rpc("json", "manifest", "{ }");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] ==
                "Missing field 'public_key'.");
        }
        {
            // manifest with manlformed public key
            auto const info = env.rpc(
                "json",
                "manifest",
                "{ \"public_key\": "
                "\"abcdef12345\"}");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "Invalid parameters.");
        }
    }

    void
    testLookup()
    {
        testcase("Lookup");

        using namespace jtx;
        std::string const key =
            "n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7";
        Env env{*this, envconfig([&key](std::unique_ptr<Config> cfg) {
                    cfg->section(SECTION_VALIDATORS).append(key);
                    return cfg;
                })};
        {
            auto const info = env.rpc(
                "json",
                "manifest",
                "{ \"public_key\": "
                "\"" +
                    key + "\"}");
            BEAST_EXPECT(info[jss::result][jss::requested] == key);
            BEAST_EXPECT(info[jss::result][jss::status] == "success");
        }
    }

    void
    run() override
    {
        testErrors();
        testLookup();
    }
};

BEAST_DEFINE_TESTSUITE(ManifestRPC, rpc, ripple);
}  // namespace test
}  // namespace ripple
