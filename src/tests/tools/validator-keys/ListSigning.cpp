#include <tools/validator-keys/ListSigning.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/Manifest.h>

#include <gtest/gtest.h>
#include <tools/validator-keys/SigningKeys.h>

#include <Fixtures.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xrpl::tools::test {

namespace {

std::string const kNotObject = "Not a JSON object";

class ListSigningTest : public ::testing::Test
{
protected:
    Publisher const publisher_;
    std::vector<ValidatorToken> const validators_ = makeValidators(3);
    std::uint32_t const now_ = 1000;
    UnsignedList const list_ = parseUnsignedList(unsignedListText(validators_, 7, now_ + 100));
    std::string const signature_ =
        signList(list_, publisher_.signingKey, publisher_.token.validationSecret);

    [[nodiscard]] json::Value
    signed1() const
    {
        return makeSignedList(
            publisher_.token.manifest,
            publisher_.manifest.masterKey,
            list_,
            signature_,
            1,
            std::nullopt,
            {});
    }

    [[nodiscard]] json::Value
    signed2() const
    {
        return makeSignedList(
            publisher_.token.manifest,
            publisher_.manifest.masterKey,
            list_,
            signature_,
            2,
            std::nullopt,
            {});
    }

    static std::string
    errorOfParse(std::string const& text)
    {
        return errorOf([&] { parseUnsignedList(text); });
    }

    // Verifies and expects @p error among the report's errors.
    void
    expectError(
        json::Value const& doc,
        std::string const& error,
        std::optional<UnsignedList> const& roster = std::nullopt,
        std::optional<PublicKey> const& key = std::nullopt,
        std::optional<std::uint32_t> at = std::nullopt) const
    {
        auto const report = verifyList(doc, roster, key, at.value_or(now_));
        EXPECT_FALSE(report["ok"].asBool());
        bool found = false;
        for (auto const& e : report["errors"])
            found = found || e.asString() == error;
        EXPECT_TRUE(found) << to_string(report);
    }

    void
    expectOk(json::Value const& doc) const
    {
        auto const report = verifyList(doc, std::nullopt, std::nullopt, now_);
        EXPECT_TRUE(report["ok"].asBool()) << to_string(report);
    }
};

}  // namespace

TEST_F(ListSigningTest, canonical_json)
{
    // Whitespace and comments outside strings go, one space follows each comma
    // and colon, key order and string contents stay.
    EXPECT_EQ(
        canonicalJson("{ \"b\" :1,\n\t\"a\": [ 1 , 2 ] , \"s\":\"x, y: z\" }"),
        "{\"b\": 1, \"a\": [1, 2], \"s\": \"x, y: z\"}");
    EXPECT_EQ(canonicalJson("{\"e\": \"a\\\"b\"}"), "{\"e\": \"a\\\"b\"}");
    EXPECT_EQ(
        canonicalJson("{\"sequence\": 1, // reviewed\n \"x\": /* two */ 2}"),
        "{\"sequence\": 1, \"x\": 2}");
    EXPECT_EQ(canonicalJson("{\"u\": \"http://x\"}"), "{\"u\": \"http://x\"}");

    EXPECT_EQ(errorOf([] { canonicalJson("[1, 2]"); }), kNotObject);
    EXPECT_EQ(errorOf([] { canonicalJson("{\"a\": "); }), kNotObject);
    EXPECT_EQ(errorOf([] { canonicalJson("{\"a\": 1} // open"); }), "");
    EXPECT_EQ(errorOf([] { canonicalJson("{\"a\": 1 /* open"); }), kNotObject);
}

