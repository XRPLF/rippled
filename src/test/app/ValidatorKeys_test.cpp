#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/unit_test/utils.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/NodeIdentity.h>
#include <xrpld/app/misc/ValidatorKeys.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/Manifest.h>

#include <boost/program_options/variables_map.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::test {

class ValidatorKeys_test : public beast::unit_test::Suite
{
    // Used with [validation_seed]
    std::string const seed_ = "shUwVw52ofnCUX5m7kPTKzJdr4HEH";

    // Used with [validation_token]
    std::string const tokenSecretStr_ = "paQmjZ37pKKPMrgadBLsuf9ab7Y7EUNzh27LQrZqoexpAs31nJi";

    std::vector<std::string> const tokenBlob_ = {
        "    eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT\n",
        " \tQzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFuaWZl     \n",
        "\tc3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE\n",
        "\t hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG\t  \t\n",
        "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2\n",
        "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1\n",
        "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj\n",
        "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n",
    };

    std::string const tokenManifest_ =
        "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/vCxHXXLplc2GnMhAkE1agqXxBwD"
        "wDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQIhAOngu9sAKqXYouJ+l2V0W+sAOkVB"
        "+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/aAZokS1vymGmVrlHPKWX3Yywu6in8HASQKPu"
        "gBD67kMaRFGvmpATHlGKJdvDFlWPYy5AqDedFv5TJa2w0i21eq3MYywLVJZnFOr7C0kw"
        "2AiTzSCjIzditQ8=";

    // Manifest does not match private key
    std::vector<std::string> const invalidTokenBlob_ = {
        "eyJtYW5pZmVzdCI6IkpBQUFBQVZ4SWUyOVVBdzViZFJudHJ1elVkREk4aDNGV1JWZl\n",
        "k3SXVIaUlKQUhJd3MxdzZzM01oQWtsa1VXQWR2RnFRVGRlSEpvS1pNY0hlS0RzOExo\n",
        "b3d3bDlHOEdkVGNJbmFka1l3UkFJZ0h2Q01lQU1aSzlqQnV2aFhlaFRLRzVDQ3BBR1\n",
        "k0bGtvZHRXYW84UGhzR3NDSUREVTA1d1c3bWNiMjlVNkMvTHBpZmgvakZPRGhFR21i\n",
        "NWF6dTJMVHlqL1pjQkpBbitmNGhtQTQ0U0tYbGtTTUFqak1rSWRyR1Rxa21SNjBzVG\n",
        "JaTjZOOUYwdk9UV3VYcUZ6eDFoSGIyL0RqWElVZXhDVGlITEcxTG9UdUp1eXdXbk55\n",
        "RFE9PSIsInZhbGlkYXRpb25fc2VjcmV0X2tleSI6IjkyRDhCNDBGMzYwMTc5MTkwMU\n",
        "MzQTUzMzI3NzBDMkUwMTA4MDI0NTZFOEM2QkI0NEQ0N0FFREQ0NzJGMDQ2RkYifQ==\n",
    };

public:
    void
    run() override
    {
        // We're only using Env for its Journal.  That Journal gives better
        // coverage in unit tests.
        test::jtx::Env env{*this, test::jtx::envconfig(), nullptr, beast::Severity::Disabled};
        beast::Journal const journal{env.app().getJournal("ValidatorKeys_test")};

        // Keys/ID when using [validation_seed]
        SecretKey const seedSecretKey =
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            generateSecretKey(KeyType::Secp256k1, *parseBase58<Seed>(seed_));
        PublicKey const seedPublicKey = derivePublicKey(KeyType::Secp256k1, seedSecretKey);
        NodeID const seedNodeID = calcNodeID(seedPublicKey);

        // Keys when using [validation_token]
        auto const tokenSecretKey =
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *parseBase58<SecretKey>(TokenType::NodePrivate, tokenSecretStr_);

        auto const tokenPublicKey = derivePublicKey(KeyType::Secp256k1, tokenSecretKey);

        auto const m = deserializeManifest(base64Decode(tokenManifest_));
        BEAST_EXPECT(m);

        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        NodeID const tokenNodeID = calcNodeID(m->masterKey);

        {
            // No config -> no key but valid
            Config const c;
            ValidatorKeys const k{c, journal};
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());
        }
        {
            // validation seed section -> empty manifest and valid seeds
            Config c;
            c.section(Sections::kValidationSeed).append(seed_);
            c.section(Sections::kValidatorKeyType).append("secp256k1");

            ValidatorKeys const k{c, journal};
            if (BEAST_EXPECT(k.keys); k.keys.has_value())
            {
                BEAST_EXPECT(k.keys->publicKey == seedPublicKey);
                BEAST_EXPECT(test::equal(k.keys->secretKey, seedSecretKey));
            }
            BEAST_EXPECT(k.nodeID == seedNodeID);
            BEAST_EXPECT(k.manifest.empty());
            BEAST_EXPECT(!k.configInvalid());
        }

