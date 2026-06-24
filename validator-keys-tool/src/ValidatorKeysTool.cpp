#include <ValidatorKeysTool.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/beast/unit_test.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/program_options.hpp>

#include <ValidatorKeys.h>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>

//------------------------------------------------------------------------------
//  The build version number. You must edit this for each release
//  and follow the format described at http://semver.org/
//--------------------------------------------------------------------------
char const* const versionString =
    "0.3.2"

#if defined(DEBUG) || defined(SANITIZER)
    "+"
#ifdef DEBUG
    "DEBUG"
#ifdef SANITIZER
    "."
#endif
#endif

#ifdef SANITIZER
    BOOST_PP_STRINGIZE(SANITIZER)
#endif
#endif

    //--------------------------------------------------------------------------
    ;

static constexpr std::size_t kConfigLineLength = 72;

static int
runUnitTests()
{
    using namespace beast::unit_test;
    reporter r;
    bool const anyFailed = r.runEach(globalSuites());
    if (anyFailed)
        return EXIT_FAILURE;  // LCOV_EXCL_LINE
    return EXIT_SUCCESS;
}

void
createKeyFile(boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    if (exists(keyFile))
        throw std::runtime_error("Refusing to overwrite existing key file: " + keyFile.string());

    ValidatorKeys const keys(KeyType::Ed25519);
    keys.writeToFile(keyFile);

    std::cout << "Validator keys stored in " << keyFile.string()
              << "\n\nThis file should be stored securely and not shared.\n\n";
}

void
createToken(boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = ValidatorKeys::make_ValidatorKeys(keyFile);

    if (keys.revoked())
        throw std::runtime_error("Validator keys have been revoked.");

    auto const token = keys.createValidatorToken();

    if (!token)
        throw std::runtime_error(
            "Maximum number of tokens have already been generated.\n"
            "Revoke validator keys if previous token has been compromised.");

    // Update key file with new token sequence
    keys.writeToFile(keyFile);

    std::cout << "Update xrpld.cfg file with these values and restart xrpld:\n\n";
    std::cout << "# validator public key: " << toBase58(TokenType::NodePublic, keys.publicKey())
              << "\n\n";
    std::cout << "[validator_token]\n";

    auto const tokenStr = token->toString();
    for (std::size_t i = 0; i < tokenStr.size(); i += kConfigLineLength)
        std::cout << tokenStr.substr(i, kConfigLineLength) << std::endl;

    std::cout << std::endl;
}

void
createRevocation(boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = ValidatorKeys::make_ValidatorKeys(keyFile);

    if (keys.revoked())
        std::cout << "WARNING: Validator keys have already been revoked!\n\n";
    else
        std::cout << "WARNING: This will revoke your validator keys!\n\n";

    auto const revocation = keys.revoke();

    // Update key file with new token sequence
    keys.writeToFile(keyFile);

    std::cout << "Update xrpld.cfg file with these values and restart xrpld:\n\n";
    std::cout << "# validator public key: " << toBase58(TokenType::NodePublic, keys.publicKey())
              << "\n\n";
    std::cout << "[validator_key_revocation]\n";

    for (std::size_t i = 0; i < revocation.size(); i += kConfigLineLength)
        std::cout << revocation.substr(i, kConfigLineLength) << std::endl;

    std::cout << std::endl;
}

void
attestDomain(xrpl::ValidatorKeys const& keys)
{
    using namespace xrpl;

    if (keys.domain().empty())
    {
        std::cout << "No attestation is necessary if no domain is specified!\n";
        std::cout << "If you have an attestation in your xrpl-ledger.toml\n";
        std::cout << "you should remove it at this time.\n";
        return;
    }

    std::cout << "The domain attestation for validator "
              << toBase58(TokenType::NodePublic, keys.publicKey()) << " is:\n\n";

    std::cout << "attestation=\""
              << keys.sign(
                     "[domain-attestation-blob:" + keys.domain() + ":" +
                     toBase58(TokenType::NodePublic, keys.publicKey()) + "]")
              << "\"\n\n";

    std::cout << "You should include it in your xrp-ledger.toml file in the\n";
    std::cout << "section for this validator.\n";
}

