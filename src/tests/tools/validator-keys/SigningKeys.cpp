#include <tools/validator-keys/SigningKeys.h>

#include <xrpl/basics/Blob.h>
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
#include <sys/resource.h>
#include <tools/validator-keys/OwnerOnlyFile.h>

#include <Fixtures.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
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

// The order n of the secp256k1 group, big-endian.
constexpr std::array<std::uint8_t, 32> kSecp256k1Order{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41,
};

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

// The DER secp256k1 signature @p sig with S replaced by n - S: a signature
// that verifies over the same bytes but is not fully canonical.
Blob
withHighS(Blob const& sig)
{
    // 30 <len> 02 <rlen> <R> 02 <slen> <S>, with S at most 33 bytes
    std::size_t const rLen = sig.at(3);
    std::size_t const sAt = 4 + rLen;
    std::size_t const sLen = sig.at(sAt + 1);
    std::size_t const used = std::min<std::size_t>(sLen, 32);
    std::array<std::uint8_t, 32> s{};
    std::copy(sig.begin() + sAt + 2 + (sLen - used), sig.begin() + sAt + 2 + sLen, s.end() - used);

    Blob high(32);
    int borrow = 0;
    for (std::size_t i = 32; i-- > 0;)
    {
        int const d = kSecp256k1Order.at(i) - s.at(i) - borrow;
        borrow = d < 0 ? 1 : 0;
        high[i] = static_cast<std::uint8_t>(d + (borrow * 256));
    }
    // n - S is above n / 2, so its first byte is never zero; DER needs a zero
    // byte before a first byte with the sign bit set.
    if ((high.front() & 0x80) != 0)
        high.insert(high.begin(), 0);

    Blob out{0x30, 0, 0x02, static_cast<std::uint8_t>(rLen)};
    out.insert(out.end(), sig.begin() + 4, sig.begin() + sAt);
    out.push_back(0x02);
    out.push_back(static_cast<std::uint8_t>(high.size()));
    out.insert(out.end(), high.begin(), high.end());
    out[1] = static_cast<std::uint8_t>(out.size() - 2);
    return out;
}

}  // namespace

