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
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/ConfigSections.h>

namespace ripple {

namespace test {

namespace validator {
static auto const seed = "ss7t3J9dYentEFgKdPA3q6eyxtrLB";
static auto const master_key =
    "nHUYwQk8AyQ8pW9p4SvrWC2hosvaoii9X54uGLDYGBtEFwWFHsJK";
static auto const signing_key =
    "n9LHPLA36SBky1YjbaVEApQQ3s9XcpazCgfAG7jsqBb1ugDAosbm";
static auto const manifest =
    "JAAAAAFxIe2cDLvm5IqpeGFlMTD98HCqv7+GE54anRD/zbvGNYtOsXMhAuUTyasIhvj2KPfN"
    "RbmmIBnqNUzidgkKb244eP794ZpMdkYwRAIgNVq8SYP7js0C/GAGMKVYXiCGUTIL7OKPSBLS"
    "7LTyrL4CIE+s4Tsn/FrrYj0nMEV1Mvf7PMRYCxtEERD3PG/etTJ3cBJAbwWWofHqg9IACoYV"
    "+n9ulZHSVRajo55EkZYw0XUXDw8zcI4gD58suOSLZTG/dXtZp17huIyHgxHbR2YeYjQpCw==";
static auto sequence = 1;
}

class ServerInfo_test : public beast::unit_test::suite
{
public:
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
            // validator configuration using a seed (a different seed than
            // the one used in jtx::validatorConf)
            Env env { *this, std::make_unique<Config>([&](Config* cf)
                {
                    auto const seed = parseBase58<Seed>(validator::seed);
                    if (!seed)
                        Throw<std::runtime_error> ("Invalid seed specified");
                    cf->VALIDATION_PRIV =
                        generateSecretKey (KeyType::secp256k1, *seed);
                    cf->VALIDATION_PUB =
                        derivePublicKey (KeyType::secp256k1, cf->VALIDATION_PRIV);
                    cf->legacy(SECTION_VALIDATION_MANIFEST, validator::manifest);
                }, defaultConf) };

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

