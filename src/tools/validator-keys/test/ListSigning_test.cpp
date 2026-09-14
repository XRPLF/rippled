#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>
#include <tools/validator-keys/test/KeyFileGuard.h>

#include <fstream>

namespace xrpl {

namespace tests {

class ListSigning_test : public beast::unit_test::Suite
{
private:
    // A publisher: master keys and the token carrying its signing key.
    struct Publisher
    {
        SigningKeys keys{KeyType::Ed25519};
        ValidatorToken token;
        Manifest manifest;

        Publisher()
            : token(*keys.createValidatorToken(KeyType::Ed25519))
            , manifest(*deserializeManifest(base64Decode(token.manifest)))
        {
        }
    };

    // The unsigned list text a publisher's `prepare` step writes: one
    // validator per token, each with its manifest.
    static std::string
    unsignedListText(
        std::vector<ValidatorToken> const& validators,
        std::uint32_t sequence,
        std::uint32_t expiration,
        std::optional<std::uint32_t> effective = std::nullopt)
    {
        std::string text = "{\n  \"sequence\": " + std::to_string(sequence);
        if (effective)
            text += ",\n  \"effective\": " + std::to_string(*effective);
        text += ",\n  \"expiration\": " + std::to_string(expiration) + ",\n  \"validators\": [";
        bool first = true;
        for (auto const& v : validators)
        {
            auto const m = deserializeManifest(base64Decode(v.manifest));
            text += first ? "\n" : ",\n";
            first = false;
            text += "    {\"validation_public_key\": \"" + strHex(m->masterKey) +
                "\", \"manifest\": \"" + v.manifest + "\"}";
        }
        text += "\n  ]\n}\n";
        return text;
    }

    static std::vector<ValidatorToken>
    makeValidators(std::size_t count)
    {
        std::vector<ValidatorToken> validators;
        for (std::size_t i = 0; i < count; ++i)
        {
            SigningKeys keys(KeyType::Ed25519);
            validators.push_back(*keys.createValidatorToken(KeyType::Secp256k1));
        }
        return validators;
    }

    static json::Value
    parse(std::string const& text)
    {
        json::Reader reader;
        json::Value jv;
        reader.parse(text, jv);
        return jv;
    }

    void
    testCanonicalJson()
    {
        testcase("Canonical JSON");

        // Whitespace outside strings goes, one space follows each comma and
        // colon, key order and string contents stay.
        BEAST_EXPECT(
            canonicalJson("{ \"b\" :1,\n\t\"a\": [ 1 , 2 ] , \"s\":\"x, y: z\" }") ==
            "{\"b\": 1, \"a\": [1, 2], \"s\": \"x, y: z\"}");
        BEAST_EXPECT(canonicalJson("{\"e\": \"a\\\"b\"}") == "{\"e\": \"a\\\"b\"}");
        BEAST_EXPECT(canonicalJson("{\"sequence\": 1, \"x\": 2}") == "{\"sequence\": 1, \"x\": 2}");

        try
        {
            canonicalJson("[1, 2]");
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() == std::string("Not a JSON object"));
        }
        try
        {
            canonicalJson("{\"a\": ");
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() == std::string("Not a JSON object"));
        }
    }