TEST_F(SigningKeysTest, equality_compares_every_field)
{
    auto const expectUnequal = [](SigningKeys const& a, SigningKeys const& b, char const* what) {
        EXPECT_FALSE(a == b) << what;
        EXPECT_FALSE(b == a) << what;
    };

    auto const ed = generateKeyPair(KeyType::Ed25519, randomSeed());
    SigningKeys const base(KeyType::Ed25519, ed.first);

    expectUnequal(SigningKeys(KeyType::Secp256k1, ed.first), base, "key type");
    expectUnequal(
        SigningKeys(KeyType::Ed25519, generateKeyPair(KeyType::Ed25519, randomSeed()).first),
        base,
        "public key");
    expectUnequal(SigningKeys(KeyType::Ed25519, ed.second, 0), base, "secret presence");
    expectUnequal(SigningKeys(KeyType::Ed25519, ed.first, 1), base, "token sequence");
    expectUnequal(SigningKeys(KeyType::Ed25519, ed.first, 0, true), base, "revoked");

    SigningKeys withDomain = base;
    withDomain.domain("validator.example.com");
    expectUnequal(withDomain, base, "domain");

    // The same key file with and without its manifest
    SigningKeys withManifest(KeyType::Ed25519);
    withManifest.createToken(KeyType::Ed25519);
    withManifest.writeToFile(keyFile_);
    auto withoutManifest = keyFileJson();
    withoutManifest.removeMember("manifest");
    auto const withoutManifestFile = dir_.file("without_manifest.json");
    writeFile(withoutManifestFile, withoutManifest.toStyledString());
    expectUnequal(
        SigningKeys::makeSigningKeys(keyFile_),
        SigningKeys::makeSigningKeys(withoutManifestFile),
        "manifest");

    SigningKeys const signerA(KeyType::Ed25519);
    SigningKeys const signerB(KeyType::Ed25519);
    SigningKeys pendingA = base;
    pendingA.startToken(KeyType::Ed25519, signerA.publicKey());
    expectUnequal(pendingA, base, "pending presence");

    SigningKeys pendingB = base;
    pendingB.startToken(KeyType::Ed25519, signerB.publicKey());
    expectUnequal(pendingA, pendingB, "pending signing key");

    // A pending generated key, and an external pending key with its public key
    SigningKeys generated = base;
    generated.startToken(KeyType::Secp256k1);
    generated.writeToFile(keyFile_);
    auto const secret = required(
        parseBase58<SecretKey>(
            TokenType::NodePrivate, keyFileJson()["pending_token_secret"].asString()));
    SigningKeys external = base;
    external.startToken(KeyType::Secp256k1, derivePublicKey(KeyType::Secp256k1, secret));
    expectUnequal(generated, external, "pending generated against external");

    // The same key file with and without its previous signing keys
    SigningKeys const master(KeyType::Ed25519);
    SigningKeys const first(KeyType::Ed25519);
    SigningKeys const second(KeyType::Ed25519);
    SigningKeys rotated(KeyType::Ed25519, master.publicKey());
    for (auto const* signer : {&first, &second})
    {
        auto const data = rotated.startToken(KeyType::Ed25519, signer->publicKey());
        rotated.finishToken(
            required(strUnHex(master.signHex(data))), required(strUnHex(signer->signHex(data))));
    }
    rotated.writeToFile(keyFile_);
    auto withoutPrevious = keyFileJson();
    withoutPrevious.removeMember("previous_signing_keys");
    auto const withoutPreviousFile = dir_.file("without_previous.json");
    writeFile(withoutPreviousFile, withoutPrevious.toStyledString());
    expectUnequal(
        SigningKeys::makeSigningKeys(keyFile_),
        SigningKeys::makeSigningKeys(withoutPreviousFile),
        "previous signing keys");
}

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
    keys.createToken(KeyType::Secp256k1);
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
    // Every directory made on the way is owner-only, not just the last one
    for (auto const* made : {"a", "a/b", "a/b/c"})
    {
        EXPECT_EQ(
            std::filesystem::status(dir_.file(made)).permissions() &
                (std::filesystem::perms::group_all | std::filesystem::perms::others_all),
            std::filesystem::perms::none)
            << made;
    }

    // The parent path is a file
    auto const blocked = std::filesystem::path(keyFile_.string() + "/keys.json");
    keys.writeToFile(keyFile_);
    EXPECT_EQ(
        errorOf([&] { keys.writeToFile(blocked); }),
        "Cannot create directory: " + blocked.parent_path().string());

    // A directory below the file: mkdir fails with ENOTDIR, as root too
    auto const underFile = std::filesystem::path(keyFile_.string() + "/sub/keys.json");
    EXPECT_EQ(
        errorOf([&] { keys.writeToFile(underFile); }),
        "Cannot create directory: " + underFile.parent_path().string());

    // The target is a directory
    auto const directory = std::filesystem::path(dir_.file("dir"));
    std::filesystem::create_directory(directory);
    EXPECT_EQ(
        errorOf([&] { keys.writeToFile(directory); }),
        "Cannot write key file: " + directory.string());

    // The directory cannot be written to; skipped as root, which writes into
    // a 0500 directory regardless of its mode.
    if (::geteuid() != 0)
    {
        auto const sealed = std::filesystem::path(dir_.file("sealed"));
        std::filesystem::create_directory(sealed);
        std::filesystem::permissions(
            sealed, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
        auto const inSealed = sealed / "keys.json";
        EXPECT_EQ(
            errorOf([&] { keys.writeToFile(inSealed); }),
            "Cannot write key file: " + inSealed.string());
        std::filesystem::permissions(sealed, std::filesystem::perms::owner_all);
    }

    // A stale file at the old fixed ".tmp" sibling name is neither followed
    // nor removed: the temporary now gets a random suffix in the same
    // directory, so that name plays no part in the write.
    auto const linked = std::filesystem::path(dir_.file("linked.json"));
    auto const stale = linked.string() + ".tmp";
    std::filesystem::create_symlink(dir_.file("elsewhere.json"), stale);
    keys.writeToFile(linked);
    EXPECT_TRUE(keys == SigningKeys::makeSigningKeys(linked));
    EXPECT_TRUE(std::filesystem::is_symlink(stale));
}

