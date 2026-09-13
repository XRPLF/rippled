#include <tools/validator-keys/ValidatorKeysTool.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/unit_test/global_suites.h>
#include <xrpl/beast/unit_test/reporter.h>
#include <xrpl/json/json_reader.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/program_options.hpp>

#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>

#include <fstream>
#include <iostream>

//------------------------------------------------------------------------------
//  The build version number. You must edit this for each release
//  and follow the format described at http://semver.org/
//--------------------------------------------------------------------------
char const* const versionString =
    "0.4.0"

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

static int
runUnitTests()
{
    using namespace beast::unit_test;
    // Report on stderr: the tool tests capture stdout to check command output,
    // and a failure reported into that capture would never be seen.
    reporter r(std::cerr);
    bool const anyFailed = r.runEach(globalSuites());
    if (anyFailed)
        return EXIT_FAILURE;  // LCOV_EXCL_LINE
    return EXIT_SUCCESS;
}

namespace {

/**
 * Parses a public key given as base58, hex or base64.
 *
 * @throws std::runtime_error if none of the encodings yields a public key
 */
xrpl::PublicKey
parsePublicKey(std::string const& data)
{
    using namespace xrpl;

    if (auto const unBase58 = parseBase58<PublicKey>(TokenType::NodePublic, data))
        return *unBase58;

    if (auto const unHex = strUnHex(data))
    {
        auto const slice = makeSlice(*unHex);
        if (publicKeyType(slice))
            return PublicKey(slice);
    }

    {
        auto const unBase64 = base64Decode(data);
        auto const slice = makeSlice(unBase64);
        if (publicKeyType(slice))
            return PublicKey(slice);
    }

    throw std::runtime_error("Unable to parse public key: " + data);
}

/**
 * Decodes a signature given as hex or base64. There is no structural way to
 * check it other than trying to use it, so if the decoding succeeds, proceed.
 */
xrpl::Blob
decodeSignature(std::string const& data)
{
    using namespace xrpl;
    if (auto const unHex = strUnHex(data))
    {
        return *unHex;
    }

    // base64Decode decodes as far as it can and returns partial data for
    // invalid input, so re-encode the result and require a round trip.
    if (auto const unBase64 = base64Decode(data); base64Encode(unBase64) == data)
    {
        return Blob(unBase64.begin(), unBase64.end());
    }

    throw std::runtime_error("Invalid master signature");
}

// Writes a config block in 72-character lines, to the file when one is given
// (readable by the owner only, since a token holds a secret) or to stdout.
void
emitBlock(
    std::string const& section,
    std::string const& publicKey,
    std::string const& body,
    std::optional<boost::filesystem::path> const& outFile)
{
    std::ostringstream block;
    block << "# validator public key: " << publicKey << "\n\n";
    block << "[" << section << "]\n";
    auto const len = 72;
    for (std::size_t i = 0; i < body.size(); i += len)
        block << body.substr(i, len) << "\n";

    if (!outFile)
    {
        std::cout << "Update xrpld.cfg file with these values and restart xrpld:\n\n";
        std::cout << block.str() << std::endl;
        return;
    }

    using namespace boost::filesystem;
    std::ofstream o(outFile->string(), std::ios_base::trunc);
    if (o.fail())
        throw std::runtime_error("Cannot open output file: " + outFile->string());
    o << block.str();
    o.close();
    permissions(*outFile, owner_read | owner_write);
    std::cout << "[" << section << "] written to " << outFile->string() << "\n\n";
}

void
emitJson(json::Value const& jv, std::optional<boost::filesystem::path> const& outFile)
{
    if (!outFile)
    {
        std::cout << jv.toStyledString() << std::endl;
        return;
    }
    std::ofstream o(outFile->string(), std::ios_base::trunc);
    if (o.fail())
        throw std::runtime_error("Cannot open output file: " + outFile->string());
    o << jv.toStyledString();
    std::cout << "Written to " << outFile->string() << "\n";
}

json::Value
readJsonFile(boost::filesystem::path const& file)
{
    std::ifstream in(file.c_str(), std::ios::in);
    if (!in)
        throw std::runtime_error("Failed to open file: " + file.string());
    json::Reader reader;
    json::Value jv;
    if (!reader.parse(in, jv))
        throw std::runtime_error("Not a JSON document: " + file.string());
    return jv;
}

}  // namespace

void
createKeyFile(boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    if (exists(keyFile))
        throw std::runtime_error("Refusing to overwrite existing key file: " + keyFile.string());

    SigningKeys const keys(KeyType::Ed25519);
    keys.writeToFile(keyFile);

    std::cout << "Validator keys stored in " << keyFile.string()
              << "\n\nThis file should be stored securely and not shared.\n\n";
}

