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
#include <ripple/test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

namespace test {

namespace validator {
static auto const seed = "ss7t3J9dYentEFgKdPA3q6eyxtrLB";
static auto const master_key =
    "nHU4LxxrSQsRTKy5uZbX95eYowoamUEPCcWraxoiCNbtDaUr1V34";
static auto const signing_key =
    "n9LHPLA36SBky1YjbaVEApQQ3s9XcpazCgfAG7jsqBb1ugDAosbm";
// Format manifest string to test trim()
static auto const manifest =
    "    JAAAAAFxIe2FwblmJwz4pVYXHLJSzSBgIK7mpQuHNQ88CxW\n"
    " \tjIN7q4nMhAuUTyasIhvj2KPfNRbmmIBnqNUzidgkKb244eP     \n"
    "\t794ZpMdkC+8l5n3R/CHP6SAwhYDOaqub0Cs2NjjewBnp1mf\n"
    "\t 23rhAzdcjRuWzm0IT12eduZ0DwcF5Ng8rAelaYP1iT93ScE\t  \t";
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
            expect (!result[jss::result].isMember (jss::error));
            expect (result[jss::status] == "success");
            expect (result[jss::result].isMember(jss::info));
        }
        {
            Env env(*this, makeValidatorConfig());
            auto const result = env.rpc("server_info", "1");
            expect (!result[jss::result].isMember (jss::error));
            expect (result[jss::status] == "success");
            expect (result[jss::result].isMember(jss::info));
            expect(result[jss::result][jss::info]
                [jss::pubkey_validator] == validator::signing_key);
            expect (result[jss::result][jss::info].isMember(
                jss::validation_manifest));
            expect (result[jss::result][jss::info][jss::validation_manifest]
                [jss::master_key] == validator::master_key);
            expect (result[jss::result][jss::info][jss::validation_manifest]
                [jss::signing_key] == validator::signing_key);
            expect (result[jss::result][jss::info][jss::validation_manifest]
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

