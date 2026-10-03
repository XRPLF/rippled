#include <tools/validator-keys/Commands.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/Manifest.h>

#include <gtest/gtest.h>
#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>

#include <Fixtures.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace xrpl::tools::test {

namespace {

std::string const kArgError = "Syntax error: Wrong number of arguments";
std::string const kRevokedOperation = "Operation error: The specified master key has been revoked!";
std::string const kExhausted =
    "Maximum number of tokens have already been generated.\n"
    "Revoke validator keys if previous token has been compromised.";

class CommandsTest : public ::testing::Test
{
protected:
    TempDir dir_;
    ToolOptions options_ = optionsFor(dir_.file("validator_keys.json"));

    [[nodiscard]] std::filesystem::path
    file(std::string const& name) const
    {
        return dir_.file(name);
    }

    [[nodiscard]] static std::string
    commandError(
        std::string const& command,
        std::vector<std::string> const& args,
        ToolOptions const& opts)
    {
        return errorOf([&] { run(command, args, opts); });
    }

    [[nodiscard]] static SigningKeys
    keys(ToolOptions const& opts)
    {
        return SigningKeys::makeSigningKeys(opts.keyFile);
    }

    // The token base64 inside a [validator_token] or [validator_manifest] block.
    static std::string
    blockBody(std::string const& text, std::string const& section)
    {
        auto pos = text.find("[" + section + "]\n");
        if (pos == std::string::npos)
        {
            ADD_FAILURE() << "no [" << section << "] block in: " << text;
            return {};
        }
        std::string body;
        for (auto rest = text.substr(pos + section.size() + 3); !rest.empty();)
        {
            auto const eol = rest.find('\n');
            auto const line = rest.substr(0, eol);
            if (line.empty())
                break;
            body += line;
            rest = eol == std::string::npos ? "" : rest.substr(eol + 1);
        }
        return body;
    }
};

}  // namespace

TEST_F(CommandsTest, dispatch)
{
    EXPECT_EQ(commandError("unknown", {}, options_), "Unknown command: unknown");
    EXPECT_EQ(commandError("create_keys", {"x"}, options_), kArgError);
    EXPECT_EQ(commandError("finish_token", {}, options_), kArgError);
    EXPECT_EQ(commandError("finish_token", {"a", "b", "c"}, options_), kArgError);
    EXPECT_EQ(commandError("finish_sign_list", {"a"}, options_), kArgError);
    EXPECT_FALSE(getVersionString().empty());
}

TEST_F(CommandsTest, create_keys)
{
    auto const r = run("create_keys", {}, options_);
    EXPECT_EQ(r.rc, EXIT_SUCCESS);
    EXPECT_NE(
        r.out.find("Validator keys stored in " + options_.keyFile.string()), std::string::npos);
    EXPECT_TRUE(keys(options_).hasSecret());
    EXPECT_EQ(
        commandError("create_keys", {}, options_),
        "Refusing to overwrite existing key file: " + options_.keyFile.string());
}

TEST_F(CommandsTest, create_external)
{
    SigningKeys const external(KeyType::Ed25519);
    auto const& key = external.publicKey();
    for (auto const& encoded :
         {toBase58(TokenType::NodePublic, key), strHex(key), base64Encode(key.data(), key.size())})
    {
        ToolOptions const opts =
            optionsFor(file("external-" + std::to_string(encoded.size()) + ".json"));
        EXPECT_EQ(run("create_external", {encoded}, opts).rc, EXIT_SUCCESS);
        auto const loaded = keys(opts);
        EXPECT_FALSE(loaded.hasSecret());
        EXPECT_EQ(loaded.publicKey(), key);
    }
    EXPECT_EQ(
        commandError("create_external", {"abcd"}, options_), "Unable to parse public key: abcd");
    auto badHex = strHex(key);
    badHex.insert(badHex.size() / 2, "n");
    EXPECT_EQ(
        commandError("create_external", {badHex}, options_),
        "Unable to parse public key: " + badHex);
    run("create_external", {strHex(key)}, options_);
    EXPECT_EQ(
        commandError("create_external", {strHex(key)}, options_),
        "Refusing to overwrite existing key file: " + options_.keyFile.string());
}

