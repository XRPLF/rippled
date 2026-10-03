#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/tokens.h>

#include <boost/program_options.hpp>

#include <tools/validator-keys/Commands.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// LCOV_EXCL_START
namespace {

std::string
getEnvVar(char const* name)
{
    auto const v = std::getenv(name);
    return v == nullptr ? std::string() : std::string(v);
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
                 "     sign_hex <data>               Decode and sign hex string with "
                 "validator key.\n"
                 "     show_manifest [hex|base64]    Displays the last generated "
                 "manifest\n"
                 "     set_domain <domain>           Associate a domain with the "
                 "validator key.\n"
                 "     clear_domain                  Disassociate a domain from a "
                 "validator key.\n"
                 "     attest_domain                 Produce the attestation string "
                 "for a domain.\n"
                 "Commands for signing externally: \n"
                 "     create_external <public key>  Generate validator keys without "
                 "a secret.\n"
                 "     start_token                   Print the bytes a token's "
                 "signatures cover; --signing-key delegates to an external key.\n"
                 "     finish_token <master sig> [<signing sig>]\n"
                 "                                   Finish the token with the "
                 "external signature(s).\n"
                 "     start_revoke_keys             Print the bytes a revocation's "
                 "signature covers.\n"
                 "     finish_revoke_keys <sig>      Finish the revocation with the "
                 "external signature.\n"
                 "Commands for validator lists: \n"
                 "     sign_list <unsigned list>     Sign a list with --token-file.\n"
                 "     start_sign_list <unsigned list>\n"
                 "                                   Print the bytes to sign with an "
                 "external signing key; needs --manifest-file.\n"
                 "     finish_sign_list <sig> <unsigned list>\n"
                 "                                   Assemble the signed list from an "
                 "external signature; needs --manifest-file.\n"
                 "     verify_list <list>            Check a published list; "
                 "--validators and --expected-key add checks.\n";
}

xrpl::PublicKey
publicKeyOption(std::string const& value)
{
    if (auto const key = xrpl::parseBase58<xrpl::PublicKey>(xrpl::TokenType::NodePublic, value))
        return *key;
    if (auto const bytes = xrpl::strUnHex(value);
        bytes && xrpl::publicKeyType(xrpl::makeSlice(*bytes)))
        return xrpl::PublicKey(xrpl::makeSlice(*bytes));
    throw std::runtime_error("Unable to parse public key: " + value);
}

}  // namespace

int
main(int argc, char** argv)
{
    namespace po = boost::program_options;
    using namespace xrpl::tools;

    po::options_description general("General Options");
    general.add_options()("help,h", "Display this message.")(
        "keyfile", po::value<std::string>(), "Specify the key file.")(
        "token-key-type",
        po::value<std::string>(),
        "Key type of a token's signing key: secp256k1 (default; the only type xrpld loads "
        "from [validator_token]) or ed25519 (for a publisher's signing key).")(
        "signing-key",
        po::value<std::string>(),
        "External signing key a token delegates to (start_token).")(
        "token-file", po::value<std::string>(), "File holding a [validator_token] block.")(
        "manifest-file", po::value<std::string>(), "File holding a base64 manifest.")(
        "out", po::value<std::string>(), "Write the token or signed list to this file.")(
        "list-version", po::value<unsigned>(), "Signed list version: 1 (default) or 2.")(
        "append", po::value<std::string>(), "Version 2 list to add the new blob to.")(
        "validators",
        po::value<std::string>(),
        "Unsigned list whose validators a published list must carry (verify_list).")(
        "expected-key",
        po::value<std::string>(),
        "Master key a published list must be signed under (verify_list).")(
        "version", "Display the build version.");

    po::options_description hidden("Hidden options");
    hidden.add_options()("command", po::value<std::string>(), "Command.")(
        "arguments",
        po::value<std::vector<std::string>>()->default_value(std::vector<std::string>(), "empty"),
        "Arguments.");
    po::positional_options_description positional;
    positional.add("command", 1).add("arguments", -1);

    po::options_description all;
    all.add(general).add(hidden);

    po::variables_map vm;
    try
    {
        po::store(
            po::command_line_parser(argc, argv).options(all).positional(positional).run(), vm);
        po::notify(vm);
    }
    catch (std::exception const&)
    {
        std::cerr << "validator-keys: Incorrect command line syntax." << std::endl;
        std::cerr << "Use '--help' for a list of options." << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.contains("version"))
    {
        std::cout << "validator-keys version " << getVersionString() << std::endl;
        return EXIT_SUCCESS;
    }

    if (vm.contains("help") || !vm.contains("command"))
    {
        printHelp(general);
        return EXIT_SUCCESS;
    }

    std::string const homeDir = getEnvVar("HOME");
    std::string const defaultKeyFile =
        (homeDir.empty() ? std::filesystem::current_path().string() : homeDir) +
        "/.ripple/validator-keys.json";

    try
    {
        ToolOptions options;
        options.keyFile = vm.contains("keyfile") ? vm["keyfile"].as<std::string>() : defaultKeyFile;

        if (vm.contains("token-key-type"))
        {
            auto const keyType = xrpl::keyTypeFromString(vm["token-key-type"].as<std::string>());
            if (!keyType)
            {
                throw std::runtime_error(
                    "Unknown key type: " + vm["token-key-type"].as<std::string>());
            }
            options.tokenKeyType = *keyType;
        }
        if (vm.contains("signing-key"))
            options.signingKey = publicKeyOption(vm["signing-key"].as<std::string>());
        if (vm.contains("token-file"))
            options.tokenFile = vm["token-file"].as<std::string>();
        if (vm.contains("manifest-file"))
            options.manifestFile = vm["manifest-file"].as<std::string>();
        if (vm.contains("out"))
            options.outFile = vm["out"].as<std::string>();
        if (vm.contains("list-version"))
            options.listVersion = vm["list-version"].as<unsigned>();
        if (vm.contains("append"))
            options.appendFile = vm["append"].as<std::string>();
        if (vm.contains("validators"))
            options.validatorsFile = vm["validators"].as<std::string>();
        if (vm.contains("expected-key"))
            options.expectedKey = publicKeyOption(vm["expected-key"].as<std::string>());

        return runCommand(
            vm["command"].as<std::string>(),
            vm["arguments"].as<std::vector<std::string>>(),
            options,
            std::cout,
            std::cerr);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
// LCOV_EXCL_STOP