void
attestDomain(boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = ValidatorKeys::make_ValidatorKeys(keyFile);

    if (keys.revoked())
        throw std::runtime_error("Operation error: The specified master key has been revoked!");

    attestDomain(keys);
}

void
setDomain(std::string const& domain, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = ValidatorKeys::make_ValidatorKeys(keyFile);

    if (keys.revoked())
        throw std::runtime_error("Operation error: The specified master key has been revoked!");

    if (domain == keys.domain())
    {
        if (domain.empty())
            std::cout << "The domain name was already cleared!\n";
        else
            std::cout << "The domain name was already set.\n";
        return;
    }

    // Set the domain and generate a new token
    keys.domain(domain);
    auto const token = keys.createValidatorToken();
    if (!token)
        throw std::runtime_error(
            "Maximum number of tokens have already been generated.\n"
            "Revoke validator keys if previous token has been compromised.");

    // Flush to disk
    keys.writeToFile(keyFile);

    if (domain.empty())
        std::cout << "The domain name has been cleared.\n";
    else
        std::cout << "The domain name has been set to: " << domain << "\n\n";
    attestDomain(keys);

    std::cout << "\n";
    std::cout << "You also need to update the xrpld.cfg file to add a new\n";
    std::cout << "validator token and restart xrpld:\n\n";
    std::cout << "# validator public key: " << toBase58(TokenType::NodePublic, keys.publicKey())
              << "\n\n";
    std::cout << "[validator_token]\n";

    auto const tokenStr = token->toString();
    for (std::size_t i = 0; i < tokenStr.size(); i += kConfigLineLength)
        std::cout << tokenStr.substr(i, kConfigLineLength) << std::endl;

    std::cout << "\n";
}

void
signData(std::string const& data, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    if (data.empty())
        throw std::runtime_error("Syntax error: Must specify data string to sign");

    auto keys = ValidatorKeys::make_ValidatorKeys(keyFile);

    if (keys.revoked())
        std::cout << "WARNING: Validator keys have been revoked!\n\n";

    std::cout << keys.sign(data) << std::endl;
    std::cout << std::endl;
}

void
generateManifest(std::string const& type, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = ValidatorKeys::make_ValidatorKeys(keyFile);

    auto const m = keys.manifest();

    if (m.empty())
    {
        std::cout << "The last manifest generated is unavailable. You can\n";
        std::cout << "generate a new one.\n\n";
        return;
    }

    if (type == "base64")
    {
        std::cout << "Manifest #" << keys.sequence() << " (Base64):\n";
        std::cout << base64Encode(m.data(), m.size()) << "\n\n";
        return;
    }

    if (type == "hex")
    {
        std::cout << "Manifest #" << keys.sequence() << " (Hex):\n";
        std::cout << strHex(makeSlice(m)) << "\n\n";
        return;
    }

    std::cout << "Unknown encoding '" << type << "'\n";
}

int
runCommand(
    std::string const& command,
    std::vector<std::string> const& args,
    boost::filesystem::path const& keyFile)
{
    using namespace std;

    static map<string, vector<string>::size_type> const commandArgs = {
        {"create_keys", 0},
        {"create_token", 0},
        {"revoke_keys", 0},
        {"set_domain", 1},
        {"clear_domain", 0},
        {"attest_domain", 0},
        {"show_manifest", 1},
        {"sign", 1},
    };

    auto const iArgs = commandArgs.find(command);

    if (iArgs == commandArgs.end())
        throw std::runtime_error("Unknown command: " + command);

    if (args.size() != iArgs->second)
        throw std::runtime_error("Syntax error: Wrong number of arguments");

    if (command == "create_keys")
        createKeyFile(keyFile);
    else if (command == "create_token")
        createToken(keyFile);
    else if (command == "revoke_keys")
        createRevocation(keyFile);
    else if (command == "set_domain")
        setDomain(args[0], keyFile);
    else if (command == "clear_domain")
        setDomain("", keyFile);
    else if (command == "attest_domain")
        attestDomain(keyFile);
    else if (command == "sign")
        signData(args[0], keyFile);
    else if (command == "show_manifest")
        generateManifest(args[0], keyFile);

    return 0;
}

