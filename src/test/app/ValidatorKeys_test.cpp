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

public:
    void
    run() override
    {
        beast::Journal j;

        // Keys when using [validation_seed]
        auto const seedSecretKey =
            generateSecretKey(KeyType::secp256k1, *parseBase58<Seed>(seed));
        auto const seedPublicKey =
            derivePublicKey(KeyType::secp256k1, seedSecretKey);

        // Keys when using [validation_token]
        auto const tokenSecretKey = *parseBase58<SecretKey>(
            TokenType::TOKEN_NODE_PRIVATE, tokenSecretStr);

        auto const tokenPublicKey =
            derivePublicKey(KeyType::secp256k1, tokenSecretKey);

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
            Config c;
            c.section(SECTION_VALIDATION_SEED).append(seed);

            ValidatorKeys k{c, j};
            BEAST_EXPECT(k.publicKey == seedPublicKey);
            BEAST_EXPECT(k.secretKey == seedSecretKey);
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

        {
            // validator token
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
            ValidatorKeys k{c, j};

            BEAST_EXPECT(k.publicKey == tokenPublicKey);
            BEAST_EXPECT(k.secretKey == tokenSecretKey);
            BEAST_EXPECT(k.manifest == tokenManifest);
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

    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(ValidatorKeys, app, ripple);

}  // namespace test
}  // namespace ripple
