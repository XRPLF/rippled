#include <tools/validator-keys/SigningKeys.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/Manifest.h>

#include <gtest/gtest.h>

#include <Fixtures.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace xrpl::tools::test {

namespace {

constexpr std::array<KeyType, 2> kKeyTypes{{KeyType::Ed25519, KeyType::Secp256k1}};
constexpr std::uint32_t kMaxSequence = std::numeric_limits<std::uint32_t>::max();

std::string const kBadManifest = "Manifest is not properly signed";
std::string const kRevoked = "Validator keys have been revoked.";
std::string const kExhausted =
    "Maximum number of tokens have already been generated.\n"
    "Revoke validator keys if previous token has been compromised.";

class SigningKeysTest : public ::testing::Test
{
protected:
    TempDir dir_;
    std::filesystem::path keyFile_{dir_.file("validator_keys.json")};

    void
    writeKeyFile(json::Value const& jv)
    {
        writeFile(keyFile_, jv.toStyledString());
    }

    std::string
    loadError(json::Value const& jv)
    {
        writeKeyFile(jv);
        return errorOf([&] { SigningKeys::makeSigningKeys(keyFile_); });
    }

    json::Value
    keyFileJson()
    {
        json::Reader reader;
        json::Value jv;
        reader.parse(readFile(keyFile_), jv);
        return jv;
    }

    [[nodiscard]] std::string
    invalidField(std::string const& field) const
    {
        return "Key file '" + keyFile_.string() + "' contains invalid \"" + field + "\" field";
    }

    static json::Value
    baseKeyFile(SecretKey const& secret)
    {
        json::Value jv;
        jv["key_type"] = "ed25519";
        jv["secret_key"] = toBase58(TokenType::NodePrivate, secret);
        jv["token_sequence"] = 1;
        jv["revoked"] = false;
        return jv;
    }
};

// The manifest of a token or a revocation, parsed and checked against the
// master key that made it.
Manifest
manifestOf(std::string const& base64, SigningKeys const& keys)
{
    auto m = required(deserializeManifest(base64Decode(base64)));
    EXPECT_TRUE(m.verify());
    EXPECT_EQ(m.masterKey, keys.publicKey());
    return m;
}

}  // namespace

TEST_F(SigningKeysTest, key_file_round_trip)
{
    for (auto const keyType : kKeyTypes)
    {
        SigningKeys const keys(keyType);
        keys.writeToFile(keyFile_);
        EXPECT_TRUE(std::filesystem::exists(keyFile_));
        EXPECT_FALSE(std::filesystem::exists(keyFile_.string() + ".tmp"));
        auto const perms = std::filesystem::status(keyFile_).permissions();
        EXPECT_EQ(
            perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all),
            std::filesystem::perms::none);
        EXPECT_TRUE(keys == SigningKeys::makeSigningKeys(keyFile_));
    }

    // A token, a domain and a pending external token all survive the round trip
    SigningKeys keys(KeyType::Ed25519);
    keys.domain("validator.example.com");
    keys.createToken();
    SigningKeys const signer(KeyType::Ed25519);
    keys.startToken(KeyType::Ed25519, signer.publicKey());
    keys.writeToFile(keyFile_);
    EXPECT_TRUE(keys == SigningKeys::makeSigningKeys(keyFile_));

    // So does a pending token with a generated signing key
    SigningKeys other(KeyType::Secp256k1);
    SigningKeys const before = other;
    other.startToken(KeyType::Secp256k1);
    other.writeToFile(keyFile_);
    EXPECT_TRUE(other == SigningKeys::makeSigningKeys(keyFile_));
    EXPECT_FALSE(other == keys);
    EXPECT_FALSE(other == before);

    // The same master key with a pending token of the other kind
    SigningKeys external = before;
    external.startToken(KeyType::Secp256k1, signer.publicKey());
    EXPECT_FALSE(other == external);
}