// LCOV_EXCL_START
static std::string
getEnvVar(char const* name)
{
    std::string value;

    auto const v = getenv(name);

    if (v != nullptr)
        value = v;

    return value;
}

void
printHelp(boost::program_options::options_description const& desc)
{
    std::cerr << "validator-keys [options] <command> [<argument> ...]\n"
              << desc << std::endl
              << "Commands: \n"
                 "     create_keys                   Generate validator keys.\n"
                 "     create_token                  Generate validator token.\n"
                 "     revoke_keys                   Revoke validator keys.\n"
                 "     sign <data>                   Sign string with validator "
                 "key.\n"
                 "     show_manifest [hex|base64]    Displays the last generated "
                 "manifest\n"
                 "     set_domain <domain>           Associate a domain with the "
                 "validator key.\n"
                 "     clear_domain                  Disassociate a domain from a "
                 "validator key.\n"
                 "     attest_domain                 Produce the attestation string "
                 "for a domain.\n";
}
// LCOV_EXCL_STOP

std::string const&
getVersionString()
{
    static std::string const value = [] {
        std::string const s = versionString;
        beast::SemanticVersion v;
        if (!v.parse(s) || v.print() != s)
            throw std::logic_error(s + ": Bad version string");  // LCOV_EXCL_LINE
        return s;
    }();
    return value;
}

int
main(int argc, char** argv)
{
    namespace po = boost::program_options;

    po::variables_map vm;

    // Set up option parsing.
    //
    po::options_description general("General Options");
    general.add_options()("help,h", "Display this message.")(
        "keyfile", po::value<std::string>(), "Specify the key file.")(
        "unittest,u", "Perform unit tests.")("version", "Display the build version.");

    po::options_description hidden("Hidden options");
    hidden.add_options()("command", po::value<std::string>(), "Command.")(
        "arguments",
        po::value<std::vector<std::string>>()->default_value(std::vector<std::string>(), "empty"),
        "Arguments.");
    po::positional_options_description p;
    p.add("command", 1).add("arguments", -1);

    po::options_description cmdline_options;
    cmdline_options.add(general).add(hidden);

    // Parse options, if no error.
    try
    {
        po::store(
            po::command_line_parser(argc, argv)
                .options(cmdline_options)  // Parse options.
                .positional(p)
                .run(),
            vm);
        po::notify(vm);  // Invoke option notify functions.
    }
    // LCOV_EXCL_START
    catch (std::exception const&)
    {
        std::cerr << "validator-keys: Incorrect command line syntax." << std::endl;
        std::cerr << "Use '--help' for a list of options." << std::endl;
        return EXIT_FAILURE;
    }
    // LCOV_EXCL_STOP

    // Run the unit tests if requested.
    // The unit tests will exit the application with an appropriate return code.
    if (vm.count("unittest"))
        return runUnitTests();

    // LCOV_EXCL_START
    if (vm.count("version"))
    {
        std::cout << "validator-keys version " << getVersionString() << std::endl;
        return 0;
    }

    if (vm.count("help") || !vm.count("command"))
    {
        printHelp(general);
        return EXIT_SUCCESS;
    }

    std::string const homeDir = getEnvVar("HOME");
    std::string const defaultKeyFile =
        (homeDir.empty() ? boost::filesystem::current_path().string() : homeDir) +
        "/.ripple/validator-keys.json";

    try
    {
        using namespace boost::filesystem;
        path keyFile = vm.count("keyfile") ? vm["keyfile"].as<std::string>() : defaultKeyFile;

        return runCommand(
            vm["command"].as<std::string>(),
            vm["arguments"].as<std::vector<std::string>>(),
            keyFile);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
    // LCOV_EXCL_STOP
}
