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
public:
    void
    run() override
    {
        beast::Journal j;

        auto testMakeValidatorKeys = [&](std::vector<
            std::pair<std::string, std::vector<std::string>>> const& sections,
            bool expectValid = true)
        {
            Config c;
            for (auto const& s : sections)
                c.section(s.first).append(s.second);
            ValidatorKeys k{c, j};
            BEAST_EXPECT(expectValid ^ k.configInvalid());
            if (! expectValid)
            {
                BEAST_EXPECT(k.publicKey.size() == 0);
                BEAST_EXPECT(k.manifest.empty());
            }
            return k;
        };

        // No config -> no key but valid
        testMakeValidatorKeys({});

        {
            // invalid validation seed
            std::string const seed = "badseed";
            testMakeValidatorKeys(
                {{SECTION_VALIDATION_SEED, {seed}}},
                false /* expectValid */);
        }

        {
            // invalid validator token
            std::string const tokenBlob = "badtoken";
            testMakeValidatorKeys(
                {{SECTION_VALIDATOR_TOKEN, {tokenBlob}}},
                false /* expectValid */);
        }

        {
            // Invalid token manifest
            const std::vector<std::string> invalidTokenBlob = {
                "eyJtYW5pZmVzdCI6IlltRmtiV0Z1YVdabGMzUT0iLCJ2YWxpZGF0aW9uX3N\n",
                "lY3JldF9rZXkiOiI5MkQ4QjQwRjM2MDE3OTE5MDFDM0E1MzMyNzcwQzJFMD\n",
                "EwODAyNDU2RThDNkJCNDRENDdBRURENDcyRjA0NkZGIn0="};

            testMakeValidatorKeys(
                {{SECTION_VALIDATOR_TOKEN, {invalidTokenBlob}}},
                false /* expectValid */);
        }

        {
            // Manifest does not match private key
            const std::vector<std::string> invalidTokenBlob = {
                "eyJtYW5pZmVzdCI6IkpBQUFBQVZ4SWUyOVVBdzViZFJudHJ1elVkREk4aDN\n",
                "GV1JWZlk3SXVIaUlKQUhJd3MxdzZzM01oQWtsa1VXQWR2RnFRVGRlSEpvS1\n",
                "pNY0hlS0RzOExob3d3bDlHOEdkVGNJbmFka1l3UkFJZ0h2Q01lQU1aSzlqQ\n",
                "nV2aFhlaFRLRzVDQ3BBR1k0bGtvZHRXYW84UGhzR3NDSUREVTA1d1c3bWNi\n",
                "MjlVNkMvTHBpZmgvakZPRGhFR21iNWF6dTJMVHlqL1pjQkpBbitmNGhtQTQ\n",
                "0U0tYbGtTTUFqak1rSWRyR1Rxa21SNjBzVGJaTjZOOUYwdk9UV3VYcUZ6eD\n",
                "FoSGIyL0RqWElVZXhDVGlITEcxTG9UdUp1eXdXbk55RFE9PSIsInZhbGlkY\n",
                "XRpb25fc2VjcmV0X2tleSI6IjkyRDhCNDBGMzYwMTc5MTkwMUMzQTUzMzI3\n",
                "NzBDMkUwMTA4MDI0NTZFOEM2QkI0NEQ0N0FFREQ0NzJGMDQ2RkYifQ=="};

            // Token manifest and private key must match
            testMakeValidatorKeys(
                {{SECTION_VALIDATOR_TOKEN, {invalidTokenBlob}}},
                false /* expectValid */);
        }

        const std::string seed = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

        {
            // validation seed section -> empty manifest and valid seeds
            SecretKey const seedSecretKey =
                generateSecretKey(KeyType::secp256k1, *parseBase58<Seed>(seed));
            PublicKey const seedPublicKey =
                derivePublicKey(KeyType::secp256k1, seedSecretKey);
            NodeID const seedNodeID = calcNodeID(seedPublicKey);

            auto const k = testMakeValidatorKeys(
                {{SECTION_VALIDATION_SEED, {seed}}});

            BEAST_EXPECT(k.publicKey == seedPublicKey);
            BEAST_EXPECT(k.secretKey == seedSecretKey);
            BEAST_EXPECT(k.nodeID == seedNodeID);
            BEAST_EXPECT(k.manifest.empty());
        }

        const std::string tokenSecretStr =
            "paQmjZ37pKKPMrgadBLsuf9ab7Y7EUNzh27LQrZqoexpAs31nJi";
        auto const tokenSecretKey = *parseBase58<SecretKey>(
            TokenType::NodePrivate, tokenSecretStr);

        auto testValidatorToken = [&](
            KeyType const& keyType,
            std::string const& tokenManifest,
            std::vector<std::string> const& tokenBlob)
        {
            auto const tokenPublicKey =
                derivePublicKey(keyType, tokenSecretKey);

            auto const m = Manifest::make_Manifest(
                base64_decode(tokenManifest));
            BEAST_EXPECT(m);
            NodeID const tokenNodeID = calcNodeID(m->masterKey);

            auto const k = testMakeValidatorKeys(
                {{SECTION_VALIDATOR_TOKEN, tokenBlob}});

            BEAST_EXPECT(k.publicKey == tokenPublicKey);
            BEAST_EXPECT(k.secretKey == tokenSecretKey);
            BEAST_EXPECT(k.nodeID == tokenNodeID);
            BEAST_EXPECT(k.manifest == tokenManifest);
        };

        {
            // validator token with secp256k1 key
            const std::vector<std::string> tokenBlob = {
                "    "
                "eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI\n",
                " \t3NDdiNTQzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlk "
                "    \n",
                "\tYWY2IiwibWFuaWZlc3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUp"
                "\n",
                "\t "
                "xQzlnVkZLaWxHZncxL3ZDeEhYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdE\t  "
                "\t\n",
                "YklENk9NU1l1TTBGREFscEFnTms4U0tGbjdNTzJmZGtjd1JRSWhBT25ndTl\n",
                "zQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2hsSkFmVXNYZkFpQnNWSk\n",
                "dlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1NmluOEhBU1FLU\n",
                "HVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdjVUSmEy\n",
                "dzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n"
            };

            const std::string tokenManifest =
                "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/vCxHXXLplc2GnMhAkE1ag"
                "qXxBwDwDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQIhAOngu9sAKqXYouJ+"
                "l2V0W+sAOkVB+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/aAZokS1vymGmVrlHPK"
                "WX3Yywu6in8HASQKPugBD67kMaRFGvmpATHlGKJdvDFlWPYy5AqDedFv5TJa2w"
                "0i21eq3MYywLVJZnFOr7C0kw2AiTzSCjIzditQ8=";

            testValidatorToken(KeyType::secp256k1, tokenManifest, tokenBlob);

            // Cannot specify both seed and token
            testMakeValidatorKeys({
                {SECTION_VALIDATION_SEED, {seed}},
                {SECTION_VALIDATOR_TOKEN, {tokenBlob}}},
                false /* expectValid */);
        }

        {
            // validator token with ed25519 key
            const std::vector<std::string> tokenBlob = {
                "eyJtYW5pZmVzdCI6IkpBQUFBQUp4SWUzbWVnMnh1aTMxWWhyTDMvOHpCOHE\n",
                "3SWduaXJ2d2xQTTlqeDBoZy8zODV1bk1oN2ZYUUdaU0l6SjlLMGJFaFNlMj\n",
                "B4RjRrdTcvVFlFVXJTbkFlQnRpZitjVUhka0FXZFlJQU5xMTN4WUZ1blEvV\n",
                "ElsSFhPeVRSZTgxc2gvbWVQV3pyU2djeFFMMlhIS0VuKzFIdkJWcnRXZXZ1\n",
                "ay9hUUNqL0pMMnF0Z05KdFlwVkxIUndHY0JKQUM1aWpxMkJVREdidUxvRGZ\n",
                "xZmRCUVpsMERQdTgzcElzNDlsUXNHKzI5eXZsZmxBcDhCVjd3UE9HK0hYMk\n",
                "F6d2gzd3FzbXRodURlTENNM25WS1hOZkN3PT0iLCJ2YWxpZGF0aW9uX3NlY\n",
                "3JldF9rZXkiOiI5RUQ0NUY4NjYyNDFDQzE4QTI3NDdCNTQzODdDMDYyNTkw\n",
                "Nzk3MkY0RTcxOTAyMzFGQUE5Mzc0NTdGQTlEQUY2In0=\n"};

            const std::string tokenManifest =
                "JAAAAAJxIe3meg2xui31YhrL3/8zB8q7IgnirvwlPM9jx0hg/385unMh7fX"
                "QGZSIzJ9K0bEhSe20xF4ku7/TYEUrSnAeBtif+cUHdkAWdYIANq13xYFunQ"
                "/TIlHXOyTRe81sh/mePWzrSgcxQL2XHKEn+1HvBVrtWevuk/aQCj/JL2qtg"
                "NJtYpVLHRwGcBJAC5ijq2BUDGbuLoDfqfdBQZl0DPu83pIs49lQsG+29yvl"
                "flAp8BV7wPOG+HX2Azwh3wqsmthuDeLCM3nVKXNfCw==";

            testValidatorToken(KeyType::ed25519, tokenManifest, tokenBlob);
        }
    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(ValidatorKeys, app, ripple);

}  // namespace test
}  // namespace ripple