TEST_F(CommandsTest, create_token)
{
    EXPECT_EQ(
        commandError("create_token", {}, options_),
        "Failed to open key file: " + options_.keyFile.string());
    run("create_keys", {}, options_);

    auto const r = run("create_token", {}, options_);
    EXPECT_EQ(r.rc, EXIT_SUCCESS);
    auto const token = required(loadValidatorToken({blockBody(r.out, "validator_token")}));
    auto const m = required(deserializeManifest(base64Decode(token.manifest)));
    EXPECT_EQ(m.sequence, 1u);
    EXPECT_EQ(required(m.signingKey), derivePublicKey(KeyType::Secp256k1, token.validationSecret));

    // Written to a file readable by the owner only, as an ed25519 token
    ToolOptions toFile = options_;
    toFile.tokenKeyType = KeyType::Ed25519;
    toFile.outFile = file("token.txt");
    EXPECT_NE(run("create_token", {}, toFile).out.find("written to"), std::string::npos);
    auto const written = loadTokenFile(*toFile.outFile);
    auto const writtenManifest = required(deserializeManifest(base64Decode(written.manifest)));
    EXPECT_EQ(
        required(writtenManifest.signingKey),
        derivePublicKey(KeyType::Ed25519, written.validationSecret));
    EXPECT_EQ(
        std::filesystem::status(*toFile.outFile).permissions() &
            (std::filesystem::perms::group_all | std::filesystem::perms::others_all),
        std::filesystem::perms::none);
    EXPECT_EQ(keys(options_).sequence(), 2u);

    // A symlink is not written through
    ToolOptions linked = options_;
    linked.outFile = file("link.txt");
    std::filesystem::create_symlink(file("elsewhere.txt"), *linked.outFile);
    EXPECT_EQ(
        commandError("create_token", {}, linked),
        "Refusing to write through a symlink: " + linked.outFile->string());
    EXPECT_EQ(keys(options_).sequence(), 2u);

    // An unwritable output path fails before the sequence is consumed
    ToolOptions unwritable = options_;
    unwritable.outFile = file("missing/token.txt");
    EXPECT_EQ(
        commandError("create_token", {}, unwritable),
        "Cannot write output file: " + unwritable.outFile->string());
    EXPECT_EQ(keys(options_).sequence(), 2u);

    // The output may not replace the key file
    ToolOptions aliased = options_;
    aliased.outFile = options_.keyFile;
    EXPECT_EQ(
        commandError("create_token", {}, aliased),
        "--out names an input file: " + options_.keyFile.string());
    EXPECT_EQ(keys(options_).sequence(), 2u);

    {
        auto const kp = generateKeyPair(KeyType::Ed25519, randomSeed());
        SigningKeys(KeyType::Ed25519, kp.second, std::numeric_limits<std::uint32_t>::max() - 1)
            .writeToFile(options_.keyFile);
        EXPECT_EQ(commandError("create_token", {}, options_), kExhausted);
    }
    run("revoke_keys", {}, options_);
    EXPECT_EQ(commandError("create_token", {}, options_), "Validator keys have been revoked.");
}