void
createExternal(std::string const& data, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    if (exists(keyFile))
        throw std::runtime_error("Refusing to overwrite existing key file: " + keyFile.string());

    auto const publicKey = parsePublicKey(data);

    SigningKeys const keys(*publicKeyType(publicKey), publicKey);
    keys.writeToFile(keyFile);

    std::cout << "Validator keys stored in " << keyFile.string()
              << "\n\nThis file should be stored securely and not shared.\n\n";
}

void
createToken(ToolOptions const& options)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(options.keyFile);

    if (keys.revoked())
        throw std::runtime_error("Validator keys have been revoked.");

    auto const token = keys.createValidatorToken(options.tokenKeyType);

    if (!token)
        throw std::runtime_error(
            "Maximum number of tokens have already been generated.\n"
            "Revoke validator keys if previous token has been compromised.");

    // Update key file with new token sequence
    keys.writeToFile(options.keyFile);

    emitBlock(
        "validator_token",
        toBase58(TokenType::NodePublic, keys.publicKey()),
        tokenToBase64(*token),
        options.outFile);
}

void
startToken(ToolOptions const& options)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(options.keyFile);

    if (keys.revoked())
        throw std::runtime_error("Validator keys have been revoked.");

    auto const token = keys.startValidatorToken(options.tokenKeyType, options.signingKey);

    if (!token)
        throw std::runtime_error(
            "Maximum number of tokens have already been generated.\n"
            "Revoke validator keys if previous token has been compromised.");

    // Update key file with the pending token
    keys.writeToFile(options.keyFile);

    std::cout << *token << std::endl;

    std::cout << std::endl;
}

void
finishToken(std::vector<std::string> const& signatures, ToolOptions const& options)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(options.keyFile);

    if (keys.revoked())
        throw std::runtime_error("Validator keys have been revoked.");

    auto const masterSig = decodeSignature(signatures.at(0));

    if (signatures.size() == 2)
    {
        auto const signingSig = decodeSignature(signatures.at(1));
        auto const manifest = keys.finishExternalToken(masterSig, signingSig);
        if (!manifest)
            throw std::runtime_error("Validator keys have been revoked.");

        keys.writeToFile(options.keyFile);

        emitBlock(
            "validator_manifest",
            toBase58(TokenType::NodePublic, keys.publicKey()),
            *manifest,
            options.outFile);
        return;
    }

    auto const token = keys.finishToken(masterSig);

    if (!token)
        throw std::runtime_error(
            "Maximum number of tokens have already been generated.\n"
            "Revoke validator keys if previous token has been compromised.");

    // Update key file with new token sequence
    keys.writeToFile(options.keyFile);

    emitBlock(
        "validator_token",
        toBase58(TokenType::NodePublic, keys.publicKey()),
        tokenToBase64(*token),
        options.outFile);
}

void
createRevocation(boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(keyFile);

    if (keys.revoked())
        std::cout << "WARNING: Validator keys have already been revoked!\n\n";
    else
        std::cout << "WARNING: This will revoke your validator keys!\n\n";

    auto const revocation = keys.revoke();

    // Update key file with new token sequence
    keys.writeToFile(keyFile);

    emitBlock(
        "validator_key_revocation",
        toBase58(TokenType::NodePublic, keys.publicKey()),
        revocation,
        std::nullopt);
}

void
startRevocation(boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(keyFile);

    if (keys.revoked())
        std::cerr << "WARNING: Validator keys have already been revoked!\n\n";
    else
        std::cerr << "WARNING: This will revoke your validator keys!\n\n";

    auto const revocation = keys.startRevoke();

    // Update key file with new token sequence
    keys.writeToFile(keyFile);

    std::cout << revocation << std::endl;

    std::cout << std::endl;
}

void
finishRevocation(std::string const& data, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(keyFile);

    if (keys.revoked())
        std::cout << "WARNING: Validator keys have already been revoked!\n\n";
    else
        std::cout << "WARNING: This will revoke your validator keys!\n\n";

    auto const masterSig = decodeSignature(data);

    auto const revocation = keys.finishRevoke(masterSig);

    // Update key file with new token sequence
    keys.writeToFile(keyFile);

    emitBlock(
        "validator_key_revocation",
        toBase58(TokenType::NodePublic, keys.publicKey()),
        revocation,
        std::nullopt);
}

void
attestDomain(xrpl::SigningKeys const& keys)
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

    auto keys = SigningKeys::make_SigningKeys(keyFile);

    if (keys.revoked())
        throw std::runtime_error("Operation error: The specified master key has been revoked!");

    attestDomain(keys);
}