TEST_F(ListSigningTest, parse_unsigned_list)
{
    {
        auto const l = parseUnsignedList(unsignedListText(validators_, 5, 1000, 500));
        EXPECT_EQ(l.sequence, 5u);
        EXPECT_EQ(l.expiration, 1000u);
        EXPECT_EQ(l.effective, 500u);
        EXPECT_EQ(l.validators.size(), 3u);
        EXPECT_EQ(
            l.validators[0],
            required(deserializeManifest(base64Decode(validators_[0].manifest))).masterKey);
    }
    {
        auto const l = parseUnsignedList(unsignedListText(validators_, 5, 1000));
        EXPECT_FALSE(l.effective);
        EXPECT_TRUE(l.canonical.starts_with("{\"sequence\": 5, \"expiration\": 1000, "));
    }

    std::string const sequenceError = "\"sequence\" must be an integer from 1 to 2147483647";
    std::string const expirationError = "\"expiration\" must be an integer from 1 to 2147483647";
    EXPECT_EQ(errorOfParse("{\"expiration\": 1, \"validators\": []}"), sequenceError);
    EXPECT_EQ(
        errorOfParse("{\"sequence\": 0, \"expiration\": 1, \"validators\": []}"), sequenceError);
    EXPECT_EQ(
        errorOfParse("{\"sequence\": true, \"expiration\": 1, \"validators\": []}"), sequenceError);
    EXPECT_EQ(
        errorOfParse("{\"sequence\": 2147483648, \"expiration\": 1, \"validators\": []}"),
        sequenceError);
    EXPECT_EQ(errorOfParse("{\"sequence\": 1, \"validators\": []}"), expirationError);
    EXPECT_EQ(
        errorOfParse("{\"sequence\": 1, \"expiration\": -1, \"validators\": []}"), expirationError);
    EXPECT_EQ(
        errorOfParse("{\"sequence\": 1, \"expiration\": 0, \"validators\": []}"), expirationError);
    {
        auto twice = validators_;
        twice.push_back(validators_[0]);
        auto const key =
            strHex(required(deserializeManifest(base64Decode(validators_[0].manifest))).masterKey);
        EXPECT_EQ(
            errorOfParse(unsignedListText(twice, 1, 1000)),
            "\"validators\" lists " + key + " more than once");
    }
    EXPECT_EQ(
        errorOfParse(
            "{\"sequence\": 1, \"effective\": 1000, \"expiration\": 1000, \"validators\": []}"),
        "\"effective\" must be earlier than \"expiration\"");
    EXPECT_EQ(
        errorOfParse(
            "{\"sequence\": 1, \"effective\": \"x\", \"expiration\": 1000, \"validators\": []}"),
        "\"effective\" must be an integer from 0 to 2147483647");
    EXPECT_EQ(
        errorOfParse("{\"sequence\": 1, \"expiration\": 1000, \"validators\": []}"),
        "\"validators\" must be a non-empty array");
    EXPECT_EQ(
        errorOfParse("{\"sequence\": 1, \"expiration\": 1000, \"validators\": [{}]}"),
        "every validator needs a \"validation_public_key\" string");
    for (auto const* bad :
         {"zz", "ED00", "FF00000000000000000000000000000000000000000000000000000000000000"})
    {
        EXPECT_EQ(
            errorOfParse(
                std::string(
                    "{\"sequence\": 1, \"expiration\": 1000, \"validators\": "
                    "[{\"validation_public_key\": \"") +
                bad + "\"}]}"),
            std::string("\"validation_public_key\" is not a hex public key: ") + bad);
    }
    {
        auto const key =
            strHex(required(deserializeManifest(base64Decode(validators_[0].manifest))).masterKey);
        auto const other =
            strHex(required(deserializeManifest(base64Decode(validators_[1].manifest))).masterKey);
        auto const entry = [](std::string const& key, std::string const& manifest) {
            return "{\"sequence\": 1, \"expiration\": 1000, \"validators\": "
                   "[{\"validation_public_key\": \"" +
                key + R"(", "manifest": )" + manifest + "}]}";
        };
        EXPECT_EQ(
            errorOfParse(entry(other, "\"" + validators_[0].manifest + "\"")),
            "\"manifest\" belongs to another key than " + other);
        EXPECT_EQ(errorOfParse(entry(key, "\"AAAA\"")), "\"manifest\" does not verify for " + key);
        EXPECT_EQ(errorOfParse(entry(key, "5")), "\"manifest\" must be a base64 string for " + key);
    }
}