    void
    testParseUnsignedList()
    {
        testcase("Parse Unsigned List");

        auto const validators = makeValidators(2);

        {
            auto const list = parseUnsignedList(unsignedListText(validators, 5, 1000, 500));
            BEAST_EXPECT(list.sequence == 5);
            BEAST_EXPECT(list.expiration == 1000);
            BEAST_EXPECT(list.effective && *list.effective == 500);
            BEAST_EXPECT(list.validators.size() == 2);
            BEAST_EXPECT(
                list.validators[0] ==
                deserializeManifest(base64Decode(validators[0].manifest))->masterKey);
        }
        {
            auto const list = parseUnsignedList(unsignedListText(validators, 5, 1000));
            BEAST_EXPECT(!list.effective);
            // The canonical text keeps the key order of the input.
            BEAST_EXPECT(list.canonical.starts_with("{\"sequence\": 5, \"expiration\": 1000, "));
        }

        auto expectError = [this](std::string const& text, std::string const& expected) {
            try
            {
                parseUnsignedList(text);
                fail(expected);
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECTS(e.what() == expected, e.what());
            }
        };

        expectError(
            "{\"expiration\": 1, \"validators\": []}", "\"sequence\" must be a positive integer");
        expectError(
            "{\"sequence\": 0, \"expiration\": 1, \"validators\": []}",
            "\"sequence\" must be a positive integer");
        expectError(
            "{\"sequence\": 1, \"validators\": []}", "\"expiration\" must be an unsigned integer");
        expectError(
            "{\"sequence\": 1, \"effective\": 1000, \"expiration\": 1000, \"validators\": []}",
            "\"effective\" must be earlier than \"expiration\"");
        expectError(
            "{\"sequence\": 1, \"effective\": \"x\", \"expiration\": 1000, \"validators\": []}",
            "\"effective\" must be an unsigned integer");
        expectError(
            "{\"sequence\": 1, \"expiration\": 1000, \"validators\": []}",
            "\"validators\" must be a non-empty array");
        expectError(
            "{\"sequence\": 1, \"expiration\": 1000, \"validators\": [{\"validation_public_key\": "
            "\"zz\"}]}",
            "\"validation_public_key\" is not a hex public key: zz");
        expectError(
            "{\"sequence\": 1, \"expiration\": 1000, \"validators\": [{}]}",
            "every validator needs a \"validation_public_key\" string");
        expectError(
            "{\"sequence\": 1, \"expiration\": 1000, \"validators\": [{\"validation_public_key\": "
            "\"ED00\"}]}",
            "\"validation_public_key\" is not a hex public key: ED00");
        {
            // A manifest of another key.
            auto const other = deserializeManifest(base64Decode(validators[1].manifest));
            auto const text =
                "{\"sequence\": 1, \"expiration\": 1000, \"validators\": "
                "[{\"validation_public_key\": \"" +
                strHex(other->masterKey) + "\", \"manifest\": \"" + validators[0].manifest +
                "\"}]}";
            expectError(
                text, "\"manifest\" belongs to another key than " + strHex(other->masterKey));
        }
        {
            auto const key = deserializeManifest(base64Decode(validators[0].manifest))->masterKey;
            auto const text =
                "{\"sequence\": 1, \"expiration\": 1000, \"validators\": "
                "[{\"validation_public_key\": \"" +
                strHex(key) + "\", \"manifest\": \"AAAA\"}]}";
            expectError(text, "\"manifest\" does not verify for " + strHex(key));
            auto const notString =
                "{\"sequence\": 1, \"expiration\": 1000, \"validators\": "
                "[{\"validation_public_key\": \"" +
                strHex(key) + "\", \"manifest\": 5}]}";
            expectError(notString, "\"manifest\" must be a base64 string for " + strHex(key));
        }
    }