TEST_F(CommandsTest, external_token)
{
    // The signer stands in for the hardware holding the master key
    SigningKeys const signer(KeyType::Ed25519);
    run("create_external", {toBase58(TokenType::NodePublic, signer.publicKey())}, options_);

    for (auto const encode : {0, 1})
    {
        auto const start = run("start_token", {}, options_);
        EXPECT_EQ(start.rc, EXIT_SUCCESS);
        auto const bytes = start.out.substr(0, start.out.find('\n'));
        auto const sig = signer.signHex(bytes);
        auto const sigBytes = required(strUnHex(sig));
        auto const encoded = (encode != 0) ? base64Encode(sigBytes.data(), sigBytes.size()) : sig;
        auto const finish = run("finish_token", {encoded}, options_);
        EXPECT_EQ(finish.rc, EXIT_SUCCESS);
        auto const token = required(loadValidatorToken({blockBody(finish.out, "validator_token")}));
        auto const m = required(deserializeManifest(base64Decode(token.manifest)));
        EXPECT_EQ(m.sequence, encode ? 2u : 1u);
    }
    EXPECT_EQ(
        commandError("finish_token", {"bad signature"}, options_), "Invalid signature encoding");
    run("start_token", {}, options_);
    EXPECT_EQ(
        commandError("finish_token", {signer.sign("foo")}, options_),
        "Manifest is not properly signed");
    EXPECT_EQ(keys(options_).sequence(), 2u);

    // An external signing key too: two signatures, a manifest without a secret
    SigningKeys const signingKey(KeyType::Ed25519);
    ToolOptions both = options_;
    both.signingKey = signingKey.publicKey();
    both.outFile = file("manifest.txt");
    auto const start = run("start_token", {}, both);
    auto const bytes = start.out.substr(0, start.out.find('\n'));
    EXPECT_EQ(
        commandError("finish_token", {signer.signHex(bytes)}, both),
        "The pending token's signing key is external; pass its signature too");
    auto const finish =
        run("finish_token", {signer.signHex(bytes), signingKey.signHex(bytes)}, both);
    EXPECT_EQ(finish.rc, EXIT_SUCCESS);
    auto const manifest = loadManifestFile(*both.outFile);
    EXPECT_EQ(required(manifest.signingKey), signingKey.publicKey());
    EXPECT_EQ(manifest.sequence, 3u);

    // The domain is stored for the next token; the attestation bytes are printed
    // for the external signer
    EXPECT_EQ(run("attest_domain", {}, options_).out.find("No attestation is necessary"), 0u);
    auto const domain = run("set_domain", {"validator.example.com"}, options_);
    EXPECT_NE(domain.out.find("run start_token and finish_token"), std::string::npos);
    EXPECT_EQ(keys(options_).domain(), "validator.example.com");
    auto const attest = run("attest_domain", {}, options_);
    EXPECT_EQ(
        attest.out.substr(0, attest.out.find('\n')),
        strHex(makeSlice(keys(options_).attestationData())));
    EXPECT_NE(attest.err.find("Sign these bytes"), std::string::npos);
    auto const next = run("start_token", {}, options_);
    auto const nextBytes = next.out.substr(0, next.out.find('\n'));
    auto const finished = run("finish_token", {signer.signHex(nextBytes)}, options_);
    auto const token = required(loadValidatorToken({blockBody(finished.out, "validator_token")}));
    auto const m = required(deserializeManifest(base64Decode(token.manifest)));
    EXPECT_EQ(m.domain, "validator.example.com");

    // Revocation in two steps
    auto const startRevoke = run("start_revoke_keys", {}, options_);
    EXPECT_NE(startRevoke.err.find("This will revoke"), std::string::npos);
    auto const revokeBytes = startRevoke.out.substr(0, startRevoke.out.find('\n'));
    EXPECT_EQ(
        commandError("finish_revoke_keys", {signer.sign("foo")}, options_),
        "Manifest is not properly signed");
    EXPECT_FALSE(keys(options_).revoked());
    auto const revoked = run("finish_revoke_keys", {signer.signHex(revokeBytes)}, options_);
    EXPECT_NE(revoked.out.find("[validator_key_revocation]"), std::string::npos);
    EXPECT_TRUE(keys(options_).revoked());
    EXPECT_NE(
        run("start_revoke_keys", {}, options_).err.find("already been revoked"), std::string::npos);
    EXPECT_EQ(commandError("start_token", {}, options_), "Validator keys have been revoked.");
    EXPECT_EQ(commandError("finish_token", {"00"}, options_), "Validator keys have been revoked.");
}

