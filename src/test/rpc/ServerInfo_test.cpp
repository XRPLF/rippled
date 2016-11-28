//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <ripple/protocol/JsonFields.h>
#include <test/support/jtx.h>
#include <ripple/beast/unit_test.h>

#include <boost/format.hpp>

namespace ripple {

namespace test {

namespace validator {
static auto const seed = "ss7t3J9dYentEFgKdPA3q6eyxtrLB";
static auto const master_key =
    "nHUYwQk8AyQ8pW9p4SvrWC2hosvaoii9X54uGLDYGBtEFwWFHsJK";
static auto const signing_key =
    "n9LHPLA36SBky1YjbaVEApQQ3s9XcpazCgfAG7jsqBb1ugDAosbm";
// Format manifest string to test trim()
static auto const manifest =
    "    JAAAAAFxIe2cDLvm5IqpeGFlMTD98HCqv7+GE54anRD/zbvGNYtOsXMhAuUTyasIhvj2KPfN\n"
    " \tRbmmIBnqNUzidgkKb244eP794ZpMdkYwRAIgNVq8SYP7js0C/GAGMKVYXiCGUTIL7OKPSBLS     \n"
    "\t7LTyrL4CIE+s4Tsn/FrrYj0nMEV1Mvf7PMRYCxtEERD3PG/etTJ3cBJAbwWWofHqg9IACoYV\n"
    "\t +n9ulZHSVRajo55EkZYw0XUXDw8zcI4gD58suOSLZTG/dXtZp17huIyHgxHbR2YeYjQpCw==\t  \t";
static auto sequence = 1;
}

class ServerInfo_test : public beast::unit_test::suite
{
public:
    static
    std::unique_ptr<Config>
    makeValidatorConfig()
    {
        auto p = std::make_unique<Config>();
        boost::format toLoad(R"rippleConfig(
[validation_manifest]
%1%

[validation_seed]
%2%
)rippleConfig");

        p->loadFromString (boost::str (
            toLoad % validator::manifest % validator::seed));

        setupConfigForUnitTests(*p);

        return p;
    }

    void testServerInfo()
    {
        using namespace test::jtx;

        {
            Env env(*this);
            auto const result = env.rpc("server_info", "1");
            BEAST_EXPECT(!result[jss::result].isMember (jss::error));
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::info));
        }
        {
            Env env(*this, makeValidatorConfig());
            auto const result = env.rpc("server_info", "1");
            BEAST_EXPECT(!result[jss::result].isMember (jss::error));
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::info));
            BEAST_EXPECT(result[jss::result][jss::info]
                [jss::pubkey_validator] == validator::signing_key);
            BEAST_EXPECT(result[jss::result][jss::info].isMember(
                jss::validation_manifest));
            BEAST_EXPECT(result[jss::result][jss::info][jss::validation_manifest]
                [jss::master_key] == validator::master_key);
            BEAST_EXPECT(result[jss::result][jss::info][jss::validation_manifest]
                [jss::signing_key] == validator::signing_key);
            BEAST_EXPECT(result[jss::result][jss::info][jss::validation_manifest]
                [jss::seq] == validator::sequence);
        }
    }

    void run ()
    {
        testServerInfo ();
    }
};

BEAST_DEFINE_TESTSUITE(ServerInfo,app,ripple);

} // test
} // ripple