    void
    testSignAndVerify()
    {
        testcase("Sign and Verify");

        Publisher const publisher;
        auto const validators = makeValidators(3);
        std::uint32_t const now = 1000;
        auto const list = parseUnsignedList(unsignedListText(validators, 7, now + 100));
        auto const signature =
            signList(list, *publisher.manifest.signingKey, publisher.token.validationSecret);

        // Version 1
        auto const v1 = makeSignedList(
            publisher.token.manifest,
            publisher.manifest.masterKey,
            list,
            signature,
            1,
            std::nullopt);
        BEAST_EXPECT(v1[jss::version].asUInt() == 1);
        BEAST_EXPECT(v1[jss::public_key].asString() == strHex(publisher.manifest.masterKey));
        BEAST_EXPECT(v1[jss::manifest].asString() == publisher.token.manifest);
        BEAST_EXPECT(base64Decode(v1[jss::blob].asString()) == list.canonical);
        BEAST_EXPECT(v1[jss::signature].asString() == signature);
        {
            auto const result = verifyList(v1, list, publisher.manifest.masterKey, now);
            BEAST_EXPECTS(result.ok, to_string(result.report));
            BEAST_EXPECT(result.report["blobs"].size() == 1);
            BEAST_EXPECT(result.report["blobs"][0u][jss::sequence].asUInt() == 7);
            BEAST_EXPECT(result.report["blobs"][0u][jss::validators].asUInt() == 3);
            BEAST_EXPECT(!result.report["blobs"][0u]["expired"].asBool());
            BEAST_EXPECT(result.report["manifest_sequence"].asUInt() == 1);
        }

        // Version 2, then a second blob appended
        auto const v2 = makeSignedList(
            publisher.token.manifest,
            publisher.manifest.masterKey,
            list,
            signature,
            2,
            std::nullopt);
        BEAST_EXPECT(v2[jss::version].asUInt() == 2);
        BEAST_EXPECT(v2[jss::blobs_v2].size() == 1);
        BEAST_EXPECT(!v2.isMember(jss::blob));
        BEAST_EXPECT(verifyList(v2, list, std::nullopt, now).ok);

        auto const later = parseUnsignedList(unsignedListText(validators, 8, now + 300, now + 200));
        auto const laterSig =
            signList(later, *publisher.manifest.signingKey, publisher.token.validationSecret);
        auto const v2b = makeSignedList(
            publisher.token.manifest, publisher.manifest.masterKey, later, laterSig, 2, v2);
        BEAST_EXPECT(v2b[jss::blobs_v2].size() == 2);
        {
            auto const result = verifyList(v2b, std::nullopt, std::nullopt, now);
            BEAST_EXPECTS(result.ok, to_string(result.report));
            BEAST_EXPECT(result.report["blobs"][1u][jss::effective].asUInt() == now + 200);
        }

        try
        {
            makeSignedList(
                publisher.token.manifest,
                publisher.manifest.masterKey,
                list,
                signature,
                3,
                std::nullopt);
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() == std::string("Unsupported list version"));
        }

        // Append after the signing key rotated: the earlier blobs keep their manifest
        {
            SigningKeys rotated = publisher.keys;
            auto const token2 = *rotated.createValidatorToken(KeyType::Ed25519);
            auto const manifest2 = *deserializeManifest(base64Decode(token2.manifest));
            auto const sig2 = signList(later, *manifest2.signingKey, token2.validationSecret);
            auto const v2c =
                makeSignedList(token2.manifest, manifest2.masterKey, later, sig2, 2, v2b);
            BEAST_EXPECT(v2c[jss::blobs_v2].size() == 3);
            BEAST_EXPECT(v2c[jss::manifest].asString() == token2.manifest);
            BEAST_EXPECT(
                v2c[jss::blobs_v2][0u][jss::manifest].asString() == publisher.token.manifest);
            BEAST_EXPECT(!v2c[jss::blobs_v2][2u].isMember(jss::manifest));
            auto const result = verifyList(v2c, std::nullopt, std::nullopt, now);
            BEAST_EXPECTS(result.ok, to_string(result.report));
            BEAST_EXPECT(result.report["manifest_sequence"].asUInt() == 2);
        }