TEST_F(CommandsTest, revoke_keys)
{
    run("create_keys", {}, options_);
    auto const first = run("revoke_keys", {}, options_);
    EXPECT_NE(first.err.find("This will revoke"), std::string::npos);
    EXPECT_NE(first.out.find("[validator_key_revocation]"), std::string::npos);
    auto const again = run("revoke_keys", {}, options_);
    EXPECT_NE(again.err.find("already been revoked"), std::string::npos);
    EXPECT_EQ(commandError("set_domain", {"validator.example.com"}, options_), kRevokedOperation);
    EXPECT_EQ(commandError("attest_domain", {}, options_), kRevokedOperation);
    EXPECT_NE(run("sign", {"data"}, options_).err.find("have been revoked"), std::string::npos);
}

TEST_F(CommandsTest, domain)
{
    run("create_keys", {}, options_);
    EXPECT_NE(run("show_manifest", {"hex"}, options_).out.find("unavailable"), std::string::npos);
    EXPECT_NE(run("clear_domain", {}, options_).out.find("already cleared"), std::string::npos);

    auto const set = run("set_domain", {"validator.example.com"}, options_);
    EXPECT_NE(set.out.find("has been set to: validator.example.com"), std::string::npos);
    EXPECT_NE(set.out.find("attestation=\""), std::string::npos);
    EXPECT_NE(set.out.find("[validator_token]"), std::string::npos);
    EXPECT_NE(
        run("set_domain", {"validator.example.com"}, options_).out.find("already set"),
        std::string::npos);
    EXPECT_NE(run("attest_domain", {}, options_).out.find("attestation=\""), std::string::npos);
    EXPECT_NE(run("show_manifest", {"base64"}, options_).out.find("(Base64)"), std::string::npos);
    EXPECT_NE(run("show_manifest", {"hex"}, options_).out.find("(Hex)"), std::string::npos);
    EXPECT_EQ(commandError("show_manifest", {"other"}, options_), "Unknown encoding 'other'");
    EXPECT_NE(run("clear_domain", {}, options_).out.find("has been cleared"), std::string::npos);
    EXPECT_EQ(
        commandError("set_domain", {"-bad.example"}, options_),
        "The domain field must use the '[host.][subdomain.]domain.tld' format");

    auto const kp = generateKeyPair(KeyType::Ed25519, randomSeed());
    SigningKeys(KeyType::Ed25519, kp.second, std::numeric_limits<std::uint32_t>::max() - 1)
        .writeToFile(options_.keyFile);
    EXPECT_EQ(commandError("set_domain", {"other.example.com"}, options_), kExhausted);
}

TEST_F(CommandsTest, sign)
{
    run("create_keys", {}, options_);
    EXPECT_EQ(
        commandError("sign", {""}, options_), "Syntax error: Must specify data string to sign");
    auto const loaded = keys(options_);
    EXPECT_EQ(run("sign", {"data"}, options_).out, loaded.sign("data") + "\n");
    EXPECT_EQ(run("sign_hex", {"00FF"}, options_).out, loaded.signHex("00FF") + "\n");
}