        {
            // validation seed bad seed -> invalid
            Config c;
            c.section(Sections::kValidationSeed).append("badseed");
            c.section(Sections::kValidatorKeyType).append("secp256k1");

            ValidatorKeys const k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // validator token
            Config c;
            c.section(Sections::kValidatorToken).append(tokenBlob_);
            c.section(Sections::kValidatorKeyType).append("secp256k1");
            ValidatorKeys k{c, journal};

            if (BEAST_EXPECT(k.keys); k.keys.has_value())
            {
                BEAST_EXPECT(k.keys->publicKey == tokenPublicKey);
                BEAST_EXPECT(test::equal(k.keys->secretKey, tokenSecretKey));
            }
            BEAST_EXPECT(k.nodeID == tokenNodeID);
            BEAST_EXPECT(k.manifest == tokenManifest_);
            BEAST_EXPECT(!k.configInvalid());
        }
        {
            // invalid validator token
            Config c;
            c.section(Sections::kValidatorToken).append("badtoken");
            c.section(Sections::kValidatorKeyType).append("secp256k1");
            ValidatorKeys const k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // Cannot specify both
            Config c;
            c.section(Sections::kValidationSeed).append(seed_);
            c.section(Sections::kValidatorToken).append(tokenBlob_);
            c.section(Sections::kValidatorKeyType).append("secp256k1");
            ValidatorKeys const k{c, journal};

            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }

        {
            // Token manifest and private key must match
            Config c;
            c.section(Sections::kValidatorToken).append(invalidTokenBlob_);
            c.section(Sections::kValidatorKeyType).append("secp256k1");
            ValidatorKeys const k{c, journal};

            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
            BEAST_EXPECT(k.manifest.empty());
        }

        testValidatorKeyType(journal);
        testNodeIdentityKeyType(env.app());
    }

private:
    void
    testValidatorKeyType(beast::Journal const& journal)
    {
        testcase("validator_key_type");

        auto const keyTypeOf = [&](std::optional<std::string> const& value) {
            Config c;
            c.section(Sections::kValidationSeed).append(seed_);
            if (value)
                c.section(Sections::kValidatorKeyType);
            if (value && !value->empty())
                c.section(Sections::kValidatorKeyType).append(*value);
            ValidatorKeys const k{c, journal};
            return k.keys ? publicKeyType(k.keys->publicKey) : std::nullopt;
        };
        BEAST_EXPECT(keyTypeOf(std::nullopt) == KeyType::Secp256k1);
        BEAST_EXPECT(keyTypeOf(std::string("secp256k1")) == KeyType::Secp256k1);
        BEAST_EXPECT(keyTypeOf(std::string("dilithium")) == KeyType::Dilithium);

        for (std::string const value : {"ed25519", "bogus", ""})
        {
            Config c;
            c.section(Sections::kValidationSeed).append(seed_);
            c.section(Sections::kValidatorKeyType);
            if (!value.empty())
                c.section(Sections::kValidatorKeyType).append(value);
            ValidatorKeys const k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
        }

        {
            // a 32-byte token secret is not a Dilithium key
            Config c;
            c.section(Sections::kValidatorToken).append(tokenBlob_);
            c.section(Sections::kValidatorKeyType).append("dilithium");
            ValidatorKeys const k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
        }

        {
            // a token whose manifest does not deserialize
            auto const secret = parseBase58<SecretKey>(TokenType::NodePrivate, tokenSecretStr_);
            BEAST_EXPECT(secret);
            if (!secret)
                return;
            json::Value token;
            token["manifest"] = base64Encode("not a manifest");
            token["validation_secret_key"] = strHex(*secret);
            Config c;
            c.section(Sections::kValidatorToken).append(base64Encode(to_string(token)));
            ValidatorKeys const k{c, journal};
            BEAST_EXPECT(k.configInvalid());
            BEAST_EXPECT(!k.keys);
        }
    }

    void
    testNodeIdentityKeyType(Application& app)
    {
        testcase("node identity key type");

        auto const seed = generateSeed("node identity");
        boost::program_options::variables_map const cmdline;
        auto& config = app.config();
        config.legacy(Sections::kNodeSeed, toBase58(seed));

        for (auto const type : {KeyType::Secp256k1, KeyType::Dilithium})
        {
            config.legacy(Sections::kValidatorKeyType, to_string(type));
            auto const [pk, sk] = getNodeIdentity(app, cmdline);
            BEAST_EXPECT(pk == derivePublicKey(type, generateSecretKey(type, seed)));
            BEAST_EXPECT(pk == derivePublicKey(type, sk));
        }

        config.legacy(Sections::kValidatorKeyType, "ed25519");
        std::string error;
        try
        {
            [[maybe_unused]] auto const identity = getNodeIdentity(app, cmdline);
        }
        catch (std::runtime_error const& e)
        {
            error = e.what();
        }
        BEAST_EXPECT(
            error ==
            "Invalid key type specified in [validator_key_type]: it must be secp256k1 "
            "or dilithium");
    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(ValidatorKeys, app, xrpl);

}  // namespace xrpl::test