        // Append refuses the wrong shape, another publisher, and a full list
        try
        {
            makeSignedList(
                publisher.token.manifest, publisher.manifest.masterKey, list, signature, 1, v2);
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(
                e.what() ==
                std::string("A version 1 list holds one blob; use version 2 to append"));
        }
        try
        {
            makeSignedList(
                publisher.token.manifest, publisher.manifest.masterKey, list, signature, 2, v1);
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() == std::string("The list to append to is not a version 2 list"));
        }
        {
            Publisher const other;
            try
            {
                makeSignedList(
                    other.token.manifest, other.manifest.masterKey, list, signature, 2, v2);
                fail();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    e.what() == std::string("The list to append to belongs to another master key"));
            }
        }
        {
            auto full = v2;
            while (full[jss::blobs_v2].size() < 5)
                full[jss::blobs_v2].append(full[jss::blobs_v2][0u]);
            try
            {
                makeSignedList(
                    publisher.token.manifest,
                    publisher.manifest.masterKey,
                    list,
                    signature,
                    2,
                    full);
                fail();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    e.what() == std::string("The list to append to already holds 5 blobs"));
            }
        }
    }

    void
    testVerifyRejects()
    {
        testcase("Verify Rejects");

        Publisher const publisher;
        auto const validators = makeValidators(2);
        std::uint32_t const now = 1000;
        auto const list = parseUnsignedList(unsignedListText(validators, 7, now + 100));
        auto const signature =
            signList(list, *publisher.manifest.signingKey, publisher.token.validationSecret);
        auto const good = makeSignedList(
            publisher.token.manifest,
            publisher.manifest.masterKey,
            list,
            signature,
            1,
            std::nullopt);

        auto expectError = [this](
                               json::Value const& doc,
                               std::optional<UnsignedList> const& roster,
                               std::optional<PublicKey> const& key,
                               std::uint32_t at,
                               std::string const& expected) {
            auto const result = verifyList(doc, roster, key, at);
            BEAST_EXPECT(!result.ok);
            bool found = false;
            for (auto const& e : result.errors)
                found = found || e == expected;
            BEAST_EXPECTS(found, to_string(result.report));
        };

        // Tampered blob: the signature no longer matches
        {
            auto tampered = good;
            auto text = list.canonical;
            text.replace(text.find("\"sequence\": 7"), 13, "\"sequence\": 9");
            tampered[jss::blob] = base64Encode(text);
            expectError(
                tampered,
                std::nullopt,
                std::nullopt,
                now,
                "blob 0: the signature does not verify under the signing key");
        }
        // Expired
        expectError(good, std::nullopt, std::nullopt, now + 100, "blob 0: expired");
        // Wrong manifest: another publisher's
        {
            Publisher const other;
            auto wrong = good;
            wrong[jss::manifest] = other.token.manifest;
            expectError(
                wrong,
                std::nullopt,
                std::nullopt,
                now,
                "\"public_key\" is not the manifest's master key");
        }
        // Not the expected key
        {
            Publisher const other;
            expectError(
                good,
                std::nullopt,
                other.manifest.masterKey,
                now,
                "the master key is not the expected key");
        }
        // Roster mismatch
        {
            auto const others = makeValidators(2);
            auto const roster = parseUnsignedList(unsignedListText(others, 1, now + 100));
            expectError(
                good,
                roster,
                std::nullopt,
                now,
                "blob 0: the validators differ from the expected list");
        }
        // Structural
        {
            auto bad = good;
            bad[jss::version] = 3;
            expectError(bad, std::nullopt, std::nullopt, now, "\"version\" must be 1 or 2");
        }
        {
            auto bad = good;
            bad[jss::blobs_v2] = json::Value(json::ValueType::Array);
            expectError(
                bad,
                std::nullopt,
                std::nullopt,
                now,
                "a version 1 list needs \"blob\" and \"signature\" and no \"blobs_v2\"");
        }
        {
            auto bad = good;
            bad[jss::manifest] = "AAAA";
            expectError(
                bad,
                std::nullopt,
                std::nullopt,
                now,
                "\"manifest\" does not deserialize and verify");

            expectError(
                json::Value(json::ValueType::Array),
                std::nullopt,
                std::nullopt,
                now,
                "the list is not a JSON object");
            {
                auto bad = good;
                bad[jss::public_key] = 1;
                expectError(
                    bad,
                    std::nullopt,
                    std::nullopt,
                    now,
                    "\"public_key\" and \"manifest\" must be strings");
            }
            {
                // A revoked publisher
                SigningKeys revoked(KeyType::Ed25519);
                auto bad = good;
                bad[jss::manifest] = revoked.revoke();
                bad[jss::public_key] = strHex(revoked.publicKey());
                expectError(
                    bad, std::nullopt, std::nullopt, now, "the publisher's master key is revoked");
            }
            {
                // A blob that parses as JSON but is not a list; the signature fails too
                auto bad = good;
                bad[jss::blob] = base64Encode("{}");
                expectError(
                    bad,
                    std::nullopt,
                    std::nullopt,
                    now,
                    "blob 0: \"sequence\" must be a positive integer");
            }

            // Version 2 structure
            auto const v2 = makeSignedList(
                publisher.token.manifest,
                publisher.manifest.masterKey,
                list,
                signature,
                2,
                std::nullopt);
            std::string const v2Shape =
                "a version 2 list needs 1 to 5 \"blobs_v2\" entries and no top-level \"blob\"";
            {
                auto bad = v2;
                bad[jss::blobs_v2] = json::Value(json::ValueType::Array);
                expectError(bad, std::nullopt, std::nullopt, now, v2Shape);
            }
            {
                auto bad = v2;
                bad[jss::blob] = "x";
                expectError(bad, std::nullopt, std::nullopt, now, v2Shape);
            }
            {
                auto bad = v2;
                bad[jss::blobs_v2][0u].removeMember(jss::signature);
                expectError(
                    bad,
                    std::nullopt,
                    std::nullopt,
                    now,
                    "every \"blobs_v2\" entry needs \"blob\" and \"signature\"");
            }
            {
                auto bad = v2;
                bad[jss::blobs_v2][0u][jss::manifest] = 5;
                expectError(
                    bad,
                    std::nullopt,
                    std::nullopt,
                    now,
                    "a \"blobs_v2\" entry's \"manifest\" must be a string");
            }
            {
                Publisher const other;
                auto bad = v2;
                bad[jss::blobs_v2][0u][jss::manifest] = other.token.manifest;
                expectError(
                    bad,
                    std::nullopt,
                    std::nullopt,
                    now,
                    "a \"blobs_v2\" entry's \"manifest\" is not this publisher's");
                // The publisher's own manifest in an entry is accepted
                auto fine = v2;
                fine[jss::blobs_v2][0u][jss::manifest] = publisher.token.manifest;
                BEAST_EXPECT(verifyList(fine, std::nullopt, std::nullopt, now).ok);
            }
        }
    }

    void
    testFiles()
    {
        testcase("Token and Manifest Files");

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());

        Publisher const publisher;

        // A token file as `create_token` writes it: header, comment, 72-char lines
        path const tokenFile = subdir / "token.txt";
        {
            std::ofstream o(tokenFile.string());
            o << "# validator public key: "
              << toBase58(TokenType::NodePublic, publisher.keys.publicKey()) << "\n\n";
            o << "[validator_token]\n";
            auto const body = tokenToBase64(publisher.token);
            for (std::size_t i = 0; i < body.size(); i += 72)
                o << body.substr(i, 72) << "\n";
        }
        {
            auto const token = loadTokenFile(tokenFile);
            BEAST_EXPECT(token.manifest == publisher.token.manifest);
            BEAST_EXPECT(
                std::equal(
                    token.validationSecret.begin(),
                    token.validationSecret.end(),
                    publisher.token.validationSecret.begin()));
        }

        path const manifestFile = subdir / "manifest.txt";
        {
            std::ofstream o(manifestFile.string());
            o << "# publisher manifest\n" << publisher.token.manifest << "\n";
        }
        {
            auto const manifest = loadManifestFile(manifestFile);
            BEAST_EXPECT(manifest.masterKey == publisher.manifest.masterKey);
            BEAST_EXPECT(manifest.signingKey == publisher.manifest.signingKey);
        }

        try
        {
            loadTokenFile(manifestFile);
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() == "Not a validator token: " + manifestFile.string());
        }
        {
            path const bad = subdir / "bad-manifest.txt";
            std::ofstream o(bad.string());
            o << "AAAA\n";
            o.close();
            try
            {
                loadManifestFile(bad);
                fail();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(e.what() == "Not a valid manifest: " + bad.string());
            }
        }
        try
        {
            loadManifestFile(subdir / "missing.txt");
            fail();
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(e.what() == "Failed to open file: " + (subdir / "missing.txt").string());
        }

        path const listFile = subdir / "unsigned.json";
        {
            std::ofstream o(listFile.string());
            o << unsignedListText(makeValidators(1), 3, 5000);
        }
        auto const list = loadUnsignedList(listFile);
        BEAST_EXPECT(list.sequence == 3 && list.validators.size() == 1);
    }

public:
    void
    run() override
    {
        testCanonicalJson();
        testParseUnsignedList();
        testSignAndVerify();
        testVerifyRejects();
        testFiles();
    }
};

BEAST_DEFINE_TESTSUITE(ListSigning, keys, xrpl);

}  // namespace tests

}  // namespace xrpl