TEST_F(SigningKeysTest, write_to_file_errors)
{
    SigningKeys const keys(KeyType::Ed25519);

    auto const nested = dir_.file("a/b/c/validator_keys.json");
    keys.writeToFile(nested);
    EXPECT_TRUE(keys == SigningKeys::makeSigningKeys(nested));
    EXPECT_EQ(
        std::filesystem::status(std::filesystem::path(nested).parent_path()).permissions() &
            (std::filesystem::perms::group_all | std::filesystem::perms::others_all),
        std::filesystem::perms::none);

    // The parent path is a file
    auto const blocked = std::filesystem::path(keyFile_.string() + "/keys.json");
    keys.writeToFile(keyFile_);
    EXPECT_EQ(
        errorOf([&] { keys.writeToFile(blocked); }),
        "Cannot create directory: " + blocked.parent_path().string());

    // The target is a directory
    auto const directory = std::filesystem::path(dir_.file("dir"));
    std::filesystem::create_directory(directory);
    EXPECT_EQ(
        errorOf([&] { keys.writeToFile(directory); }),
        "Cannot write key file: " + directory.string());

    // The directory cannot be written to
    auto const sealed = std::filesystem::path(dir_.file("sealed"));
    std::filesystem::create_directory(sealed);
    std::filesystem::permissions(
        sealed, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    auto const inSealed = sealed / "keys.json";
    EXPECT_EQ(
        errorOf([&] { keys.writeToFile(inSealed); }),
        "Cannot write key file: " + inSealed.string());
    std::filesystem::permissions(sealed, std::filesystem::perms::owner_all);

    // The temporary beside the key file is a symlink
    auto const linked = std::filesystem::path(dir_.file("linked.json"));
    std::filesystem::create_symlink(dir_.file("elsewhere.json"), linked.string() + ".tmp");
    EXPECT_EQ(
        errorOf([&] { keys.writeToFile(linked); }),
        "Refusing to write through a symlink: " + linked.string() + ".tmp");
}

TEST_F(SigningKeysTest, key_file_fields)
{
    EXPECT_EQ(
        errorOf([&] { SigningKeys::makeSigningKeys(keyFile_); }),
        "Failed to open key file: " + keyFile_.string());

    writeFile(keyFile_, "{{}");
    EXPECT_EQ(
        errorOf([&] { SigningKeys::makeSigningKeys(keyFile_); }),
        "Unable to parse json key file: " + keyFile_.string());

    json::Value jv;
    jv["dummy"] = "field";
    for (auto const* field : {"key_type", "secret_key", "token_sequence", "revoked"})
    {
        EXPECT_EQ(
            loadError(jv),
            "Key file '" + keyFile_.string() + "' is missing \"" + field + "\" field");
        jv[field] = "dummy";
    }
    EXPECT_EQ(loadError(jv), invalidField("key_type"));

    auto const kp = generateKeyPair(KeyType::Ed25519, randomSeed());
    jv["key_type"] = "ed25519";
    EXPECT_EQ(loadError(jv), invalidField("token_sequence"));
    jv["token_sequence"] = -1;
    EXPECT_EQ(loadError(jv), invalidField("token_sequence"));
    jv["token_sequence"] = true;
    EXPECT_EQ(loadError(jv), invalidField("token_sequence"));
    jv["token_sequence"] = json::UInt(kMaxSequence);
    EXPECT_EQ(loadError(jv), invalidField("revoked"));
    jv["revoked"] = false;
    EXPECT_EQ(loadError(jv), invalidField("secret_key"));
    jv["secret_key"] = toBase58(TokenType::NodePrivate, kp.second);
    EXPECT_EQ(loadError(jv), "");

    // Optional fields with the wrong type or value
    for (auto const* field :
         {"domain", "manifest", "pending_token_secret", "pending_signing_key", "pending_key_type"})
    {
        auto bad = baseKeyFile(kp.second);
        bad[field] = 1;
        if (field == std::string("pending_key_type"))
        {
            bad["pending_token_secret"] = toBase58(TokenType::NodePrivate, kp.second);
        }
        else if (std::string(field).starts_with("pending_"))
        {
            bad["pending_key_type"] = "ed25519";
        }
        EXPECT_EQ(loadError(bad), invalidField(field)) << field;
    }
    for (auto const* field : {"manifest", "pending_token_secret", "pending_signing_key"})
    {
        auto bad = baseKeyFile(kp.second);
        bad[field] = "not valid";
        bad["pending_key_type"] = "ed25519";
        EXPECT_EQ(loadError(bad), invalidField(field)) << field;
    }
    {
        auto bad = baseKeyFile(kp.second);
        bad["manifest"] = "";
        EXPECT_EQ(loadError(bad), invalidField("manifest"));
    }
    {
        auto bad = baseKeyFile(kp.second);
        bad["domain"] = "-bad.example";
        EXPECT_EQ(
            loadError(bad), "The domain field must use the '[host.][subdomain.]domain.tld' format");
    }
    {
        // Pending fields need a key type and exclude each other
        auto bad = baseKeyFile(kp.second);
        bad["pending_token_secret"] = toBase58(TokenType::NodePrivate, kp.second);
        EXPECT_EQ(
            loadError(bad),
            "Key file '" + keyFile_.string() + "' is missing \"pending_key_type\" field");
        bad["pending_key_type"] = "dummy";
        EXPECT_EQ(loadError(bad), invalidField("pending_key_type"));
        bad["pending_key_type"] = "ed25519";
        bad["pending_signing_key"] = toBase58(TokenType::NodePublic, kp.first);
        EXPECT_EQ(
            loadError(bad),
            "Key file '" + keyFile_.string() +
                "' has both \"pending_token_secret\" and \"pending_signing_key\"");
    }
}

TEST_F(SigningKeysTest, external_key_file_fields)
{
    auto const kp = generateKeyPair(KeyType::Ed25519, randomSeed());
    json::Value jv;
    jv["key_type"] = "ed25519";
    jv["secret_key"] = "external";
    jv["token_sequence"] = 0;
    jv["revoked"] = false;
    EXPECT_EQ(
        loadError(jv), "Key file '" + keyFile_.string() + "' is missing \"public_key\" field");
    jv["public_key"] = "dummy public";
    EXPECT_EQ(loadError(jv), invalidField("public_key"));
    jv["public_key"] = toBase58(TokenType::NodePublic, kp.first);
    jv["key_type"] = "secp256k1";
    EXPECT_EQ(
        loadError(jv),
        "Key file '" + keyFile_.string() +
            "' has a \"key_type\" that does not match \"public_key\"");
    jv["key_type"] = "ed25519";
    EXPECT_EQ(loadError(jv), "");

    auto const keys = SigningKeys::makeSigningKeys(keyFile_);
    EXPECT_FALSE(keys.hasSecret());
    EXPECT_EQ(keys.publicKey(), kp.first);
    EXPECT_TRUE(keys == SigningKeys(KeyType::Ed25519, kp.first));
}

TEST_F(SigningKeysTest, create_token)
{
    for (auto const keyType : kKeyTypes)
    {
        SigningKeys keys(keyType);
        std::uint32_t sequence = 0;
        for (auto const tokenKeyType : kKeyTypes)
        {
            auto const token = keys.createToken(tokenKeyType);
            auto const m = manifestOf(token.manifest, keys);
            EXPECT_EQ(m.sequence, ++sequence);
            EXPECT_EQ(keys.sequence(), sequence);
            ASSERT_TRUE(m.signingKey);
            EXPECT_EQ(
                required(m.signingKey), derivePublicKey(tokenKeyType, token.validationSecret));
            EXPECT_EQ(base64Encode(keys.manifest().data(), keys.manifest().size()), token.manifest);
        }
    }

    auto const kp = generateKeyPair(KeyType::Ed25519, randomSeed());
    {
        SigningKeys keys(KeyType::Ed25519, kp.second, kMaxSequence - 1);
        EXPECT_EQ(errorOf([&] { keys.createToken(); }), kExhausted);
    }
    {
        // A key migrated from a publisher whose manifests carried the list
        // sequence continues from that sequence.
        SigningKeys keys(KeyType::Ed25519, kp.second, 2026091301);
        auto const m = manifestOf(keys.createToken(KeyType::Ed25519).manifest, keys);
        EXPECT_EQ(m.sequence, 2026091302u);
    }
    {
        SigningKeys keys(KeyType::Ed25519);
        keys.revoke();
        EXPECT_EQ(errorOf([&] { keys.createToken(); }), kRevoked);
    }
    {
        SigningKeys keys(KeyType::Ed25519, kp.first);
        EXPECT_EQ(
            errorOf([&] { keys.createToken(); }), "This key file cannot be used to sign tokens.");
        EXPECT_EQ(errorOf([&] { keys.revoke(); }), "This key file cannot be used to sign tokens.");
    }
}

TEST_F(SigningKeysTest, token_with_domain)
{
    SigningKeys keys(KeyType::Ed25519);
    keys.domain("validator.example.com");
    auto const m = manifestOf(keys.createToken().manifest, keys);
    EXPECT_EQ(m.domain, "validator.example.com");
    EXPECT_EQ(
        keys.attestationData(),
        "[domain-attestation-blob:validator.example.com:" +
            toBase58(TokenType::NodePublic, keys.publicKey()) + "]");

    for (auto const* bad : {"a.b", "-bad.example", "nodots"})
    {
        EXPECT_EQ(
            errorOf([&] { keys.domain(bad); }),
            "The domain field must use the '[host.][subdomain.]domain.tld' format")
            << bad;
    }
    keys.domain("");
    EXPECT_TRUE(keys.domain().empty());
}

TEST_F(SigningKeysTest, revoke)
{
    for (auto const keyType : kKeyTypes)
    {
        SigningKeys keys(keyType);
        auto const m = manifestOf(keys.revoke(), keys);
        EXPECT_TRUE(m.revoked());
        EXPECT_FALSE(m.signingKey);
        EXPECT_TRUE(keys.revoked());
        // Revoking again is allowed
        manifestOf(keys.revoke(), keys);
    }
}

TEST_F(SigningKeysTest, sign)
{
    std::map<KeyType, std::string> const expected{
        {KeyType::Ed25519,
         "2EE541D6825791BF5454C571D2B363EAB3F01C73159B1F"
         "237AC6D38663A82B9D5EAD262D5F776B916E68247A1F082090F3BAE7ABC939"
         "C8F29B0DC759FD712300"},
        {KeyType::Secp256k1,
         "3045022100F142C27BF83D8D4541C7A4E759DE64A672"
         "51A388A422DFDA6F4B470A2113ABC4022002DA56695F3A805F62B55E7CC8D5"
         "55438D64A229CD0B4BA2AE33402443B20409"}};

    std::string const data = "data to sign";
    for (auto const keyType : kKeyTypes)
    {
        auto const sk = generateSecretKey(keyType, generateSeed("test"));
        SigningKeys const keys(keyType, sk, 1);
        EXPECT_EQ(keys.sign(data), expected.at(keyType));
        EXPECT_EQ(keys.signHex(strHex(data)), expected.at(keyType));
        auto const sig = required(strUnHex(keys.sign(data)));
        EXPECT_TRUE(verify(keys.publicKey(), makeSlice(data), makeSlice(sig)));

        SigningKeys const external(keyType, derivePublicKey(keyType, sk));
        EXPECT_EQ(
            errorOf([&] { (void)external.sign(data); }), "This key file cannot be used to sign.");
        EXPECT_EQ(
            errorOf([&] { (void)external.signHex(strHex(data)); }),
            "This key file cannot be used to sign.");
    }
    SigningKeys const keys(KeyType::Ed25519);
    EXPECT_EQ(errorOf([&] { (void)keys.signHex("zz"); }), "Could not decode hex string: zz");
}

TEST_F(SigningKeysTest, external_master)
{
    for (auto const keyType : kKeyTypes)
    {
        // The signer stands in for the hardware holding the master key
        SigningKeys const signer(keyType);
        SigningKeys keys(keyType, signer.publicKey());
        std::uint32_t sequence = 0;

        for (auto const tokenKeyType : kKeyTypes)
        {
            auto const data = keys.startToken(tokenKeyType);
            keys.writeToFile(keyFile_);
            auto fileKeys = SigningKeys::makeSigningKeys(keyFile_);
            EXPECT_TRUE(keys == fileKeys);

            auto const finished = fileKeys.finishToken(required(strUnHex(signer.signHex(data))));
            ASSERT_TRUE(finished.secret);
            auto const m = manifestOf(finished.manifest, keys);
            EXPECT_EQ(m.sequence, ++sequence);
            EXPECT_EQ(fileKeys.sequence(), sequence);
            EXPECT_EQ(
                required(m.signingKey), derivePublicKey(tokenKeyType, required(finished.secret)));

            // A signature over other bytes does not finish the token
            EXPECT_EQ(
                errorOf([&] { keys.finishToken(required(strUnHex(signer.sign("foo")))); }),
                kBadManifest);
            EXPECT_EQ(
                errorOf(
                    [&] { keys.finishToken(required(strUnHex(signer.signHex(data))), Blob{}); }),
                "The pending token's signing key is in this key file; pass one signature");
            keys.finishToken(required(strUnHex(signer.signHex(data))));
        }

        // Nothing pending
        EXPECT_EQ(errorOf([&] { keys.finishToken(Blob{}); }), "No pending token to finish");

        // Revocation: the same bytes each time, so a signature can be kept and reused
        auto const revocation = keys.startRevoke();
        EXPECT_EQ(revocation, keys.startRevoke());
        EXPECT_EQ(
            errorOf([&] { keys.finishRevoke(required(strUnHex(signer.sign("foo")))); }),
            kBadManifest);
        EXPECT_FALSE(keys.revoked());
        auto const sig = required(strUnHex(signer.signHex(revocation)));
        manifestOf(keys.finishRevoke(sig), keys);
        EXPECT_TRUE(keys.revoked());
        manifestOf(keys.finishRevoke(sig), keys);

        EXPECT_EQ(errorOf([&] { keys.startToken(); }), kRevoked);
        EXPECT_EQ(errorOf([&] { keys.finishToken(sig); }), kRevoked);
    }

    SigningKeys exhausted(
        KeyType::Ed25519, SigningKeys(KeyType::Ed25519).publicKey(), kMaxSequence - 1);
    EXPECT_EQ(errorOf([&] { exhausted.startToken(); }), kExhausted);
}

TEST_F(SigningKeysTest, external_signing_key)
{
    // The master key is in software here; the signing key is held elsewhere.
    SigningKeys const signer(KeyType::Ed25519);
    SigningKeys keys(KeyType::Ed25519);

    EXPECT_EQ(
        errorOf([&] { keys.startToken(KeyType::Ed25519, keys.publicKey()); }),
        "The signing key must differ from the master key");

    auto const data = keys.startToken(KeyType::Ed25519, signer.publicKey());
    keys.writeToFile(keyFile_);
    auto fileKeys = SigningKeys::makeSigningKeys(keyFile_);
    EXPECT_TRUE(keys == fileKeys);

    auto const masterSig = required(strUnHex(keys.signHex(data)));
    auto const signingSig = required(strUnHex(signer.signHex(data)));
    EXPECT_EQ(
        errorOf([&] { fileKeys.finishToken(masterSig); }),
        "The pending token's signing key is external; pass its signature too");
    EXPECT_EQ(errorOf([&] { fileKeys.finishToken(masterSig, masterSig); }), kBadManifest);

    auto const finished = fileKeys.finishToken(masterSig, signingSig);
    EXPECT_FALSE(finished.secret);
    auto const m = manifestOf(finished.manifest, keys);
    EXPECT_EQ(m.sequence, 1u);
    EXPECT_EQ(required(m.signingKey), signer.publicKey());
    EXPECT_EQ(fileKeys.sequence(), 1u);
    EXPECT_EQ(
        errorOf([&] { fileKeys.finishToken(masterSig, signingSig); }),
        "No pending token to finish");
}

TEST_F(SigningKeysTest, stored_manifest_is_checked)
{
    SigningKeys keys(KeyType::Ed25519);
    keys.createToken(KeyType::Ed25519);
    keys.writeToFile(keyFile_);
    auto const good = keyFileJson();
    EXPECT_EQ(loadError(good), "");

    // A token manifest on revoked keys
    auto jv = good;
    jv["revoked"] = true;
    EXPECT_EQ(loadError(jv), kBadManifest);

    // A revocation manifest on keys that are not revoked
    SigningKeys revoked(KeyType::Ed25519);
    revoked.revoke();
    jv = good;
    jv["manifest"] = strHex(makeSlice(revoked.manifest()));
    EXPECT_EQ(loadError(jv), kBadManifest);

    // A manifest of another key
    jv = good;
    jv["secret_key"] =
        toBase58(TokenType::NodePrivate, generateKeyPair(KeyType::Ed25519, randomSeed()).second);
    EXPECT_EQ(loadError(jv), kBadManifest);
}

}  // namespace xrpl::tools::test
