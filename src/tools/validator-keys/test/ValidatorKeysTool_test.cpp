#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/protocol/SecretKey.h>

#include <boost/algorithm/string.hpp>

#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>
#include <tools/validator-keys/ValidatorKeysTool.h>
#include <tools/validator-keys/test/KeyFileGuard.h>

namespace xrpl {

namespace tests {

class ValidatorKeysTool_test : public beast::unit_test::Suite
{
private:
    static ToolOptions
    toolOptions(boost::filesystem::path const& keyFile)
    {
        ToolOptions options;
        options.keyFile = keyFile;
        return options;
    }

    // Allow a stream to be redirected. Destructor restores old streambuf.
    class Redirect
    {
    public:
        Redirect(std::ostream& stream, std::stringstream& sStream)
            : stream_(stream), old_(stream_.rdbuf(sStream.rdbuf()))
        {
        }

        virtual ~Redirect()
        {
            stream_.rdbuf(old_);
        }

    private:
        std::ostream& stream_;
        std::streambuf* const old_;
    };

    // Allow cout to be redirected.  Destructor restores old cout streambuf.
    class CoutRedirect : public Redirect
    {
    public:
        CoutRedirect(std::stringstream& sStream) : Redirect(std::cout, sStream)
        {
        }

        ~CoutRedirect()
        {
        }
    };

    void
    testCreateKeyFile()
    {
        testcase("Create Key File");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        createKeyFile(keyFile);
        BEAST_EXPECT(exists(keyFile));

        std::string const expectedError =
            "Refusing to overwrite existing key file: " + keyFile.string();
        std::string error;
        try
        {
            createKeyFile(keyFile);
            fail();
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }
        BEAST_EXPECT(error == expectedError);
    }

    void
    testCreateToken()
    {
        testcase("Create Token");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        auto testToken = [this](path const& keyFile, std::string const& expectedError) {
            try
            {
                createToken(toolOptions(keyFile));
                BEAST_EXPECT(expectedError.empty());
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(e.what() == expectedError);
            }
        };

        {
            std::string const expectedError = "Failed to open key file: " + keyFile.string();
            testToken(keyFile, expectedError);
        }

        createKeyFile(keyFile);

        {
            std::string const expectedError = "";
            testToken(keyFile, expectedError);
        }
        {
            auto const keyType = KeyType::Ed25519;
            auto const kp = generateKeyPair(keyType, randomSeed());

            auto keys =
                SigningKeys(keyType, kp.second, std::numeric_limits<std::uint32_t>::max() - 1);

            keys.writeToFile(keyFile);
            std::string const expectedError =
                "Maximum number of tokens have already been generated.\n"
                "Revoke validator keys if previous token has been compromised.";
            testToken(keyFile, expectedError);
        }
        {
            createRevocation(keyFile);
            std::string const expectedError = "Validator keys have been revoked.";
            testToken(keyFile, expectedError);
        }
    }

    void
    testCreateRevocation()
    {
        testcase("Create Revocation");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        auto expectedError = "Failed to open key file: " + keyFile.string();
        std::string error;
        try
        {
            createRevocation(keyFile);
            fail();
        }
        catch (std::runtime_error& e)
        {
            error = e.what();
        }
        BEAST_EXPECT(error == expectedError);

        createKeyFile(keyFile);
        BEAST_EXPECT(exists(keyFile));

        createRevocation(keyFile);
        createRevocation(keyFile);
    }