TEST_F(ListSigningTest, files)
{
    TempDir const dir;

    // A token file as create_token writes it: header, comment, 72-character lines
    auto const tokenFile = std::filesystem::path(dir.file("token.txt"));
    {
        std::string text = "# validator public key: " +
            toBase58(TokenType::NodePublic, publisher_.keys.publicKey()) +
            "\n\n[validator_token]\n";
        auto const body = tokenToBase64(publisher_.token);
        for (std::size_t i = 0; i < body.size(); i += 72)
            text += body.substr(i, 72) + "\n";
        writeFile(tokenFile, text);
    }
    auto const token = loadTokenFile(tokenFile);
    EXPECT_EQ(token.manifest, publisher_.token.manifest);
    EXPECT_TRUE(sameSecret(token.validationSecret, publisher_.token.validationSecret));

    auto const manifestFile = std::filesystem::path(dir.file("manifest.txt"));
    writeFile(manifestFile, "# publisher manifest\n" + publisher_.token.manifest + "\n");
    auto const manifest = loadManifestFile(manifestFile);
    EXPECT_EQ(manifest.masterKey, publisher_.manifest.masterKey);
    EXPECT_EQ(manifest.signingKey, publisher_.manifest.signingKey);

    EXPECT_EQ(
        errorOf([&] { loadTokenFile(manifestFile); }),
        "Not a validator token: " + manifestFile.string());
    auto const bad = std::filesystem::path(dir.file("bad-manifest.txt"));
    writeFile(bad, "AAAA\n");
    EXPECT_EQ(errorOf([&] { loadManifestFile(bad); }), "Not a valid manifest: " + bad.string());
    auto const missing = std::filesystem::path(dir.file("missing.txt"));
    EXPECT_EQ(
        errorOf([&] { loadManifestFile(missing); }), "Failed to open file: " + missing.string());

    auto const listFile = std::filesystem::path(dir.file("unsigned.json"));
    writeFile(listFile, unsignedListText(validators_, 3, 5000));
    EXPECT_EQ(loadUnsignedList(listFile).sequence, 3u);
}

TEST_F(ListSigningTest, sign_and_verify_version_1)
{
    auto const v1 = signed1();
    EXPECT_EQ(v1[jss::version].asUInt(), 1u);
    EXPECT_EQ(v1[jss::public_key].asString(), strHex(publisher_.manifest.masterKey));
    EXPECT_EQ(v1[jss::manifest].asString(), publisher_.token.manifest);
    EXPECT_EQ(base64Decode(v1[jss::blob].asString()), list_.canonical);
    EXPECT_EQ(v1[jss::signature].asString(), signature_);

    auto const report = verifyList(v1, list_, publisher_.manifest.masterKey, now_);
    EXPECT_TRUE(report["ok"].asBool()) << to_string(report);
    EXPECT_EQ(report["blobs"].size(), 1u);
    EXPECT_EQ(report["blobs"][0u][jss::sequence].asUInt(), 7u);
    EXPECT_EQ(report["blobs"][0u][jss::validators].asUInt(), 3u);
    EXPECT_FALSE(report["blobs"][0u]["expired"].asBool());
    EXPECT_EQ(report["manifest_sequence"].asUInt(), 1u);
    EXPECT_EQ(report["signing_key"].asString(), strHex(publisher_.signingKey));

    EXPECT_EQ(
        errorOf([&] {
            makeSignedList(
                publisher_.token.manifest,
                publisher_.manifest.masterKey,
                list_,
                signature_,
                3,
                std::nullopt,
                {});
        }),
        "Unsupported list version");
}