void
setDomain(std::string const& domain, ToolOptions const& options)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(options.keyFile);

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
    auto const token = keys.createValidatorToken(options.tokenKeyType);
    if (!token)
        throw std::runtime_error(
            "Maximum number of tokens have already been generated.\n"
            "Revoke validator keys if previous token has been compromised.");

    // Flush to disk
    keys.writeToFile(options.keyFile);

    if (domain.empty())
        std::cout << "The domain name has been cleared.\n";
    else
        std::cout << "The domain name has been set to: " << domain << "\n\n";
    attestDomain(keys);

    std::cout << "\n";
    std::cout << "You also need to update the xrpld.cfg file to add a new\n";
    std::cout << "validator token and restart xrpld:\n\n";
    emitBlock(
        "validator_token",
        toBase58(TokenType::NodePublic, keys.publicKey()),
        tokenToBase64(*token),
        options.outFile);
}

void
signData(std::string const& data, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    if (data.empty())
        throw std::runtime_error("Syntax error: Must specify data string to sign");

    auto keys = SigningKeys::make_SigningKeys(keyFile);

    if (keys.revoked())
        std::cout << "WARNING: Validator keys have been revoked!\n\n";

    std::cout << keys.sign(data) << std::endl;
    std::cout << std::endl;
}

void
signHexData(std::string const& data, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    if (data.empty())
        throw std::runtime_error("Syntax error: Must specify data string to sign");

    auto keys = SigningKeys::make_SigningKeys(keyFile);

    if (keys.revoked())
        std::cout << "WARNING: Validator keys have been revoked!\n\n";

    std::cout << keys.signHex(data) << std::endl;
    std::cout << std::endl;
}

