//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2017 Ripple Labs Inc.

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

#include <ripple/app/misc/ValidatorKeys.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/base64.h>
#include <ripple/beast/unit_test.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <string>

namespace ripple {
namespace test {

class ValidatorKeys_test : public beast::unit_test::suite
{
    // Used with [validation_seed]
    const std::string seed = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

    // Used with [validation_token]
    const std::string tokenSecretStr =
        "paQmjZ37pKKPMrgadBLsuf9ab7Y7EUNzh27LQrZqoexpAs31nJi";

    const std::vector<std::string> tokenBlob = {
        "    "
        "eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT\n",
        " \tQzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFuaWZl "
        "    \n",
        "\tc3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE"
        "\n",
        "\t "
        "hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG\t  "
        "\t\n",
        "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2\n",
        "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1\n",
        "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj\n",
        "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n"};

    const std::string tokenManifest =
        "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/vCxHXXLplc2GnMhAkE1agqXxBwD"
        "wDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQIhAOngu9sAKqXYouJ+l2V0W+sAOkVB"
        "+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/aAZokS1vymGmVrlHPKWX3Yywu6in8HASQKPu"
        "gBD67kMaRFGvmpATHlGKJdvDFlWPYy5AqDedFv5TJa2w0i21eq3MYywLVJZnFOr7C0kw"
        "2AiTzSCjIzditQ8=";

    // Manifest does not match private key
    const std::vector<std::string> invalidTokenBlob = {
        "eyJtYW5pZmVzdCI6IkpBQUFBQVZ4SWUyOVVBdzViZFJudHJ1elVkREk4aDNGV1JWZl\n",
        "k3SXVIaUlKQUhJd3MxdzZzM01oQWtsa1VXQWR2RnFRVGRlSEpvS1pNY0hlS0RzOExo\n",
        "b3d3bDlHOEdkVGNJbmFka1l3UkFJZ0h2Q01lQU1aSzlqQnV2aFhlaFRLRzVDQ3BBR1\n",
        "k0bGtvZHRXYW84UGhzR3NDSUREVTA1d1c3bWNiMjlVNkMvTHBpZmgvakZPRGhFR21i\n",
        "NWF6dTJMVHlqL1pjQkpBbitmNGhtQTQ0U0tYbGtTTUFqak1rSWRyR1Rxa21SNjBzVG\n",
        "JaTjZOOUYwdk9UV3VYcUZ6eDFoSGIyL0RqWElVZXhDVGlITEcxTG9UdUp1eXdXbk55\n",
        "RFE9PSIsInZhbGlkYXRpb25fc2VjcmV0X2tleSI6IjkyRDhCNDBGMzYwMTc5MTkwMU\n",
        "MzQTUzMzI3NzBDMkUwMTA4MDI0NTZFOEM2QkI0NEQ0N0FFREQ0NzJGMDQ2RkYifQ==\n"};

    const std::vector<std::string> tokenBlobEd25519 = {
        "eyJtYW5pZmVzdCI6IkpBQUFBQUp4SWUzbWVnMnh1aTMxWWhyTDMvOHpCOHE3SWduaXJ2d2xQ\n",
        "TTlqeDBoZy8zODV1bk1oN2ZYUUdaU0l6SjlLMGJFaFNlMjB4RjRrdTcvVFlFVXJTbkFlQnRp\n",
        "ZitjVUhka0FXZFlJQU5xMTN4WUZ1blEvVElsSFhPeVRSZTgxc2gvbWVQV3pyU2djeFFMMlhI\n",
        "S0VuKzFIdkJWcnRXZXZ1ay9hUUNqL0pMMnF0Z05KdFlwVkxIUndHY0JKQUM1aWpxMkJVREdi\n",
        "dUxvRGZxZmRCUVpsMERQdTgzcElzNDlsUXNHKzI5eXZsZmxBcDhCVjd3UE9HK0hYMkF6d2gz\n",
        "d3FzbXRodURlTENNM25WS1hOZkN3PT0iLCJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5RUQ0\n",
        "NUY4NjYyNDFDQzE4QTI3NDdCNTQzODdDMDYyNTkwNzk3MkY0RTcxOTAyMzFGQUE5Mzc0NTdG\n",
        "QTlEQUY2In0=\n"};