TEST_F(ListSigningTest, sign_and_verify_version_2)
{
    auto const v2 = signed2();
    EXPECT_EQ(v2[jss::version].asUInt(), 2u);
    EXPECT_EQ(v2[jss::blobs_v2].size(), 1u);
    EXPECT_FALSE(v2.isMember(jss::blob));
    expectOk(v2);

    // A second blob appended under the same manifest
    auto const later = parseUnsignedList(unsignedListText(validators_, 8, now_ + 300, now_ + 200));
    auto const laterSig = signList(later, publisher_.signingKey, publisher_.token.validationSecret);
    auto const v2b = makeSignedList(
        publisher_.token.manifest, publisher_.manifest.masterKey, later, laterSig, 2, v2, {});
    EXPECT_EQ(v2b[jss::blobs_v2].size(), 2u);
    EXPECT_EQ(v2b[jss::blobs_v2][0u][jss::signature].asString(), signature_);
    {
        auto const report = verifyList(v2b, std::nullopt, std::nullopt, now_);
        EXPECT_TRUE(report["ok"].asBool()) << to_string(report);
        EXPECT_EQ(report["blobs"][1u][jss::effective].asUInt(), now_ + 200);
    }

    // After the signing key rotates the earlier blobs are signed again with the
    // new key, because a server verifies every blob under the newest manifest.
    SigningKeys rotated = publisher_.keys;
    auto const token2 = rotated.createToken(KeyType::Ed25519);
    auto const manifest2 = required(deserializeManifest(base64Decode(token2.manifest)));
    auto const signingKey2 = required(manifest2.signingKey);
    auto const resign = [&](std::string const& bytes) {
        return strHex(sign(signingKey2, token2.validationSecret, makeSlice(bytes)));
    };
    auto const third = parseUnsignedList(unsignedListText(validators_, 9, now_ + 400, now_ + 350));
    EXPECT_EQ(
        errorOf([&] {
            makeSignedList(
                token2.manifest, manifest2.masterKey, third, resign(third.canonical), 2, v2b, {});
        }),
        "The list to append to was signed under another manifest and its blobs need signing "
        "again");
    auto const v2c = makeSignedList(
        token2.manifest, manifest2.masterKey, third, resign(third.canonical), 2, v2b, resign);
    EXPECT_EQ(v2c[jss::blobs_v2].size(), 3u);
    EXPECT_EQ(v2c[jss::manifest].asString(), token2.manifest);
    EXPECT_NE(v2c[jss::blobs_v2][0u][jss::signature].asString(), signature_);
    EXPECT_EQ(
        v2c[jss::blobs_v2][0u][jss::blob].asString(), v2b[jss::blobs_v2][0u][jss::blob].asString());
    EXPECT_FALSE(v2c[jss::blobs_v2][0u].isMember(jss::manifest));
    {
        auto const report = verifyList(v2c, std::nullopt, std::nullopt, now_);
        EXPECT_TRUE(report["ok"].asBool()) << to_string(report);
        EXPECT_EQ(report["manifest_sequence"].asUInt(), 2u);
        EXPECT_EQ(report["signing_key"].asString(), strHex(signingKey2));
    }

    // Append refuses the wrong shape, another publisher, a broken blob and a full list
    EXPECT_EQ(
        errorOf([&] {
            makeSignedList(
                publisher_.token.manifest,
                publisher_.manifest.masterKey,
                list_,
                signature_,
                1,
                v2,
                {});
        }),
        "A version 1 list holds one blob; use version 2 to append");
    EXPECT_EQ(
        errorOf([&] {
            makeSignedList(
                publisher_.token.manifest,
                publisher_.manifest.masterKey,
                list_,
                signature_,
                2,
                signed1(),
                {});
        }),
        "The list to append to is not a version 2 list");
    {
        Publisher const other;
        EXPECT_EQ(
            errorOf([&] {
                makeSignedList(
                    other.token.manifest, other.manifest.masterKey, list_, signature_, 2, v2, {});
            }),
            "The list to append to belongs to another master key");
    }
    {
        auto broken = v2;
        broken[jss::blobs_v2][0u][jss::blob] = 5;
        EXPECT_EQ(
            errorOf([&] {
                makeSignedList(
                    token2.manifest, manifest2.masterKey, third, "00", 2, broken, resign);
            }),
            "The list to append to holds an invalid blob");
    }
    {
        auto full = v2;
        while (full[jss::blobs_v2].size() < 5)
            full[jss::blobs_v2].append(full[jss::blobs_v2][0u]);
        EXPECT_EQ(
            errorOf([&] {
                makeSignedList(
                    publisher_.token.manifest,
                    publisher_.manifest.masterKey,
                    list_,
                    signature_,
                    2,
                    full,
                    {});
            }),
            "The list to append to already holds 5 blobs");
    }
}

