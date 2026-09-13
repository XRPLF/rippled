#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Sign.h>

#include <tools/validator-keys/SigningKeys.h>
#include <tools/validator-keys/test/KeyFileGuard.h>

namespace xrpl {

namespace tests {

class SigningKeys_test : public beast::unit_test::Suite
{
private:
    void
    testKeyFile(
        boost::filesystem::path const& keyFile,
        json::Value const& jv,
        std::string const& expectedError)
    {
        {
            std::ofstream o(keyFile.string(), std::ios_base::trunc);
            o << jv.toStyledString();
            o.close();
        }

        try
        {
            SigningKeys::make_SigningKeys(keyFile);
            BEAST_EXPECT(expectedError.empty());
        }
        catch (std::runtime_error& e)
        {
            BEAST_EXPECT(e.what() == expectedError);
        }
    }

    std::array<KeyType, 2> const keyTypes{{KeyType::Ed25519, KeyType::Secp256k1}};

    void
    testMakeSigningKeys()
    {
        testcase("Make Validator Keys");

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        path const keyFile = subdir / "validator_keys.json";

        for (auto const keyType : keyTypes)
        {
            SigningKeys const keys(keyType);

            KeyFileGuard const g(*this, subdir.string());

            keys.writeToFile(keyFile);
            BEAST_EXPECT(exists(keyFile));

            auto const keys2 = SigningKeys::make_SigningKeys(keyFile);
            BEAST_EXPECT(keys == keys2);
        }
        {
            // Require expected fields
            KeyFileGuard g(*this, subdir.string());

            auto expectedError = "Failed to open key file: " + keyFile.string();
            std::string error;
            try
            {
                SigningKeys::make_SigningKeys(keyFile);
                fail();
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);

            expectedError = "Unable to parse json key file: " + keyFile.string();

            {
                std::ofstream o(keyFile.string(), std::ios_base::trunc);
                o << "{{}";
                o.close();
            }

            try
            {
                SigningKeys::make_SigningKeys(keyFile);
                fail();
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);

            json::Value jv;
            jv["dummy"] = "field";
            expectedError = "Key file '" + keyFile.string() + "' is missing \"key_type\" field";
            testKeyFile(keyFile, jv, expectedError);

            jv["key_type"] = "dummy keytype";
            expectedError = "Key file '" + keyFile.string() + "' is missing \"secret_key\" field";
            testKeyFile(keyFile, jv, expectedError);

            jv["secret_key"] = "dummy secret";
            expectedError =
                "Key file '" + keyFile.string() + "' is missing \"token_sequence\" field";
            testKeyFile(keyFile, jv, expectedError);

            jv["token_sequence"] = "dummy sequence";
            expectedError = "Key file '" + keyFile.string() + "' is missing \"revoked\" field";
            testKeyFile(keyFile, jv, expectedError);

            jv["revoked"] = "dummy revoked";
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"key_type\" field: " + jv["key_type"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            auto const keyType = KeyType::Ed25519;
            jv["key_type"] = to_string(keyType);
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"secret_key\" field: " + jv["secret_key"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            SigningKeys const keys(keyType);
            {
                auto const kp = generateKeyPair(keyType, randomSeed());
                jv["secret_key"] = toBase58(TokenType::NodePrivate, kp.second);
            }
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"token_sequence\" field: " +
                jv["token_sequence"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            jv["token_sequence"] = -1;
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"token_sequence\" field: " +
                jv["token_sequence"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            jv["token_sequence"] = json::UInt(std::numeric_limits<std::uint32_t>::max());
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"revoked\" field: " + jv["revoked"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            jv["revoked"] = false;
            expectedError = "";
            testKeyFile(keyFile, jv, expectedError);

            jv["revoked"] = true;
            testKeyFile(keyFile, jv, expectedError);
        }
    }

    void
    testCreateValidatorToken()
    {
        testcase("Create Validator Token");

        for (auto const keyType : keyTypes)
        {
            SigningKeys keys(keyType);
            std::uint32_t sequence = 0;

            for (auto const tokenKeyType : keyTypes)
            {
                auto const token = keys.createValidatorToken(tokenKeyType);

                if (!BEAST_EXPECT(token))
                    continue;

                auto const tokenPublicKey = derivePublicKey(tokenKeyType, token->validationSecret);

                STObject st(sfGeneric);
                auto const manifest = xrpl::base64Decode(token->manifest);
                SerialIter sit(manifest.data(), manifest.size());
                st.set(sit);

                auto const seq = get(st, sfSequence);
                BEAST_EXPECT(seq);
                BEAST_EXPECT(*seq == ++sequence);

                auto const tpk = get<PublicKey>(st, sfSigningPubKey);
                BEAST_EXPECT(tpk);
                BEAST_EXPECT(*tpk == tokenPublicKey);
                BEAST_EXPECT(verify(st, HashPrefix::Manifest, tokenPublicKey));

                auto const pk = get<PublicKey>(st, sfPublicKey);
                BEAST_EXPECT(pk);
                BEAST_EXPECT(*pk == keys.publicKey());
                BEAST_EXPECT(verify(st, HashPrefix::Manifest, keys.publicKey(), sfMasterSignature));

                try
                {
                    keys.verifyManifest();
                }
                catch (std::exception const& e)
                {
                    fail(e.what());
                }
            }
        }

        auto const keyType = KeyType::Ed25519;
        auto const kp = generateKeyPair(keyType, randomSeed());

        auto keys = SigningKeys(keyType, kp.second, std::numeric_limits<std::uint32_t>::max() - 1);

        BEAST_EXPECT(!keys.createValidatorToken(keyType));

        keys.revoke();
        BEAST_EXPECT(!keys.createValidatorToken(keyType));
    }

    void
    testRevoke()
    {
        testcase("Revoke");

        for (auto const keyType : keyTypes)
        {
            SigningKeys keys(keyType);

            auto const revocation = keys.revoke();

            STObject st(sfGeneric);
            auto const manifest = xrpl::base64Decode(revocation);
            SerialIter sit(manifest.data(), manifest.size());
            st.set(sit);

            auto const seq = get(st, sfSequence);
            BEAST_EXPECT(seq);
            BEAST_EXPECT(*seq == std::numeric_limits<std::uint32_t>::max());

            auto const pk = get(st, sfPublicKey);
            BEAST_EXPECT(pk);
            BEAST_EXPECT(*pk == keys.publicKey());
            BEAST_EXPECT(verify(st, HashPrefix::Manifest, keys.publicKey(), sfMasterSignature));

            try
            {
                keys.verifyManifest();
            }
            catch (std::exception const& e)
            {
                fail(e.what());
            }
        }
    }

    void
    signWorker(
        std::function<std::string(std::string const&)> modifyFunc,
        std::function<std::string(SigningKeys const&, std::string const&)> signFunc)
    {
        std::string const rawdata = "data to sign";
        std::string const data = modifyFunc(rawdata);

        std::map<KeyType, std::string> expected(
            {{KeyType::Ed25519,
              "2EE541D6825791BF5454C571D2B363EAB3F01C73159B1F"
              "237AC6D38663A82B9D5EAD262D5F776B916E68247A1F082090F3BAE7ABC939"
              "C8F29B0DC759FD712300"},
             {KeyType::Secp256k1,
              "3045022100F142C27BF83D8D4541C7A4E759DE64A672"
              "51A388A422DFDA6F4B470A2113ABC4022002DA56695F3A805F62B55E7CC8D5"
              "55438D64A229CD0B4BA2AE33402443B20409"}});

        for (auto const keyType : keyTypes)
        {
            auto const sk = generateSecretKey(keyType, generateSeed("test"));
            SigningKeys keys(keyType, sk, 1);

            {
                SigningKeys pkOnly(keyType, derivePublicKey(keyType, sk));
                try
                {
                    signFunc(pkOnly, data);
                    fail();
                }
                catch (std::exception const& e)
                {
                    using namespace std::string_literals;
                    BEAST_EXPECT(e.what() == "This key file cannot be used to sign."s);
                }
            }

            auto const signature = signFunc(keys, data);
            BEAST_EXPECT(expected[keyType] == signature);

            auto const ret = strUnHex(signature);
            BEAST_EXPECT(ret);
            BEAST_EXPECT(ret->size());
            BEAST_EXPECT(verify(keys.publicKey(), makeSlice(rawdata), makeSlice(*ret)));
        }
    }

    void
    testSign()
    {
        testcase("Sign");

        signWorker(
            [](auto const& data) { return data; },
            [](auto const& keys, auto const& data) { return keys.sign(data); });
    }

    void
    testSignHex()
    {
        testcase("Sign Hex");

        signWorker(
            [](auto const& data) { return strHex(data); },
            [](auto const& keys, auto const& data) { return keys.signHex(data); });
    }

    void
    testWriteToFile()
    {
        testcase("Write to File");

        using namespace boost::filesystem;

        auto const keyType = KeyType::Ed25519;
        SigningKeys keys(keyType);

        {
            path const subdir = "test_key_file";
            path const keyFile = subdir / "validator_keys.json";
            KeyFileGuard g(*this, subdir.string());

            keys.writeToFile(keyFile);
            BEAST_EXPECT(exists(keyFile));

            {
                auto fileKeys = SigningKeys::make_SigningKeys(keyFile);
                BEAST_EXPECT(keys == fileKeys);

                // Overwrite file with new sequence
                keys.createValidatorToken(KeyType::Secp256k1);
                keys.writeToFile(keyFile);
            }

            {
                auto const fileKeys = SigningKeys::make_SigningKeys(keyFile);
                BEAST_EXPECT(keys == fileKeys);
            }
        }
        {
            // Write to key file in current relative directory
            path const keyFile = "test_validator_keys.json";
            if (!exists(keyFile))
            {
                keys.writeToFile(keyFile);
                remove(keyFile.string());
            }
            else
            {
                // Cannot run the test. Someone created a file
                // where we want to put our key file
                Throw<std::runtime_error>("Cannot create key file: " + keyFile.string());
            }
        }
        {
            // Create key file directory
            path const subdir = "test_key_file";
            path const keyFile = subdir / "directories/to/create/validator_keys.json";
            KeyFileGuard g(*this, subdir.string());

            keys.writeToFile(keyFile);
            BEAST_EXPECT(exists(keyFile));

            auto const fileKeys = SigningKeys::make_SigningKeys(keyFile);
            BEAST_EXPECT(keys == fileKeys);
        }
        {
            // Fail if file cannot be opened for write
            path const subdir = "test_key_file";
            KeyFileGuard g(*this, subdir.string());

            path const badKeyFile = subdir / ".";
            auto expectedError = "Cannot open key file: " + badKeyFile.string();
            std::string error;
            try
            {
                keys.writeToFile(badKeyFile);
                fail();
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);

            // Fail if parent directory is existing file
            path const keyFile = subdir / "validator_keys.json";
            keys.writeToFile(keyFile);
            path const conflictingPath = keyFile / "validators_keys.json";
            expectedError = "Cannot create directory: " + conflictingPath.parent_path().string();
            try
            {
                keys.writeToFile(conflictingPath);
                fail();
            }
            catch (std::runtime_error& e)
            {
                error = e.what();
            }
            BEAST_EXPECT(error == expectedError);
        }
    }

    ////////////////////////////////////////////
    // Tests related to using external keys
    //
    // These tests will use two SigningKeys objects,
    // one with a secret key representing the external
    // signing mechanism, and one only containing the
    // public key from the first representing the real
    // worker.
    ////////////////////////////////////////////

    void
    testExternalMakeValidatorKeys()
    {
        testcase("Make External Validator Keys");

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        path const externalKeyFile = subdir / "validator_keys_external.json";
        path const keyFile = subdir / "validator_keys.json";

        for (auto const keyType : keyTypes)
        {
            SigningKeys const externalKeys(keyType);

            KeyFileGuard const g(*this, subdir.string());

            externalKeys.writeToFile(externalKeyFile);
            BEAST_EXPECT(exists(externalKeyFile));

            SigningKeys const keys(keyType, externalKeys.publicKey());
            keys.writeToFile(keyFile);
            BEAST_EXPECT(exists(keyFile));

            auto const keys2 = SigningKeys::make_SigningKeys(keyFile);
            BEAST_EXPECT(keys == keys2);
        }
        {
            // Require expected fields
            KeyFileGuard g(*this, subdir.string());

            auto expectedError = "Failed to open key file: " + keyFile.string();
            std::string error;

            json::Value jv;
            jv["key_type"] = "dummy keytype";

            jv["secret_key"] = "external";
            expectedError =
                "Key file '" + keyFile.string() + "' is missing \"token_sequence\" field";
            testKeyFile(keyFile, jv, expectedError);

            jv["token_sequence"] = "dummy sequence";
            expectedError = "Key file '" + keyFile.string() + "' is missing \"revoked\" field";
            testKeyFile(keyFile, jv, expectedError);

            jv["revoked"] = "dummy revoked";
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"key_type\" field: " + jv["key_type"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            auto const keyType = KeyType::Ed25519;
            jv["key_type"] = to_string(keyType);
            expectedError = "Key file '" + keyFile.string() + "' is missing \"public_key\" field";
            testKeyFile(keyFile, jv, expectedError);

            jv["public_key"] = "dummy public";
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"public_key\" field: " + jv["public_key"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            SigningKeys const keys(keyType);
            {
                auto const kp = generateKeyPair(keyType, randomSeed());
                jv["public_key"] = toBase58(TokenType::NodePublic, kp.first);
            }
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"token_sequence\" field: " +
                jv["token_sequence"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            jv["token_sequence"] = -1;
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"token_sequence\" field: " +
                jv["token_sequence"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            jv["token_sequence"] = json::UInt(std::numeric_limits<std::uint32_t>::max());
            expectedError = "Key file '" + keyFile.string() +
                "' contains invalid \"revoked\" field: " + jv["revoked"].toStyledString();
            testKeyFile(keyFile, jv, expectedError);

            jv["revoked"] = false;
            expectedError = "";
            testKeyFile(keyFile, jv, expectedError);

            jv["revoked"] = true;
            testKeyFile(keyFile, jv, expectedError);
        }
    }

    void
    testExternalCreateValidatorToken()
    {
        testcase("Create External Validator Token");

        using namespace std::string_literals;

        for (auto const keyType : keyTypes)
        {
            SigningKeys const externalKeys(keyType);
            SigningKeys keys(keyType, externalKeys.publicKey());
            std::uint32_t sequence = 0;

            for (auto const tokenKeyType : keyTypes)
            {
                try
                {
                    auto const token = keys.createValidatorToken(tokenKeyType);
                    fail();
                }
                catch (std::exception const& e)
                {
                    BEAST_EXPECT(e.what() == "This key file cannot be used to sign tokens."s);
                }

                auto const start = keys.startValidatorToken(tokenKeyType);

                if (!BEAST_EXPECT(start))
                    continue;

                auto const sig = externalKeys.signHex(*start);
                auto const sigBlob = strUnHex(sig);
                if (!BEAST_EXPECT(sigBlob))
                    continue;
                auto const token = keys.finishToken(*sigBlob);

                if (!BEAST_EXPECT(token))
                    continue;

                auto const tokenPublicKey = derivePublicKey(tokenKeyType, token->validationSecret);

                STObject st(sfGeneric);
                auto const manifest = xrpl::base64Decode(token->manifest);
                SerialIter sit(manifest.data(), manifest.size());
                st.set(sit);

                auto const seq = get(st, sfSequence);
                BEAST_EXPECT(seq);
                BEAST_EXPECT(*seq == ++sequence);

                auto const tpk = get<PublicKey>(st, sfSigningPubKey);
                BEAST_EXPECT(tpk);
                BEAST_EXPECT(*tpk == tokenPublicKey);
                BEAST_EXPECT(verify(st, HashPrefix::Manifest, tokenPublicKey));

                auto const pk = get<PublicKey>(st, sfPublicKey);
                BEAST_EXPECT(pk);
                BEAST_EXPECT(*pk == keys.publicKey());
                BEAST_EXPECT(verify(st, HashPrefix::Manifest, keys.publicKey(), sfMasterSignature));

                try
                {
                    keys.verifyManifest();
                }
                catch (std::exception const& e)
                {
                    fail(e.what());
                }
            }
        }

        auto const keyType = KeyType::Ed25519;
        auto const kp = generateKeyPair(keyType, randomSeed());

        {
            // The next sequence is the special "revoked" value
            auto keys =
                SigningKeys(keyType, kp.first, std::numeric_limits<std::uint32_t>::max() - 1);

            BEAST_EXPECT(!keys.startValidatorToken(keyType));
        }

        {
            // Key is revoked
            auto keys = SigningKeys(keyType, kp.first, std::numeric_limits<std::uint32_t>::max());

            BEAST_EXPECT(!keys.startValidatorToken(keyType));
        }
    }

    void
    testExternalRevoke()
    {
        testcase("External Revoke");

        using namespace std::string_literals;

        for (auto const keyType : keyTypes)
        {
            SigningKeys const externalKeys(keyType);
            SigningKeys keys(keyType, externalKeys.publicKey());

            try
            {
                auto const revocation = keys.revoke();
                fail();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(e.what() == "This key file cannot be used to sign tokens."s);
            }
            auto const start = keys.startRevoke();
            auto const sig = externalKeys.signHex(start);
            auto const sigBlob = strUnHex(sig);
            if (!BEAST_EXPECT(sigBlob))
                continue;

            auto const revocation = keys.finishRevoke(*sigBlob);

            STObject st(sfGeneric);
            auto const manifest = xrpl::base64Decode(revocation);
            SerialIter sit(manifest.data(), manifest.size());
            st.set(sit);

            auto const seq = get(st, sfSequence);
            BEAST_EXPECT(seq);
            BEAST_EXPECT(*seq == std::numeric_limits<std::uint32_t>::max());

            auto const pk = get(st, sfPublicKey);
            BEAST_EXPECT(pk);
            BEAST_EXPECT(*pk == keys.publicKey());
            BEAST_EXPECT(verify(st, HashPrefix::Manifest, keys.publicKey(), sfMasterSignature));

            try
            {
                keys.verifyManifest();
            }
            catch (std::exception const& e)
            {
                fail(e.what());
            }
        }
    }

    void
    testExternalWriteToFile()
    {
        testcase("External Write to File");

        using namespace boost::filesystem;

        auto const keyType = KeyType::Ed25519;
        SigningKeys const externalKeys(keyType);
        SigningKeys keys(keyType, externalKeys.publicKey());

        {
            path const subdir = "test_key_file";
            path const keyFile = subdir / "validator_keys.json";
            KeyFileGuard g(*this, subdir.string());

            keys.writeToFile(keyFile);
            BEAST_EXPECT(exists(keyFile));

            {
                auto const sigBlob = [&]() -> std::optional<Blob> {
                    auto fileKeys = SigningKeys::make_SigningKeys(keyFile);
                    BEAST_EXPECT(keys == fileKeys);

                    // Prepare to write new sequence
                    auto const start = keys.startValidatorToken(KeyType::Secp256k1);
                    if (!BEAST_EXPECT(start))
                        return std::nullopt;
                    // keys looks the same as the original file (though
                    // the pending fields have changed)
                    BEAST_EXPECT(keys == fileKeys);
                    keys.writeToFile(keyFile);

                    auto const sig = externalKeys.signHex(*start);
                    auto const sigBlob = strUnHex(sig);
                    return sigBlob;
                }();
                auto fileKeys = SigningKeys::make_SigningKeys(keyFile);
                BEAST_EXPECT(keys == fileKeys);

                if (!sigBlob)
                    return;

                // Overwrite file with new sequence
                auto const token = keys.finishToken(*sigBlob);
                if (!BEAST_EXPECT(token))
                    return;
                BEAST_EXPECT(keys != fileKeys);
                keys.writeToFile(keyFile);
            }

            {
                auto const fileKeys = SigningKeys::make_SigningKeys(keyFile);
                BEAST_EXPECT(keys == fileKeys);
            }
        }
    }

public:
    void
    run() override
    {
        testMakeSigningKeys();
        testCreateValidatorToken();
        testRevoke();
        testSign();
        testSignHex();
        testWriteToFile();
        // External
        testExternalMakeValidatorKeys();
        testExternalCreateValidatorToken();
        testExternalRevoke();
        testExternalWriteToFile();
    }
};

BEAST_DEFINE_TESTSUITE(SigningKeys, keys, xrpl);

}  // namespace tests

}  // namespace xrpl