TEST_F(SigningKeysTest, owner_only_file_edges)
{
    // A one-byte write commits
    {
        OwnerOnlyFile file(keyFile_, "key file");
        file.write("x");
        EXPECT_EQ(errorOf([&] { file.commit(); }), "");
    }
    EXPECT_EQ(readFile(keyFile_), "x");

    // Each missing directory on the way is made with mode 0700
    SigningKeys const keys(KeyType::Ed25519);
    auto const nested = std::filesystem::path(dir_.file("a/b/key.json"));
    keys.writeToFile(nested);
    EXPECT_TRUE(keys == SigningKeys::makeSigningKeys(nested));
    for (auto const* made : {"a", "a/b"})
    {
        EXPECT_EQ(
            std::filesystem::status(dir_.file(made)).permissions() & std::filesystem::perms::mask,
            std::filesystem::perms::owner_all)
            << made;
    }

    // A relative path whose first mkdir succeeds with errno 0 left from before
    {
        WorkingDirectory const cwd(dir_.path());
        errno = 0;
        EXPECT_EQ(errorOf([&] { keys.writeToFile("c/key.json"); }), "");
    }
    EXPECT_TRUE(keys == SigningKeys::makeSigningKeys(dir_.file("c/key.json")));
}

TEST_F(SigningKeysTest, create_refuses_existing)
{
    SigningKeys const first(KeyType::Ed25519);
    SigningKeys const second(KeyType::Ed25519);

    // A second creator that passed the same missing-file state commits after the first
    OwnerOnlyFile late(keyFile_, "key file", OwnerOnlyFile::Existing::Refuse);
    late.write("{}");
    first.writeToFile(keyFile_, OwnerOnlyFile::Existing::Refuse);
    EXPECT_EQ(
        errorOf([&] { late.commit(); }),
        "Refusing to overwrite existing key file: " + keyFile_.string());
    EXPECT_TRUE(first == SigningKeys::makeSigningKeys(keyFile_));

    EXPECT_EQ(
        errorOf([&] { second.writeToFile(keyFile_, OwnerOnlyFile::Existing::Refuse); }),
        "Refusing to overwrite existing key file: " + keyFile_.string());
    EXPECT_TRUE(first == SigningKeys::makeSigningKeys(keyFile_));

    // Only the target is left in the directory: no temporary survives either path
    std::size_t entries = 0;
    for ([[maybe_unused]] auto const& entry :
         std::filesystem::directory_iterator(keyFile_.parent_path()))
        ++entries;
    EXPECT_EQ(entries, 1u);
}

TEST_F(SigningKeysTest, write_past_file_size_limit)
{
    OwnerOnlyFile file(keyFile_, "key file");

    // A file size limit of 0 with SIGXFSZ ignored makes write fail with EFBIG;
    // both are restored before any expectation.
    auto const previousHandler = ::signal(SIGXFSZ, SIG_IGN);
    bool const ignored = previousHandler != SIG_ERR;
    struct rlimit previousLimit = {};
    bool const gotLimit = ::getrlimit(RLIMIT_FSIZE, &previousLimit) == 0;
    struct rlimit zero = previousLimit;
    zero.rlim_cur = 0;
    bool const limited = gotLimit && ::setrlimit(RLIMIT_FSIZE, &zero) == 0;
    file.write("{}");
    bool const restored = !limited || ::setrlimit(RLIMIT_FSIZE, &previousLimit) == 0;
    if (ignored)
        ::signal(SIGXFSZ, previousHandler);

    ASSERT_TRUE(ignored);
    ASSERT_TRUE(limited);
    ASSERT_TRUE(restored);
    EXPECT_EQ(errorOf([&] { file.commit(); }), "Cannot write key file: " + keyFile_.string());

    // Neither the target nor the temporary is left in the directory
    std::size_t entries = 0;
    for ([[maybe_unused]] auto const& entry :
         std::filesystem::directory_iterator(keyFile_.parent_path()))
        ++entries;
    EXPECT_EQ(entries, 0u);
}

