#include <test/KeyFileGuard.h>

#include <xrpl/protocol/SecretKey.h>

#include <ValidatorKeys.h>
#include <ValidatorKeysTool.h>

#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl {

namespace tests {

class ValidatorKeysTool_test : public beast::unit_test::Suite
{
private:
    // Allow cout to be redirected.  Destructor restores old cout streambuf.
    class CoutRedirect
    {
    public:
        CoutRedirect(std::stringstream& sStream) : old_(std::cout.rdbuf(sStream.rdbuf()))
        {
        }

        ~CoutRedirect()
        {
            std::cout.rdbuf(old_);
        }

    private:
        std::streambuf* const old_;
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
                createToken(keyFile);
                BEAST_EXPECT(expectedError.empty());
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(std::string{e.what()} == expectedError);
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
                ValidatorKeys(keyType, kp.second, std::numeric_limits<std::uint32_t>::max() - 1);

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
        }
        catch (std::runtime_error const& e)
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
                    BEAST_EXPECT(std::string{e.what()} == expectedError);
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
                runCommand(command, args, keyFile);
                BEAST_EXPECT(expectedError.empty());
            }
            catch (std::exception const& e)
            {
                BEAST_EXPECT(std::string{e.what()} == expectedError);
            }
        };

        std::vector<std::string> const noArgs;
        std::vector<std::string> const oneArg = {"some data"};
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
    }

public:
    void
    run() override
    {
        getVersionString();

        testCreateKeyFile();
        testCreateToken();
        testCreateRevocation();
        testSign();
        testRunCommand();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorKeysTool, keys, xrpl);

}  // namespace tests

}  // namespace xrpl