TEST_F(CommandsTest, list_commands)
{
    // The publisher: a key file and a token carrying an ed25519 signing key
    ToolOptions publisher = optionsFor(file("publisher.json"));
    publisher.tokenKeyType = KeyType::Ed25519;
    publisher.tokenFile = file("publisher-token.txt");
    publisher.outFile = publisher.tokenFile;
    run("create_keys", {}, publisher);
    run("create_token", {}, publisher);
    publisher.outFile.reset();
    auto const master = keys(publisher).publicKey();

    auto const unsignedList = file("unsigned.json");
    auto const now = netClockNow();
    writeFile(unsignedList, unsignedListText(makeValidators(2), 2026091301, now + 3600));

    ToolOptions noToken = publisher;
    noToken.tokenFile.reset();
    EXPECT_EQ(
        commandError("sign_list", {unsignedList.string()}, noToken),
        "sign_list needs --token-file");

    // Sign to a file, then verify with every check on
    ToolOptions signer = publisher;
    signer.outFile = file("vl.json");
    EXPECT_NE(
        run("sign_list", {unsignedList.string()}, signer).out.find("written to"),
        std::string::npos);

    ToolOptions verifier = optionsFor(publisher.keyFile);
    verifier.validatorsFile = unsignedList;
    verifier.expectedKey = master;
    auto const verified = run("verify_list", {signer.outFile->string()}, verifier);
    EXPECT_EQ(verified.rc, EXIT_SUCCESS) << verified.out;
    {
        ToolOptions wrong = verifier;
        wrong.expectedKey = SigningKeys(KeyType::Ed25519).publicKey();
        EXPECT_EQ(run("verify_list", {signer.outFile->string()}, wrong).rc, EXIT_FAILURE);
    }

    // To stdout without --out
    {
        ToolOptions const stdoutSigner = publisher;
        auto const r = run("sign_list", {unsignedList.string()}, stdoutSigner);
        json::Reader reader;
        json::Value jv;
        EXPECT_TRUE(reader.parse(r.out, jv)) << r.out;
        EXPECT_EQ(jv[jss::public_key].asString(), strHex(master));
    }

    // Version 2, appended to itself, then appended again after a key rotation
    ToolOptions v2 = signer;
    v2.listVersion = 2;
    v2.outFile = file("vl2.json");
    run("sign_list", {unsignedList.string()}, v2);
    v2.appendFile = v2.outFile;
    v2.outFile = file("vl2b.json");
    run("sign_list", {unsignedList.string()}, v2);
    EXPECT_EQ(run("verify_list", {v2.outFile->string()}, verifier).rc, EXIT_SUCCESS);
    {
        ToolOptions rotated = v2;
        rotated.outFile = file("publisher-token-2.txt");
        run("create_token", {}, rotated);
        rotated.tokenFile = rotated.outFile;
        rotated.appendFile = v2.outFile;
        rotated.outFile = file("vl2c.json");
        run("sign_list", {unsignedList.string()}, rotated);
        auto const r = run("verify_list", {rotated.outFile->string()}, verifier);
        EXPECT_EQ(r.rc, EXIT_SUCCESS) << r.out;
        json::Reader reader;
        json::Value report;
        reader.parse(r.out, report);
        EXPECT_EQ(report["manifest_sequence"].asUInt(), 2u);
        EXPECT_EQ(report["blobs"].size(), 3u);
    }

    // Output paths that cannot be opened, inputs that cannot be read
    {
        ToolOptions bad = signer;
        bad.outFile = file("missing/vl.json");
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, bad),
            "Cannot write output file: " + bad.outFile->string());

        // The output may not replace the list it signs
        bad.outFile = unsignedList;
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, bad),
            "--out names an input file: " + unsignedList.string());

        // A failed command leaves an existing output as it was
        bad.outFile = file("kept.json");
        writeFile(*bad.outFile, "previous");
        EXPECT_EQ(
            commandError("sign_list", {file("missing.json").string()}, bad),
            "Failed to open file: " + file("missing.json").string());
        EXPECT_EQ(readFile(*bad.outFile), "previous");
        EXPECT_FALSE(std::filesystem::exists(file("kept.json.tmp")));
    }
    EXPECT_EQ(
        commandError("verify_list", {file("missing.json").string()}, verifier),
        "Failed to open file: " + file("missing.json").string());
    writeFile(file("not.json"), "nope\n");
    EXPECT_EQ(
        commandError("verify_list", {file("not.json").string()}, verifier),
        "Not a JSON document: " + file("not.json").string());

    // Tokens that cannot sign a list: an invalid manifest, a secret of another key
    auto const writeToken = [&](std::filesystem::path const& path, ValidatorToken const& token) {
        writeFile(path, "[validator_token]\n" + tokenToBase64(token) + "\n");
    };
    auto const secret = generateSecretKey(KeyType::Ed25519, randomSeed());
    {
        ToolOptions badManifest = signer;
        badManifest.tokenFile = file("bad-manifest-token.txt");
        writeToken(
            *badManifest.tokenFile, ValidatorToken{.manifest = "AAAA", .validationSecret = secret});
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, badManifest),
            "The token's manifest is not valid");
        ToolOptions wrongSecret = signer;
        wrongSecret.tokenFile = file("wrong-secret-token.txt");
        writeToken(
            *wrongSecret.tokenFile,
            ValidatorToken{
                .manifest = loadTokenFile(*publisher.tokenFile).manifest,
                .validationSecret = secret});
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, wrongSecret),
            "The token's secret does not match its manifest");
    }
}