    void
    testCreateKeyFileExternal()
    {
        testcase("Create Key File External");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        path const keyFile = subdir / "validator_keys.json";

        // The externalKey will contain a secret key, and be used
        // to simulate the actions of an actual external signing device
        // or process. Note that it is const and not written to disk.
        SigningKeys const externalKey(KeyType::Ed25519);

        auto testCreate = [this, &subdir, &keyFile](
                              std::string pubKey, std::string const& expectedError) {
            KeyFileGuard const g(*this, subdir.string());

            try
            {
                createExternal(pubKey, keyFile);
                BEAST_EXPECT(expectedError.empty());
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(e.what() == expectedError);
            }
        };
        // Test a few different ways to create the file, and remove the file in
        // between
        {
            std::string const pubKey(strHex(externalKey.publicKey()));
            std::string const expectedError;

            testCreate(pubKey, expectedError);
        }
        {
            auto const& key = externalKey.publicKey();
            std::string const pubKey(base64Encode(key.data(), key.size()));
            std::string const expectedError;

            testCreate(pubKey, expectedError);
        }
        {
            std::string badPubKey(strHex(externalKey.publicKey()));
            badPubKey.insert(badPubKey.size() / 2, "n");
            std::string const expectedError = "Unable to parse public key: " + badPubKey;

            testCreate(badPubKey, expectedError);
        }
        {
            std::string const badPubKey = "abcd";
            std::string const expectedError = "Unable to parse public key: " + badPubKey;

            testCreate(badPubKey, expectedError);
        }

        // Use one file for the remainder of the tests
        KeyFileGuard const g(*this, subdir.string());

        std::string const pubKey(toBase58(TokenType::NodePublic, externalKey.publicKey()));

        createExternal(pubKey, keyFile);

        BEAST_EXPECT(exists(keyFile));

        std::string const expectedError =
            "Refusing to overwrite existing key file: " + keyFile.string();
        std::string error;
        try
        {
            createExternal(pubKey, keyFile);
            fail();
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }
        BEAST_EXPECT(error == expectedError);
    }