TEST_F(SigningKeysTest, commit_in_unreadable_directory)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "root reads a directory of any mode";

    // A directory that can be written and searched but not read: the commit
    // succeeds although the directory cannot be opened to sync it.
    auto const unreadable = std::filesystem::path(dir_.file("unreadable"));
    std::filesystem::create_directory(unreadable);
    std::filesystem::permissions(
        unreadable, std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec);
    auto const target = unreadable / "validator_keys.json";
    std::string error;
    {
        OwnerOnlyFile file(target, "key file");
        file.write("{}");
        error = errorOf([&] { file.commit(); });
    }
    std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all);
    std::string const text = readFile(target);

    EXPECT_EQ(error, "");
    EXPECT_EQ(text, "{}");
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

    // JSON that is not an object
    writeFile(keyFile_, "[]");
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
        EXPECT_EQ(errorOf([&] { keys.createToken(KeyType::Secp256k1); }), kExhausted);
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
        EXPECT_EQ(errorOf([&] { keys.createToken(KeyType::Secp256k1); }), kRevoked);
    }
    {
        SigningKeys keys(KeyType::Ed25519, kp.first);
        EXPECT_EQ(
            errorOf([&] { keys.createToken(KeyType::Secp256k1); }),
            "This key file cannot be used to sign tokens.");
        EXPECT_EQ(
            errorOf([&] { keys.revoke(); }),
            "This key file cannot sign a revocation: the master key is external; use "
            "start_revoke_keys and finish_revoke_keys.");
    }
}

TEST_F(SigningKeysTest, token_with_domain)
{
    SigningKeys keys(KeyType::Ed25519);
    keys.domain("validator.example.com");
    auto const m = manifestOf(keys.createToken(KeyType::Secp256k1).manifest, keys);
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
        {
            KeyType::Ed25519,
            "2EE541D6825791BF5454C571D2B363EAB3F01C73159B1F"
            "237AC6D38663A82B9D5EAD262D5F776B916E68247A1F082090F3BAE7ABC939"
            "C8F29B0DC759FD712300",
        },
        {
            KeyType::Secp256k1,
            "3045022100F142C27BF83D8D4541C7A4E759DE64A672"
            "51A388A422DFDA6F4B470A2113ABC4022002DA56695F3A805F62B55E7CC8D5"
            "55438D64A229CD0B4BA2AE33402443B20409",
        },
    };

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
    EXPECT_EQ(
        errorOf([&] { (void)keys.signHex(" "); }),
        "Syntax error: Must specify data string to sign");
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

        EXPECT_EQ(errorOf([&] { keys.startToken(KeyType::Secp256k1); }), kRevoked);
        EXPECT_EQ(errorOf([&] { keys.finishToken(sig); }), kRevoked);
    }

    SigningKeys exhausted(
        KeyType::Ed25519, SigningKeys(KeyType::Ed25519).publicKey(), kMaxSequence - 1);
    EXPECT_EQ(errorOf([&] { exhausted.startToken(KeyType::Secp256k1); }), kExhausted);
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

    // The signing key the current manifest names is refused for the next
    // token: ManifestCache::applyManifest returns BadEphemeralKey for it
    EXPECT_EQ(
        errorOf([&] { fileKeys.startToken(KeyType::Ed25519, signer.publicKey()); }),
        "The signing key is the current manifest's; a server rejects a manifest that reuses it");
    EXPECT_EQ(fileKeys.sequence(), 1u);
    SigningKeys const next(KeyType::Ed25519);
    EXPECT_EQ(errorOf([&] { fileKeys.startToken(KeyType::Ed25519, next.publicKey()); }), "");
}