TEST_F(CommandsTest, external_list_signing)
{
    // The master key delegates to a signing key it never holds; the list is then
    // signed in two steps with that key.
    ToolOptions const publisher = optionsFor(file("publisher.json"));
    run("create_keys", {}, publisher);
    SigningKeys const external(KeyType::Ed25519);
    ToolOptions delegate = publisher;
    delegate.signingKey = external.publicKey();
    delegate.outFile = file("manifest.txt");
    auto const start = run("start_token", {}, delegate);
    auto const bytes = start.out.substr(0, start.out.find('\n'));
    run("finish_token", {keys(publisher).signHex(bytes), external.signHex(bytes)}, delegate);

    auto const unsignedList = file("unsigned.json");
    writeFile(unsignedList, unsignedListText(makeValidators(2), 2026091301, netClockNow() + 3600));

    ToolOptions hardware = publisher;
    EXPECT_EQ(
        commandError("start_sign_list", {unsignedList.string()}, hardware),
        "start_sign_list needs --manifest-file");
    EXPECT_EQ(
        commandError("finish_sign_list", {"00", unsignedList.string()}, hardware),
        "finish_sign_list needs --manifest-file");
    hardware.manifestFile = delegate.outFile;

    auto const toSign = run("start_sign_list", {unsignedList.string()}, hardware);
    auto const listBytes = toSign.out.substr(0, toSign.out.find('\n'));
    EXPECT_EQ(listBytes, strHex(makeSlice(loadUnsignedList(unsignedList).canonical)));

    hardware.outFile = file("vl.json");
    EXPECT_EQ(
        commandError(
            "finish_sign_list",
            {keys(publisher).signHex(listBytes), unsignedList.string()},
            hardware),
        "The signature does not verify under the manifest's signing key");
    EXPECT_EQ(
        run("finish_sign_list", {external.signHex(listBytes), unsignedList.string()}, hardware).rc,
        EXIT_SUCCESS);
    ToolOptions verifier = optionsFor(publisher.keyFile);
    verifier.validatorsFile = unsignedList;
    EXPECT_EQ(run("verify_list", {hardware.outFile->string()}, verifier).rc, EXIT_SUCCESS);

    // A version 2 append under the same manifest works; after a rotation it
    // cannot re-sign the earlier blobs and says so
    ToolOptions v2 = hardware;
    v2.listVersion = 2;
    v2.outFile = file("vl2.json");
    run("finish_sign_list", {external.signHex(listBytes), unsignedList.string()}, v2);
    v2.appendFile = v2.outFile;
    v2.outFile = file("vl2b.json");
    run("finish_sign_list", {external.signHex(listBytes), unsignedList.string()}, v2);
    EXPECT_EQ(run("verify_list", {v2.outFile->string()}, verifier).rc, EXIT_SUCCESS);
    {
        ToolOptions rotated = publisher;
        rotated.outFile = file("token.txt");
        run("create_token", {}, rotated);
        ToolOptions append = v2;
        append.manifestFile.reset();
        append.tokenFile = rotated.outFile;
        append.appendFile = v2.outFile;
        append.outFile = file("vl2c.json");
        EXPECT_EQ(run("sign_list", {unsignedList.string()}, append).rc, EXIT_SUCCESS);
        EXPECT_EQ(run("verify_list", {append.outFile->string()}, verifier).rc, EXIT_SUCCESS);
    }

    // A revoked manifest signs nothing
    SigningKeys revokedKeys(KeyType::Ed25519);
    ToolOptions revoked = hardware;
    revoked.manifestFile = file("revoked-manifest.txt");
    writeFile(*revoked.manifestFile, revokedKeys.revoke() + "\n");
    EXPECT_EQ(
        commandError("start_sign_list", {unsignedList.string()}, revoked),
        "The manifest is revoked");
    EXPECT_EQ(
        commandError("finish_sign_list", {"00", unsignedList.string()}, revoked),
        "The manifest is revoked");
}

}  // namespace xrpl::tools::test