    void
    testCreateTokenExternal()
    {
        testcase("Create Token External");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        // The external key will contain a secret key, and be used
        // to simulate the actions of an actual external signing device
        // or process. Note that it is const.
        KeyType const externalKeyType = KeyType::Ed25519;
        SigningKeys const externalKey(externalKeyType);
        std::string const pubKey(toBase58(TokenType::NodePublic, externalKey.publicKey()));

        auto testStart = [this](path const& keyFile, std::string const& expectedError) {
            std::stringstream capture;
            CoutRedirect coutRedirect{capture};
            try
            {
                startToken(toolOptions(keyFile));
                BEAST_EXPECT(expectedError.empty());
                return capture.str();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(e.what() == expectedError);
            }
            return std::string();
        };

        auto testFinish =
            [this](std::string const& sig, path const& keyFile, std::string const& expectedError) {
                try
                {
                    finishToken({sig}, toolOptions(keyFile));
                    BEAST_EXPECT(expectedError.empty());
                }
                catch (std::exception const& e)
                {
                    BEAST_EXPECT(e.what() == expectedError);
                }
            };

        {
            std::string const expectedError = "Failed to open key file: " + keyFile.string();
            BEAST_EXPECT(testStart(keyFile, expectedError).empty());
        }

        createExternal(pubKey, keyFile);

        std::string const noError = "";
        {
            auto const start = testStart(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = externalKey.signHex(start);
            testFinish(sig, keyFile, noError);
        }
        {
            auto const start = testStart(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = [&]() {
                auto sigBlob = strUnHex(externalKey.signHex(start));
                if (BEAST_EXPECT(sigBlob))
                    return base64Encode(sigBlob->data(), sigBlob->size());
                return base64Encode("fail");
            }();

            testFinish(sig, keyFile, noError);
        }
        {
            std::string const expectedError = "Manifest is not properly signed";
            auto const start = testStart(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = externalKey.sign("foo");
            testFinish(sig, keyFile, expectedError);
        }
        {
            std::string const expectedError = "Invalid master signature";
            auto const start = testStart(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = "bad signature";
            testFinish(sig, keyFile, expectedError);
        }
        {
            {
                // Need to ensure any pending token is gone. Best
                // way to do that is to generate one successfully
                auto const start = testStart(keyFile, noError);
                BEAST_EXPECT(!start.empty());
                auto const sig = externalKey.signHex(start);
                testFinish(sig, keyFile, noError);
            }

            std::string const expectedError = "No pending token to finish";
            auto const sig = externalKey.sign("foo");
            testFinish(sig, keyFile, expectedError);
        }
        {
            auto keys = SigningKeys(
                externalKeyType,
                externalKey.publicKey(),
                std::numeric_limits<std::uint32_t>::max() - 1);

            keys.writeToFile(keyFile);
            std::string const expectedError =
                "Maximum number of tokens have already been generated.\n"
                "Revoke validator keys if previous token has been compromised.";
            BEAST_EXPECT(testStart(keyFile, expectedError).empty());
        }
        {
            // Create the file revoked
            auto keys = SigningKeys(externalKeyType, externalKey.publicKey(), 42, true);

            keys.writeToFile(keyFile);
            std::string const expectedError = "Validator keys have been revoked.";
            BEAST_EXPECT(testStart(keyFile, expectedError).empty());
        }
    }

    void
    testCreateRevocationExternal()
    {
        testcase("Create Revocation External");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        // The external key will contain a secret key, and be used
        // to simulate the actions of an actual external signing device
        // or process. Note that it is const.
        SigningKeys const externalKey(KeyType::Ed25519);
        std::string const pubKey(toBase58(TokenType::NodePublic, externalKey.publicKey()));

        auto testStartRevoke = [this](
                                   path const& keyFile,
                                   std::string const& expectedError,
                                   bool expectRevoked = true) {
            std::stringstream capture;
            std::stringstream errCapture;
            CoutRedirect coutRedirect{capture};
            Redirect cerrRedirect{std::cerr, errCapture};
            try
            {
                startRevocation(keyFile);
                BEAST_EXPECT(expectedError.empty());
                if (expectRevoked)
                    BEAST_EXPECT(
                        errCapture.str() ==
                        "WARNING: Validator keys have already been "
                        "revoked!\n\n");
                else
                    BEAST_EXPECT(
                        errCapture.str() == "WARNING: This will revoke your validator keys!\n\n");

                return capture.str();
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(e.what() == expectedError);
            }
            return std::string();
        };

        auto testFinishRevoke =
            [this](std::string const& sig, path const& keyFile, std::string const& expectedError) {
                try
                {
                    finishRevocation(sig, keyFile);
                    BEAST_EXPECT(expectedError.empty());
                }
                catch (std::exception const& e)
                {
                    BEAST_EXPECT(e.what() == expectedError);
                }
            };

        std::string const noError = "";
        {
            auto const expectedError = "Failed to open key file: " + keyFile.string();
            testStartRevoke(keyFile, expectedError);
        }

        createExternal(pubKey, keyFile);
        BEAST_EXPECT(exists(keyFile));

        {
            auto const start = testStartRevoke(keyFile, noError, false);
            BEAST_EXPECT(!start.empty());
            auto const sig = externalKey.signHex(start);
            testFinishRevoke(sig, keyFile, noError);
        }
        {
            auto const start = testStartRevoke(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = [&]() {
                auto sigBlob = strUnHex(externalKey.signHex(start));
                if (BEAST_EXPECT(sigBlob))
                    return base64Encode(sigBlob->data(), sigBlob->size());
                return base64Encode("fail");
            }();
            testFinishRevoke(sig, keyFile, noError);
        }

        {
            // keys can be revoked multiple times
            auto const start = testStartRevoke(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = externalKey.signHex(start);
            testFinishRevoke(sig, keyFile, noError);
        }
        {
            std::string const expectedError = "Manifest is not properly signed";
            auto const start = testStartRevoke(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = externalKey.sign("foo");
            testFinishRevoke(sig, keyFile, expectedError);
        }
        {
            std::string const expectedError = "Invalid master signature";
            auto const start = testStartRevoke(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = "bad signature";
            testFinishRevoke(sig, keyFile, expectedError);
        }
        {
            // Unlike tokens, which have a random key and a changing sequence,
            // revocations are fixed, so as long as a valid signature has been
            // generated, it can be reused. Same idea as how a signed revocation
            // can be stored and released at any time.
            // Generate a revocation successfully
            auto const start = testStartRevoke(keyFile, noError);
            BEAST_EXPECT(!start.empty());
            auto const sig = externalKey.signHex(start);
            testFinishRevoke(sig, keyFile, noError);

            // Reuse the signature.
            testFinishRevoke(sig, keyFile, noError);
        }
    }

    void
    testSign()
    {
        testcase("Sign");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        auto testSign =
            [this](std::string const& data, path const& keyFile, std::string const& expectedError) {
                try
                {
                    signData(data, keyFile);
                    BEAST_EXPECT(expectedError.empty());
                }
                catch (std::exception const& e)
                {
                    BEAST_EXPECT(e.what() == expectedError);
                }
            };

        std::string const data = "data to sign";

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        {
            std::string const expectedError = "Failed to open key file: " + keyFile.string();
            testSign(data, keyFile, expectedError);
        }

        createKeyFile(keyFile);
        BEAST_EXPECT(exists(keyFile));

        {
            std::string const emptyData = "";
            std::string const expectedError = "Syntax error: Must specify data string to sign";
            testSign(emptyData, keyFile, expectedError);
        }
        {
            std::string const expectedError = "";
            testSign(data, keyFile, expectedError);
        }
    }

    void
    testHexSign()
    {
        testcase("Sign Hex");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        auto testSign =
            [this](std::string const& data, path const& keyFile, std::string const& expectedError) {
                try
                {
                    signHexData(data, keyFile);
                    BEAST_EXPECT(expectedError.empty());
                }
                catch (std::exception const& e)
                {
                    BEAST_EXPECT(e.what() == expectedError);
                }
            };

        std::string const rawdata = "data to sign";
        std::string const data = strHex(rawdata);

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        {
            std::string const expectedError = "Failed to open key file: " + keyFile.string();
            testSign(data, keyFile, expectedError);
        }

        createKeyFile(keyFile);
        BEAST_EXPECT(exists(keyFile));

        {
            std::string const emptyData = "";
            std::string const expectedError = "Syntax error: Must specify data string to sign";
            testSign(emptyData, keyFile, expectedError);
        }
        {
            std::string const expectedError = "";
            testSign(data, keyFile, expectedError);
        }
    }

    void
    testRunCommand()
    {
        testcase("Run Command");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard g(*this, subdir.string());
        path const keyFile = subdir / "validator_keys.json";

        auto testCommand = [this](
                               std::string const& command,
                               std::vector<std::string> const& args,
                               path const& keyFile,
                               std::string const& expectedError) {
            try
            {
                runCommand(command, args, toolOptions(keyFile));
                BEAST_EXPECT(expectedError.empty());
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(e.what() == expectedError);
            }
        };

        std::vector<std::string> const noArgs;
        std::vector<std::string> const oneArg = {"some data"};
        std::vector<std::string> const oneHexArg = {strHex(oneArg[0])};
        std::vector<std::string> const oneDomainArg = {"validator.example.com"};
        std::vector<std::string> const twoArgs = {"data", "more data"};
        std::string const noError = "";
        std::string const argError = "Syntax error: Wrong number of arguments";
        {
            std::string const command = "unknown";
            std::string const expectedError = "Unknown command: " + command;
            testCommand(command, noArgs, keyFile, expectedError);
            testCommand(command, oneArg, keyFile, expectedError);
            testCommand(command, twoArgs, keyFile, expectedError);
        }
        {
            std::string const command = "create_keys";
            testCommand(command, noArgs, keyFile, noError);
            testCommand(command, oneArg, keyFile, argError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "create_token";
            testCommand(command, noArgs, keyFile, noError);
            testCommand(command, oneArg, keyFile, argError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "set_domain";
            testCommand(command, noArgs, keyFile, argError);
            testCommand(command, oneDomainArg, keyFile, noError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "attest_domain";
            testCommand(command, noArgs, keyFile, noError);
            testCommand(command, oneArg, keyFile, argError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "clear_domain";
            testCommand(command, noArgs, keyFile, noError);
            testCommand(command, oneArg, keyFile, argError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "show_manifest";
            testCommand(command, noArgs, keyFile, argError);
            testCommand(command, oneArg, keyFile, noError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "revoke_keys";
            testCommand(command, noArgs, keyFile, noError);
            testCommand(command, oneArg, keyFile, argError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "sign";
            testCommand(command, noArgs, keyFile, argError);
            testCommand(command, oneArg, keyFile, noError);
            testCommand(command, twoArgs, keyFile, argError);
        }
        {
            std::string const command = "sign_hex";
            testCommand(command, noArgs, keyFile, argError);
            testCommand(command, oneHexArg, keyFile, noError);
            testCommand(command, twoArgs, keyFile, argError);
        }

        // External signing functionality.
        std::string const pkArg = [&]() {
            SigningKeys const keys = SigningKeys::make_SigningKeys(keyFile);
            return toBase58(TokenType::NodePublic, keys.publicKey());
        }();
        {
            // Purposely shadow "keyFile" from the outer context
            // to prevent reuse
            path const keyFile = subdir / "validator_keys_ext.json";
            // For the functions that expect a signature, don't pass in a
            // valid signature. This is the error that is returned.
            std::string const masterKeyError = "Invalid master signature";

            {
                std::string const command = "create_external";
                testCommand(command, noArgs, keyFile, argError);
                testCommand(command, {pkArg}, keyFile, noError);
                testCommand(command, twoArgs, keyFile, argError);
            }
            {
                std::string const command = "start_token";
                testCommand(command, noArgs, keyFile, noError);
                testCommand(command, oneArg, keyFile, argError);
                testCommand(command, twoArgs, keyFile, argError);
            }
            {
                // One signature finishes a software-signed token, two finish a
                // token whose signing key is external.
                std::string const command = "finish_token";
                std::vector<std::string> const threeArgs = {"a", "b", "c"};
                testCommand(command, noArgs, keyFile, argError);
                testCommand(command, oneArg, keyFile, masterKeyError);
                testCommand(command, twoArgs, keyFile, masterKeyError);
                testCommand(command, threeArgs, keyFile, argError);
            }
            {
                std::stringstream ignore;
                Redirect errRedirect(std::cerr, ignore);
                std::string const command = "start_revoke_keys";
                testCommand(command, noArgs, keyFile, noError);
                testCommand(command, oneArg, keyFile, argError);
                testCommand(command, twoArgs, keyFile, argError);
            }
            {
                std::string const command = "finish_revoke_keys";
                testCommand(command, noArgs, keyFile, argError);
                testCommand(command, oneArg, keyFile, masterKeyError);
                testCommand(command, twoArgs, keyFile, argError);
            }
        }
    }

    void
    testListCommands()
    {
        testcase("List Commands");

        std::stringstream coutCapture;
        CoutRedirect coutRedirect{coutCapture};

        using namespace boost::filesystem;

        path const subdir = "test_key_file";
        KeyFileGuard const g(*this, subdir.string());

        auto run = [this](
                       std::string const& command,
                       std::vector<std::string> const& args,
                       ToolOptions const& options,
                       std::string const& expectedError) -> int {
            try
            {
                auto const rc = runCommand(command, args, options);
                BEAST_EXPECTS(expectedError.empty(), "expected: " + expectedError);
                return rc;
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECTS(e.what() == expectedError, e.what());
                return -1;
            }
        };

        // The publisher: a key file and a token carrying an ed25519 signing key
        ToolOptions publisher;
        publisher.keyFile = subdir / "publisher.json";
        publisher.tokenKeyType = KeyType::Ed25519;
        publisher.tokenFile = subdir / "publisher-token.txt";
        publisher.outFile = publisher.tokenFile;
        run("create_keys", {}, publisher, "");
        run("create_token", {}, publisher, "");
        BEAST_EXPECT(exists(*publisher.tokenFile));
        publisher.outFile.reset();

        // Two validators, each with a token whose manifest goes in the list
        std::string validators;
        for (int i = 0; i < 2; ++i)
        {
            SigningKeys keys(KeyType::Ed25519);
            auto const token = keys.createValidatorToken(KeyType::Secp256k1);
            validators += (i ? ", " : "") + std::string("{\"validation_public_key\": \"") +
                strHex(keys.publicKey()) + "\", \"manifest\": \"" + token->manifest + "\"}";
        }
        auto const now = rippleEpochNow();
        path const unsignedList = subdir / "unsigned.json";
        {
            std::ofstream o(unsignedList.string());
            o << "{\"sequence\": 2026091301, \"expiration\": " << now + 3600
              << ", \"validators\": [" << validators << "]}\n";
        }

        // sign_list needs a token
        {
            ToolOptions noToken = publisher;
            noToken.tokenFile.reset();
            run("sign_list", {unsignedList.string()}, noToken, "sign_list needs --token-file");
        }

        // Sign, then verify with every check on
        ToolOptions signer = publisher;
        signer.outFile = subdir / "vl.json";
        run("sign_list", {unsignedList.string()}, signer, "");
        BEAST_EXPECT(exists(*signer.outFile));

        ToolOptions verifier;
        verifier.keyFile = publisher.keyFile;
        verifier.validatorsFile = unsignedList;
        verifier.expectedKey = SigningKeys::make_SigningKeys(publisher.keyFile).publicKey();
        BEAST_EXPECT(run("verify_list", {signer.outFile->string()}, verifier, "") == 0);

        // The wrong expected key fails verification
        {
            ToolOptions wrong = verifier;
            wrong.expectedKey = SigningKeys(KeyType::Ed25519).publicKey();
            BEAST_EXPECT(run("verify_list", {signer.outFile->string()}, wrong, "") == 1);
        }

        // Version 2, appended to itself once
        ToolOptions v2 = signer;
        v2.listVersion = 2;
        v2.outFile = subdir / "vl2.json";
        run("sign_list", {unsignedList.string()}, v2, "");
        v2.appendFile = v2.outFile;
        v2.outFile = subdir / "vl2b.json";
        run("sign_list", {unsignedList.string()}, v2, "");
        BEAST_EXPECT(run("verify_list", {v2.outFile->string()}, verifier, "") == 0);

        // External signing key: the master delegates to a key it never holds,
        // then the list is signed in two steps with that key.
        SigningKeys const external(KeyType::Ed25519);
        auto master = SigningKeys::make_SigningKeys(publisher.keyFile);
        auto const toSign = master.startValidatorToken(KeyType::Ed25519, external.publicKey());
        BEAST_EXPECT(toSign);
        auto const manifest = master.finishExternalToken(
            *strUnHex(master.signHex(*toSign)), *strUnHex(external.signHex(*toSign)));
        BEAST_EXPECT(manifest);
        master.writeToFile(publisher.keyFile);

        ToolOptions hardware;
        hardware.keyFile = publisher.keyFile;
        hardware.manifestFile = subdir / "manifest.txt";
        {
            std::ofstream o(hardware.manifestFile->string());
            o << *manifest << "\n";
        }
        run("start_sign_list",
            {unsignedList.string()},
            publisher,
            "start_sign_list needs --manifest-file");
        coutCapture.str("");
        run("start_sign_list", {unsignedList.string()}, hardware, "");
        auto bytes = coutCapture.str();
        boost::algorithm::trim(bytes);
        BEAST_EXPECT(!bytes.empty());

        hardware.outFile = subdir / "vl-external.json";
        run("finish_sign_list",
            {master.signHex(bytes), unsignedList.string()},
            hardware,
            "The signature does not verify under the manifest's signing key");
        run("finish_sign_list", {external.signHex(bytes), unsignedList.string()}, hardware, "");
        BEAST_EXPECT(run("verify_list", {hardware.outFile->string()}, verifier, "") == 0);
    }

public:
    void
    run() override
    {
        getVersionString();

        testCreateKeyFile();
        testCreateToken();
        testCreateRevocation();
        testCreateKeyFileExternal();
        testCreateTokenExternal();
        testCreateRevocationExternal();
        testSign();
        testHexSign();
        testRunCommand();
        testListCommands();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorKeysTool, keys, xrpl);

}  // namespace tests

}  // namespace xrpl