TEST_F(SigningKeysTest, pending_token_is_kept)
{
    SigningKeys const signer(KeyType::Ed25519);
    SigningKeys keys(KeyType::Ed25519);
    std::string const pendingError =
        "A token is pending: finish it with finish_token before starting another";

    // A generated signing key: the same key type gets the same bytes, through
    // the key file too, and another request is refused
    auto const data = keys.startToken(KeyType::Secp256k1);
    EXPECT_EQ(keys.startToken(KeyType::Secp256k1), data);
    keys.writeToFile(keyFile_);
    auto fileKeys = SigningKeys::makeSigningKeys(keyFile_);
    EXPECT_EQ(fileKeys.startToken(KeyType::Secp256k1), data);
    EXPECT_TRUE(keys == fileKeys);
    EXPECT_EQ(errorOf([&] { keys.startToken(KeyType::Ed25519); }), pendingError);
    EXPECT_EQ(
        errorOf([&] { keys.startToken(KeyType::Secp256k1, signer.publicKey()); }), pendingError);
    EXPECT_TRUE(keys == fileKeys);
    auto const finished = keys.finishToken(required(strUnHex(keys.signHex(data))));
    auto const m = manifestOf(finished.manifest, keys);
    EXPECT_EQ(m.sequence, 1u);
    EXPECT_EQ(
        required(m.signingKey), derivePublicKey(KeyType::Secp256k1, required(finished.secret)));

    // An external signing key: the same key gets the same bytes, whatever key
    // type the call names; another key or a generated key is refused
    auto const external = keys.startToken(KeyType::Ed25519, signer.publicKey());
    EXPECT_EQ(keys.startToken(KeyType::Secp256k1, signer.publicKey()), external);
    EXPECT_EQ(errorOf([&] { keys.startToken(KeyType::Ed25519); }), pendingError);
    EXPECT_EQ(
        errorOf(
            [&] { keys.startToken(KeyType::Ed25519, SigningKeys(KeyType::Ed25519).publicKey()); }),
        pendingError);
    // createToken would replace the pending token too, so it is refused
    EXPECT_EQ(errorOf([&] { keys.createToken(KeyType::Secp256k1); }), pendingError);
    EXPECT_EQ(keys.startToken(KeyType::Ed25519, signer.publicKey()), external);
    auto const masterSig = required(strUnHex(keys.signHex(external)));
    auto const signingSig = required(strUnHex(signer.signHex(external)));
    EXPECT_EQ(manifestOf(keys.finishToken(masterSig, signingSig).manifest, keys).sequence, 2u);
}

TEST_F(SigningKeysTest, earlier_signing_key_refused)
{
    // The master and both signing keys are held outside the key file
    SigningKeys const master(KeyType::Ed25519);
    SigningKeys const a(KeyType::Ed25519);
    SigningKeys const b(KeyType::Ed25519);
    SigningKeys keys(KeyType::Ed25519, master.publicKey());
    std::string const earlierKey =
        "The signing key was used by an earlier manifest; a rotated-out key is not used again";

    for (auto const* signer : {&a, &b})
    {
        auto const data = keys.startToken(KeyType::Ed25519, signer->publicKey());
        keys.finishToken(
            required(strUnHex(master.signHex(data))), required(strUnHex(signer->signHex(data))));
    }
    EXPECT_EQ(keys.sequence(), 2u);

    EXPECT_EQ(errorOf([&] { keys.startToken(KeyType::Secp256k1, a.publicKey()); }), earlierKey);
    EXPECT_EQ(keys.sequence(), 2u);

    // The key file keeps the earlier signing keys
    keys.writeToFile(keyFile_);
    auto const jv = keyFileJson();
    ASSERT_TRUE(jv["previous_signing_keys"].isArray());
    ASSERT_EQ(jv["previous_signing_keys"].size(), 1u);
    EXPECT_EQ(
        jv["previous_signing_keys"][0u].asString(), toBase58(TokenType::NodePublic, a.publicKey()));
    auto fileKeys = SigningKeys::makeSigningKeys(keyFile_);
    EXPECT_TRUE(keys == fileKeys);
    EXPECT_EQ(errorOf([&] { fileKeys.startToken(KeyType::Secp256k1, a.publicKey()); }), earlierKey);

    // A previous_signing_keys that is not an array of NodePublic keys is refused
    std::string const invalidPrevious =
        "Key file '" + keyFile_.string() + "' contains invalid \"previous_signing_keys\" field";
    json::Value notKey(json::ValueType::Array);
    notKey.append("notakey");
    json::Value notString(json::ValueType::Array);
    notString.append(5);
    for (auto const& bad : {json::Value(5), notString, notKey})
    {
        auto badFile = jv;
        badFile["previous_signing_keys"] = bad;
        writeKeyFile(badFile);
        EXPECT_EQ(errorOf([&] { SigningKeys::makeSigningKeys(keyFile_); }), invalidPrevious);
    }

    // A key file without previous_signing_keys loads
    auto withoutField = jv;
    withoutField.removeMember("previous_signing_keys");
    writeKeyFile(withoutField);
    EXPECT_EQ(errorOf([&] { SigningKeys::makeSigningKeys(keyFile_); }), "");
    EXPECT_EQ(SigningKeys::makeSigningKeys(keyFile_).sequence(), 2u);
}

