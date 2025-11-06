#include <test/jtx/Env.h>

#include <xrpld/app/misc/Manifest.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/basics/base64.h>
#include <xrpl/beast/unit_test.h>

#include <string>

namespace ripple {
namespace test {

class ValidatorKeys_test : public beast::unit_test::suite
{
    // Used with [validation_seed]
    std::string const seed = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

    // Used with [validation_token]
    std::string const tokenSecretStr =
        "paQmjZ37pKKPMrgadBLsuf9ab7Y7EUNzh27LQrZqoexpAs31nJi";

    std::vector<std::string> const tokenBlob = {
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

    std::string const tokenManifest =
        "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/vCxHXXLplc2GnMhAkE1agqXxBwD"
        "wDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQIhAOngu9sAKqXYouJ+l2V0W+sAOkVB"
        "+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/aAZokS1vymGmVrlHPKWX3Yywu6in8HASQKPu"
        "gBD67kMaRFGvmpATHlGKJdvDFlWPYy5AqDedFv5TJa2w0i21eq3MYywLVJZnFOr7C0kw"
        "2AiTzSCjIzditQ8=";

    // Manifest does not match private key
    std::vector<std::string> const invalidTokenBlob = {
        "eyJtYW5pZmVzdCI6IkpBQUFBQVZ4SWUyOVVBdzViZFJudHJ1elVkREk4aDNGV1JWZl\n",
        "k3SXVIaUlKQUhJd3MxdzZzM01oQWtsa1VXQWR2RnFRVGRlSEpvS1pNY0hlS0RzOExo\n",
        "b3d3bDlHOEdkVGNJbmFka1l3UkFJZ0h2Q01lQU1aSzlqQnV2aFhlaFRLRzVDQ3BBR1\n",
        "k0bGtvZHRXYW84UGhzR3NDSUREVTA1d1c3bWNiMjlVNkMvTHBpZmgvakZPRGhFR21i\n",
        "NWF6dTJMVHlqL1pjQkpBbitmNGhtQTQ0U0tYbGtTTUFqak1rSWRyR1Rxa21SNjBzVG\n",
        "JaTjZOOUYwdk9UV3VYcUZ6eDFoSGIyL0RqWElVZXhDVGlITEcxTG9UdUp1eXdXbk55\n",
        "RFE9PSIsInZhbGlkYXRpb25fc2VjcmV0X2tleSI6IjkyRDhCNDBGMzYwMTc5MTkwMU\n",
        "MzQTUzMzI3NzBDMkUwMTA4MDI0NTZFOEM2QkI0NEQ0N0FFREQ0NzJGMDQ2RkYifQ==\n"};

public:
    void
    run() override
    {
        // We're only using Env for its Journal.  That Journal gives better
        // coverage in unit tests.
        test::jtx::Env env{
            *this,
            test::jtx::envconfig(),
            nullptr,
            beast::severities::kDisabled};
        beast::Journal journal{env.app().journal("ValidatorKeys_test")};

        // Keys/ID when using [validation_seed]
        SecretKey const seedSecretKey =
            generateSecretKey(KeyType::secp256k1, *parseBase58<Seed>(seed));
        PublicKey const seedPublicKey =
            derivePublicKey(KeyType::secp256k1, seedSecretKey);
        NodeID const seedNodeID = calcNodeID(seedPublicKey);

        // Keys when using [validation_token]
        auto const tokenSecretKey =
            *parseBase58<SecretKey>(TokenType::NodePrivate, tokenSecretStr);

        auto const tokenPublicKey =
            derivePublicKey(KeyType::secp256k1, tokenSecretKey);

        auto const m = deserializeManifest(base64_decode(tokenManifest));
        BEAST_EXPECT(m);
        NodeID const tokenNodeID = calcNodeID(m->masterKey);

        {
            // No config -> no key but valid
            Config c;
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());
        }
        {
            // validation seed section -> empty manifest and valid seeds
            Config c;
            c.section(SECTION_VALIDATION_SEED).append(seed);

            ValidatorKeys k{c, journal};
            if (BEAST_EXPECT(k.keys))
            {
                BEAST_EXPECT(k.keys->publicKey == seedPublicKey);
                BEAST_EXPECT(k.keys->secretKey == seedSecretKey);
            }
            BEAST_EXPECT(k.nodeID == seedNodeID);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());
        }

        {
            // validation seed bad seed -> invalid
            Config c;
            c.section(SECTION_VALIDATION_SEED).append("badseed");

            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // validator token
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
            ValidatorKeys k{c, journal};

            if (BEAST_EXPECT(k.keys))
            {
                BEAST_EXPECT(k.keys->publicKey == tokenPublicKey);
                BEAST_EXPECT(k.keys->secretKey == tokenSecretKey);
            }
            BEAST_EXPECT(k.nodeID == tokenNodeID);
            BEAST_EXPECT(k.manifest == tokenManifest);
            BEAST_EXPECT(!k.configInvalid());
        }
        {
            // invalid validator token
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append("badtoken");
            ValidatorKeys k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // Cannot specify both
            Config c;
            c.section(SECTION_VALIDATION_SEED).append(seed);
            c.section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
            ValidatorKeys k{c, journal};

            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // Token manifest and private key must match
            Config c;
            c.section(SECTION_VALIDATOR_TOKEN).append(invalidTokenBlob);
            ValidatorKeys k{c, journal};

            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }
    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(ValidatorKeys, app, ripple);

}  // namespace test
}  // namespace ripple