    const std::string tokenManifestEd25519 =
        "JAAAAAJxIe3meg2xui31YhrL3/8zB8q7IgnirvwlPM9jx0hg/385unMh7fXQGZSIzJ9K"
        "0bEhSe20xF4ku7/TYEUrSnAeBtif+cUHdkAWdYIANq13xYFunQ/TIlHXOyTRe81sh/me"
        "PWzrSgcxQL2XHKEn+1HvBVrtWevuk/aQCj/JL2qtgNJtYpVLHRwGcBJAC5ijq2BUDGbu"
        "LoDfqfdBQZl0DPu83pIs49lQsG+29yvlflAp8BV7wPOG+HX2Azwh3wqsmthuDeLCM3nV"
        "KXNfCw==";

public:
    void
    run() override
    {
        beast::Journal j;

        {
            // No config -> no key but valid
            Config c;
            ValidatorKeys k{c, j};
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());

        }
        {
            // validation seed section -> empty manifest and valid seeds
            SecretKey const seedSecretKey =
                generateSecretKey(KeyType::secp256k1, *parseBase58<Seed>(seed));
            PublicKey const seedPublicKey =
                derivePublicKey(KeyType::secp256k1, seedSecretKey);
            NodeID const seedNodeID = calcNodeID(seedPublicKey);

            Config c;
            c.section(SECTION_VALIDATION_SEED).append(seed);

            ValidatorKeys k{c, j};
            BEAST_EXPECT(k.publicKey == seedPublicKey);
            BEAST_EXPECT(k.secretKey == seedSecretKey);
            BEAST_EXPECT(k.nodeID == seedNodeID);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());
        }

        {
            // validation seed bad seed -> invalid
            Config c;
            c.section(SECTION_VALIDATION_SEED).append("badseed");

            ValidatorKeys k{c, j};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }

        auto const tokenSecretKey = *parseBase58<SecretKey>(
            TokenType::NodePrivate, tokenSecretStr);

        {
            // validator token
            auto const tokenPublicKey =
                derivePublicKey(KeyType::secp256k1, tokenSecretKey);

            auto const m = Manifest::make_Manifest(
                boost::beast::detail::base64_decode(tokenManifest));
            BEAST_EXPECT(m);
            NodeID const tokenNodeID = calcNodeID(m->masterKey);

            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
            ValidatorKeys k{c, j};

            BEAST_EXPECT(k.publicKey == tokenPublicKey);
            BEAST_EXPECT(k.secretKey == tokenSecretKey);
            BEAST_EXPECT(k.nodeID == tokenNodeID);
            BEAST_EXPECT(k.manifest == tokenManifest);
            BEAST_EXPECT(!k.configInvalid());
        }

        {
            // validator token with ed25519 key
            auto const tokenPublicKey =
                derivePublicKey(KeyType::ed25519, tokenSecretKey);
            auto const m = Manifest::make_Manifest(
                boost::beast::detail::base64_decode(tokenManifestEd25519));
            BEAST_EXPECT(m);
            NodeID const tokenNodeID = calcNodeID(m->masterKey);

            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlobEd25519);
            ValidatorKeys k{c, j};

            BEAST_EXPECT(k.publicKey == tokenPublicKey);
            BEAST_EXPECT(k.secretKey == tokenSecretKey);
            BEAST_EXPECT(k.nodeID == tokenNodeID);
            BEAST_EXPECT(k.manifest == tokenManifestEd25519);
            BEAST_EXPECT(!k.configInvalid());
        }

        {
            // invalid validator token
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append("badtoken");
            ValidatorKeys k{c, j};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // Cannot specify both
            Config c;
            c.section(SECTION_VALIDATION_SEED).append(seed);
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
            ValidatorKeys k{c, j};

            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // Token manifest and private key must match
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(invalidTokenBlob);
            ValidatorKeys k{c, j};

            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(k.publicKey.size() == 0);
            BEAST_EXPECT(k.manifest.empty());
        }

    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(ValidatorKeys, app, ripple);

}  // namespace test
}  // namespace ripple