TEST_F(SigningKeysTest, stored_manifest_is_checked)
{
    SigningKeys keys(KeyType::Ed25519);
    keys.createToken(KeyType::Ed25519);
    keys.writeToFile(keyFile_);
    auto const good = keyFileJson();
    EXPECT_EQ(loadError(good), "");

    // Hex that is not a manifest
    auto jv = good;
    jv["manifest"] = "00";
    EXPECT_EQ(loadError(jv), kBadManifest);

    // A token manifest on revoked keys
    jv = good;
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

    // A manifest above token_sequence loads, so the keys can still be revoked,
    // but makes no token: the next one would not be newer than it
    jv = good;
    jv["token_sequence"] = 0;
    EXPECT_EQ(loadError(jv), "");
    {
        auto loaded = SigningKeys::makeSigningKeys(keyFile_);
        std::string const stale =
            "The stored manifest has sequence 1, above token_sequence 0; a server would reject "
            "the next token as stale";
        EXPECT_EQ(errorOf([&] { loaded.createToken(KeyType::Secp256k1); }), stale);
        EXPECT_EQ(errorOf([&] { loaded.startToken(KeyType::Secp256k1); }), stale);
        EXPECT_EQ(loaded.sequence(), 0u);
        EXPECT_TRUE(manifestOf(loaded.revoke(), loaded).revoked());
    }

    // A token_sequence above the manifest, as a migrated key file sets it,
    // continues from there
    jv = good;
    jv["token_sequence"] = 5;
    EXPECT_EQ(loadError(jv), "");
    {
        auto loaded = SigningKeys::makeSigningKeys(keyFile_);
        EXPECT_EQ(manifestOf(loaded.createToken(KeyType::Secp256k1).manifest, loaded).sequence, 6u);
    }
}

TEST_F(SigningKeysTest, key_file_too_large)
{
    SigningKeys const keys(KeyType::Ed25519);
    keys.writeToFile(keyFile_);
    auto text = readFile(keyFile_);
    ASSERT_LT(text.size(), 65536u);

    // Trailing whitespace keeps the JSON valid, so only the size is refused
    text.resize(65536, ' ');
    writeFile(keyFile_, text);
    EXPECT_EQ(errorOf([&] { SigningKeys::makeSigningKeys(keyFile_); }), "");

    text.push_back(' ');
    writeFile(keyFile_, text);
    EXPECT_EQ(
        errorOf([&] { SigningKeys::makeSigningKeys(keyFile_); }),
        "Failed to open key file: " + keyFile_.string());
}

TEST_F(SigningKeysTest, non_canonical_signature_refused)
{
    SigningKeys const signer(KeyType::Secp256k1);
    SigningKeys keys(KeyType::Secp256k1, signer.publicKey());
    auto const data = keys.startToken(KeyType::Secp256k1);
    auto const sig = required(strUnHex(signer.signHex(data)));
    auto const high = withHighS(sig);
    EXPECT_EQ(ecdsaCanonicality(makeSlice(high)), ECDSACanonicality::Canonical);

    EXPECT_EQ(errorOf([&] { keys.finishToken(high); }), kBadManifest);
    EXPECT_EQ(keys.sequence(), 0u);

    // The fully canonical signature it was made from finishes the token
    EXPECT_EQ(errorOf([&] { keys.finishToken(sig); }), "");
    EXPECT_EQ(keys.sequence(), 1u);
}

TEST_F(SigningKeysTest, revoke_discards_pending)
{
    SigningKeys const signer(KeyType::Ed25519);
    for (bool const externalSigningKey : {false, true})
    {
        SigningKeys keys(KeyType::Ed25519);
        if (externalSigningKey)
        {
            keys.startToken(KeyType::Ed25519, signer.publicKey());
        }
        else
        {
            keys.startToken(KeyType::Secp256k1);
        }
        keys.revoke();
        keys.writeToFile(keyFile_);

        auto const jv = keyFileJson();
        EXPECT_TRUE(jv["revoked"].asBool()) << externalSigningKey;
        EXPECT_FALSE(jv.isMember("pending_token_secret")) << externalSigningKey;
        EXPECT_FALSE(jv.isMember("pending_signing_key")) << externalSigningKey;
    }
}

}  // namespace xrpl::tools::test