TEST_F(ListSigningTest, verify_rejects)
{
    auto const good = signed1();

    {
        auto tampered = good;
        auto text = list_.canonical;
        text.replace(text.find("\"sequence\": 7"), 13, "\"sequence\": 9");
        tampered[jss::blob] = base64Encode(text);
        expectError(tampered, "blob 0: the signature does not verify under the signing key");
    }
    expectError(good, "blob 0: expired", std::nullopt, std::nullopt, now_ + 100);
    {
        Publisher const other;
        auto wrong = good;
        wrong[jss::manifest] = other.token.manifest;
        expectError(wrong, "\"public_key\" is not the manifest's master key");
        expectError(
            good, "the master key is not the expected key", std::nullopt, other.manifest.masterKey);
    }
    {
        auto const roster = parseUnsignedList(unsignedListText(makeValidators(2), 1, now_ + 100));
        expectError(good, "blob 0: the validators differ from the expected list", roster);
    }
    expectError(json::Value(json::ValueType::Array), "the list is not a JSON object");
    for (auto const version : {0, 3})
    {
        auto bad = good;
        bad[jss::version] = version;
        expectError(bad, "\"version\" must be 1 or 2");
    }
    {
        auto bad = good;
        bad[jss::public_key] = 1;
        expectError(bad, R"("public_key" and "manifest" must be strings)");
    }
    {
        auto bad = good;
        bad[jss::manifest] = "AAAA";
        expectError(bad, "\"manifest\" does not deserialize and verify");
    }
    {
        SigningKeys revoked(KeyType::Ed25519);
        auto bad = good;
        bad[jss::manifest] = revoked.revoke();
        bad[jss::public_key] = strHex(revoked.publicKey());
        expectError(bad, "the publisher's master key is revoked");
    }
    {
        auto bad = good;
        bad[jss::blob] = base64Encode("{}");
        expectError(bad, "blob 0: \"sequence\" must be an integer from 1 to 2147483647");
    }
    {
        auto bad = good;
        bad[jss::blobs_v2] = json::Value(json::ValueType::Array);
        expectError(bad, R"(a version 1 list needs "blob" and "signature" and no "blobs_v2")");
    }

    auto const v2 = signed2();
    std::string const v2Shape =
        R"(a version 2 list needs 1 to 5 "blobs_v2" entries and no top-level "blob")";
    {
        auto bad = v2;
        bad[jss::blobs_v2] = json::Value(json::ValueType::Array);
        expectError(bad, v2Shape);
        bad = v2;
        bad[jss::blob] = "x";
        expectError(bad, v2Shape);
    }
    std::string const entryShape =
        "every \"blobs_v2\" entry needs \"blob\" and \"signature\" strings and an optional "
        "\"manifest\" string";
    {
        auto bad = v2;
        bad[jss::blobs_v2][0u].removeMember(jss::signature);
        expectError(bad, entryShape);
        bad = v2;
        bad[jss::blobs_v2][0u][jss::manifest] = 5;
        expectError(bad, entryShape);
    }
    {
        Publisher const other;
        auto bad = v2;
        bad[jss::blobs_v2][0u][jss::manifest] = other.token.manifest;
        expectError(bad, "blob 0: its \"manifest\" is not this publisher's");

        SigningKeys revoked = publisher_.keys;
        bad[jss::blobs_v2][0u][jss::manifest] = revoked.revoke();
        expectError(bad, "blob 0: its \"manifest\" revokes the publisher's master key");

        // An entry carrying the publisher's own manifest changes nothing
        auto fine = v2;
        fine[jss::blobs_v2][0u][jss::manifest] = publisher_.token.manifest;
        expectOk(fine);
    }
    {
        // An entry with a newer manifest moves the signing key for that blob and
        // the ones after it, as a server would.
        SigningKeys rotated = publisher_.keys;
        auto const token2 = rotated.createToken(KeyType::Ed25519);
        auto const manifest2 = required(deserializeManifest(base64Decode(token2.manifest)));
        auto const later = parseUnsignedList(unsignedListText(validators_, 8, now_ + 300));
        json::Value entry(json::ValueType::Object);
        entry[jss::blob] = base64Encode(later.canonical);
        entry[jss::signature] =
            signList(later, required(manifest2.signingKey), token2.validationSecret);
        entry[jss::manifest] = token2.manifest;
        auto newer = v2;
        newer[jss::blobs_v2].append(entry);
        auto const report = verifyList(newer, std::nullopt, std::nullopt, now_);
        EXPECT_TRUE(report["ok"].asBool()) << to_string(report);
        EXPECT_EQ(report["manifest_sequence"].asUInt(), 2u);

        // Placed first, it invalidates the blob signed under the older key
        auto reordered = v2;
        reordered[jss::blobs_v2] = json::Value(json::ValueType::Array);
        reordered[jss::blobs_v2].append(entry);
        reordered[jss::blobs_v2].append(v2[jss::blobs_v2][0u]);
        expectError(reordered, "blob 1: the signature does not verify under the signing key");
    }
}

}  // namespace xrpl::tools::test