void
generateManifest(std::string const& type, boost::filesystem::path const& keyFile)
{
    using namespace xrpl;

    auto keys = SigningKeys::make_SigningKeys(keyFile);

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

void
signListFile(boost::filesystem::path const& unsignedList, ToolOptions const& options)
{
    using namespace xrpl;

    if (!options.tokenFile)
        throw std::runtime_error("sign_list needs --token-file");

    auto const token = loadTokenFile(*options.tokenFile);
    auto const manifest = deserializeManifest(base64Decode(token.manifest));
    if (!manifest || !manifest->verify() || manifest->revoked() || !manifest->signingKey)
        throw std::runtime_error("The token's manifest is not valid");

    auto const keyType = publicKeyType(*manifest->signingKey);
    if (!keyType || derivePublicKey(*keyType, token.validationSecret) != *manifest->signingKey)
        throw std::runtime_error("The token's secret does not match its manifest");

    auto const list = loadUnsignedList(unsignedList);
    auto const signature = signList(list, *manifest->signingKey, token.validationSecret);

    std::optional<json::Value> append;
    if (options.appendFile)
        append = readJsonFile(*options.appendFile);

    emitJson(
        makeSignedList(
            token.manifest, manifest->masterKey, list, signature, options.listVersion, append),
        options.outFile);
}

void
startSignList(boost::filesystem::path const& unsignedList, ToolOptions const& options)
{
    using namespace xrpl;

    if (!options.manifestFile)
        throw std::runtime_error("start_sign_list needs --manifest-file");

    // The manifest names the signing key; the bytes to sign are the list.
    auto const manifest = loadManifestFile(*options.manifestFile);
    if (manifest.revoked() || !manifest.signingKey)
        throw std::runtime_error("The manifest is revoked");

    auto const list = loadUnsignedList(unsignedList);
    std::cout << strHex(makeSlice(list.canonical)) << std::endl;
}

void
finishSignList(
    std::string const& signature,
    boost::filesystem::path const& unsignedList,
    ToolOptions const& options)
{
    using namespace xrpl;

    if (!options.manifestFile)
        throw std::runtime_error("finish_sign_list needs --manifest-file");

    auto const manifest = loadManifestFile(*options.manifestFile);
    if (manifest.revoked() || !manifest.signingKey)
        throw std::runtime_error("The manifest is revoked");

    auto const list = loadUnsignedList(unsignedList);
    auto const sig = decodeSignature(signature);
    if (!verify(*manifest.signingKey, makeSlice(list.canonical), makeSlice(sig)))
        throw std::runtime_error("The signature does not verify under the manifest's signing key");

    std::optional<json::Value> append;
    if (options.appendFile)
        append = readJsonFile(*options.appendFile);

    emitJson(
        makeSignedList(
            base64Encode(manifest.serialized),
            manifest.masterKey,
            list,
            strHex(sig),
            options.listVersion,
            append),
        options.outFile);
}

int
verifyListFile(boost::filesystem::path const& list, ToolOptions const& options)
{
    using namespace xrpl;

    std::optional<UnsignedList> roster;
    if (options.validatorsFile)
        roster = loadUnsignedList(*options.validatorsFile);

    auto const result =
        verifyList(readJsonFile(list), roster, options.expectedKey, rippleEpochNow());

    std::cout << result.report.toStyledString() << std::endl;
    return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
runCommand(
    std::string const& command,
    std::vector<std::string> const& args,
    ToolOptions const& options)
{
    using namespace std;

    // Minimum and maximum number of positional arguments per command.
    static map<string, pair<size_t, size_t>> const commandArgs = {
        {"create_keys", {0, 0}},
        {"create_token", {0, 0}},
        {"revoke_keys", {0, 0}},
        {"set_domain", {1, 1}},
        {"clear_domain", {0, 0}},
        {"attest_domain", {0, 0}},
        {"show_manifest", {1, 1}},
        {"sign", {1, 1}},
        {"sign_hex", {1, 1}},
        {"create_external", {1, 1}},
        {"start_token", {0, 0}},
        {"finish_token", {1, 2}},
        {"start_revoke_keys", {0, 0}},
        {"finish_revoke_keys", {1, 1}},
        {"sign_list", {1, 1}},
        {"start_sign_list", {1, 1}},
        {"finish_sign_list", {2, 2}},
        {"verify_list", {1, 1}},
    };

    auto const iArgs = commandArgs.find(command);

    if (iArgs == commandArgs.end())
        throw std::runtime_error("Unknown command: " + command);

    if (args.size() < iArgs->second.first || args.size() > iArgs->second.second)
        throw std::runtime_error("Syntax error: Wrong number of arguments");

    auto const& keyFile = options.keyFile;

    if (command == "create_keys")
        createKeyFile(keyFile);
    else if (command == "create_token")
        createToken(options);
    else if (command == "revoke_keys")
        createRevocation(keyFile);
    else if (command == "set_domain")
        setDomain(args[0], options);
    else if (command == "clear_domain")
        setDomain("", options);
    else if (command == "attest_domain")
        attestDomain(keyFile);
    else if (command == "sign")
        signData(args[0], keyFile);
    else if (command == "sign_hex")
        signHexData(args[0], keyFile);
    else if (command == "show_manifest")
        generateManifest(args[0], keyFile);
    else if (command == "create_external")
        createExternal(args[0], keyFile);
    else if (command == "start_token")
        startToken(options);
    else if (command == "finish_token")
        finishToken(args, options);
    else if (command == "start_revoke_keys")
        startRevocation(keyFile);
    else if (command == "finish_revoke_keys")
        finishRevocation(args[0], keyFile);
    else if (command == "sign_list")
        signListFile(args[0], options);
    else if (command == "start_sign_list")
        startSignList(args[0], options);
    else if (command == "finish_sign_list")
        finishSignList(args[0], args[1], options);
    else if (command == "verify_list")
        return verifyListFile(args[0], options);

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
                 "     start_token                   Generate a partial token for "
                 "external signing; --signing-key delegates to an external key.\n"
                 "     finish_token <master sig> [<signing sig>]\n"
                 "                                   Finish generating token with "
                 "external signature(s).\n"
                 "     start_revoke_keys             Generate a partial revocation "
                 "for external signing.\n"
                 "     finish_revoke_keys <sig>      Finish generating revocation "
                 "with external signature.\n"
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
        "token-key-type",
        po::value<std::string>(),
        "Key type of a token's signing key: secp256k1 (default) or ed25519.")(
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

        ToolOptions options;
        options.keyFile = vm.count("keyfile") ? vm["keyfile"].as<std::string>() : defaultKeyFile;

        if (vm.count("token-key-type"))
        {
            auto const keyType = xrpl::keyTypeFromString(vm["token-key-type"].as<std::string>());
            if (!keyType)
                throw std::runtime_error(
                    "Unknown key type: " + vm["token-key-type"].as<std::string>());
            options.tokenKeyType = *keyType;
        }
        if (vm.count("signing-key"))
            options.signingKey = parsePublicKey(vm["signing-key"].as<std::string>());
        if (vm.count("token-file"))
            options.tokenFile = path(vm["token-file"].as<std::string>());
        if (vm.count("manifest-file"))
            options.manifestFile = path(vm["manifest-file"].as<std::string>());
        if (vm.count("out"))
            options.outFile = path(vm["out"].as<std::string>());
        if (vm.count("list-version"))
            options.listVersion = vm["list-version"].as<unsigned>();
        if (vm.count("append"))
            options.appendFile = path(vm["append"].as<std::string>());
        if (vm.count("validators"))
            options.validatorsFile = path(vm["validators"].as<std::string>());
        if (vm.count("expected-key"))
            options.expectedKey = parsePublicKey(vm["expected-key"].as<std::string>());

        return runCommand(
            vm["command"].as<std::string>(),
            vm["arguments"].as<std::vector<std::string>>(),
            options);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
    // LCOV_EXCL_STOP
}
