#include <tools/validator-keys/Commands.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/json/json_reader.h>

#include <boost/preprocessor/stringize.hpp>

#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>

#include <array>
#include <fstream>

namespace xrpl::tools {

namespace {

// The build version number: edit for each release, in semantic version form.
char const* const kVersionString =
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
        ;

constexpr std::size_t kMaxDocumentBytes = 4 * 1024 * 1024;
constexpr std::size_t kBlockLineLength = 72;

using Args = std::vector<std::string>;

struct Context
{
    ToolOptions const& options;
    std::ostream& out;
    std::ostream& err;
};

/**
 * Where a command's result goes: the output file named by `--out`, opened
 * before the command changes any state so an unwritable path fails first, or
 * the output stream. A file that receives nothing is removed again.
 */
class Output
{
    std::optional<std::filesystem::path> file_;
    std::ofstream stream_;
    std::ostream& out_;
    bool written_ = false;

public:
    Output(std::optional<std::filesystem::path> const& file, std::ostream& out)
        : file_(file), out_(out)
    {
        if (file_)
        {
            if (std::filesystem::is_symlink(*file_))
                throw std::runtime_error("Refusing to write through a symlink: " + file_->string());
            stream_.open(*file_, std::ios_base::trunc);
            if (stream_.fail())
                throw std::runtime_error("Cannot open output file: " + file_->string());
            // A token holds a secret: restrict the file before anything is written.
            std::error_code ec;
            std::filesystem::permissions(
                *file_,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                ec);
        }
    }

    ~Output()
    {
        if (file_ && !written_)
        {
            stream_.close();
            std::error_code ec;
            std::filesystem::remove(*file_, ec);
        }
    }

    Output(Output const&) = delete;
    Output&
    operator=(Output const&) = delete;

    // A config block in 72-character lines.
    void
    block(std::string const& section, std::string const& publicKey, std::string const& body)
    {
        std::string text = "# validator public key: " + publicKey + "\n\n[" + section + "]\n";
        for (std::size_t i = 0; i < body.size(); i += kBlockLineLength)
            text.append(body, i, kBlockLineLength).push_back('\n');

        if (!file_)
        {
            out_ << "Update xrpld.cfg file with these values and restart xrpld:\n\n"
                 << text << std::endl;
            return;
        }
        write(text, "[" + section + "]");
    }

    void
    json(json::Value const& jv)
    {
        if (!file_)
        {
            out_ << jv.toStyledString() << std::endl;
            return;
        }
        write(jv.toStyledString(), "The list");
    }

private:
    void
    write(std::string const& text, std::string const& what)
    {
        stream_ << text;
        stream_.close();
        if (stream_.fail())
            throw std::runtime_error(  // LCOV_EXCL_LINE
                "Cannot write output file: " + file_->string());  // LCOV_EXCL_LINE
        written_ = true;
        out_ << what << " written to " << file_->string() << "\n";
    }
};

/**
 * Parses a public key given as base58, hex or base64.
 *
 * @throws std::runtime_error if none of the encodings yields a public key
 */
PublicKey
parsePublicKey(std::string const& data)
{
    if (auto const key = parseBase58<PublicKey>(TokenType::NodePublic, data))
        return *key;
    if (auto const key = parseHexKey(data))
        return *key;
    if (auto const bytes = base64Decode(data); publicKeyType(makeSlice(bytes)))
        return PublicKey(makeSlice(bytes));
    throw std::runtime_error("Unable to parse public key: " + data);
}

/**
 * Decodes a signature given as hex or base64. Only using it shows whether it
 * is right, so any string that decodes is accepted.
 */
Blob
decodeSignature(std::string const& data)
{
    if (auto const bytes = strUnHex(data))
        return *bytes;
    // base64Decode returns partial data for invalid input, so require a round trip.
    if (auto const bytes = base64Decode(data); base64Encode(bytes) == data)
        return Blob(bytes.begin(), bytes.end());
    throw std::runtime_error("Invalid signature encoding");
}

json::Value
readJsonFile(std::filesystem::path const& file)
{
    std::error_code ec;
    auto const text = getFileContents(ec, file, kMaxDocumentBytes);
    if (ec)
        throw std::runtime_error("Failed to open file: " + file.string());
    json::Reader reader;
    json::Value jv;
    if (!reader.parse(text, jv))
        throw std::runtime_error("Not a JSON document: " + file.string());
    return jv;
}

std::string
nodePublic(SigningKeys const& keys)
{
    return toBase58(TokenType::NodePublic, keys.publicKey());
}

void
refuseExisting(std::filesystem::path const& keyFile)
{
    if (std::filesystem::exists(keyFile))
        throw std::runtime_error("Refusing to overwrite existing key file: " + keyFile.string());
}

void
storedNotice(std::filesystem::path const& keyFile, std::ostream& out)
{
    out << "Validator keys stored in " << keyFile.string()
        << "\n\nThis file should be stored securely and not shared.\n\n";
}

SigningKeys
loadUnrevoked(std::filesystem::path const& keyFile)
{
    auto keys = SigningKeys::make_SigningKeys(keyFile);
    if (keys.revoked())
        throw std::runtime_error("Operation error: The specified master key has been revoked!");
    return keys;
}

void
warnRevocation(SigningKeys const& keys, std::ostream& err)
{
    if (keys.revoked())
        err << "WARNING: Validator keys have already been revoked!\n\n";
    else
        err << "WARNING: This will revoke your validator keys!\n\n";
}

void
emitFinished(SigningKeys const& keys, SigningKeys::Finished const& finished, Output& output)
{
    if (finished.secret)
        output.block(
            "validator_token",
            nodePublic(keys),
            tokenToBase64(ValidatorToken{finished.manifest, *finished.secret}));
    else
        output.block("validator_manifest", nodePublic(keys), finished.manifest);
}

void
emitAttestation(SigningKeys const& keys, Context& ctx)
{
    if (keys.domain().empty())
    {
        ctx.out << "No attestation is necessary if no domain is specified!\n"
                   "If you have an attestation in your xrpl-ledger.toml\n"
                   "you should remove it at this time.\n";
        return;
    }
    if (!keys.hasSecret())
    {
        ctx.err << "Sign these bytes with the master key; the hex signature is the\n"
                   "attestation for xrp-ledger.toml.\n\n";
        ctx.out << strHex(makeSlice(keys.attestationData())) << std::endl;
        return;
    }
    ctx.out << "The domain attestation for validator " << nodePublic(keys) << " is:\n\n"
            << "attestation=\"" << keys.sign(keys.attestationData()) << "\"\n\n"
            << "You should include it in your xrp-ledger.toml file in the\n"
               "section for this validator.\n";
}

int
cmdCreateKeys(Args const&, Context& ctx)
{
    refuseExisting(ctx.options.keyFile);
    SigningKeys const keys(KeyType::Ed25519);
    keys.writeToFile(ctx.options.keyFile);
    storedNotice(ctx.options.keyFile, ctx.out);
    return EXIT_SUCCESS;
}

int
cmdCreateExternal(Args const& args, Context& ctx)
{
    refuseExisting(ctx.options.keyFile);
    auto const publicKey = parsePublicKey(args[0]);
    SigningKeys const keys(*publicKeyType(publicKey), publicKey);
    keys.writeToFile(ctx.options.keyFile);
    storedNotice(ctx.options.keyFile, ctx.out);
    return EXIT_SUCCESS;
}

int
cmdCreateToken(Args const&, Context& ctx)
{
    Output output(ctx.options.outFile, ctx.out);
    auto keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    auto const token = keys.createToken(ctx.options.tokenKeyType);
    keys.writeToFile(ctx.options.keyFile);
    output.block("validator_token", nodePublic(keys), tokenToBase64(token));
    return EXIT_SUCCESS;
}

int
cmdStartToken(Args const&, Context& ctx)
{
    auto keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    auto const data = keys.startToken(ctx.options.tokenKeyType, ctx.options.signingKey);
    keys.writeToFile(ctx.options.keyFile);
    ctx.out << data << std::endl;
    return EXIT_SUCCESS;
}

int
cmdFinishToken(Args const& args, Context& ctx)
{
    Output output(ctx.options.outFile, ctx.out);
    auto keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    std::optional<Blob> signingSig;
    if (args.size() == 2)
        signingSig = decodeSignature(args[1]);
    auto const finished = keys.finishToken(decodeSignature(args[0]), signingSig);
    keys.writeToFile(ctx.options.keyFile);
    emitFinished(keys, finished, output);
    return EXIT_SUCCESS;
}

int
cmdRevokeKeys(Args const&, Context& ctx)
{
    auto keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    warnRevocation(keys, ctx.err);
    auto const revocation = keys.revoke();
    keys.writeToFile(ctx.options.keyFile);
    Output(std::nullopt, ctx.out).block("validator_key_revocation", nodePublic(keys), revocation);
    return EXIT_SUCCESS;
}

int
cmdStartRevokeKeys(Args const&, Context& ctx)
{
    auto const keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    warnRevocation(keys, ctx.err);
    ctx.out << keys.startRevoke() << std::endl;
    return EXIT_SUCCESS;
}

int
cmdFinishRevokeKeys(Args const& args, Context& ctx)
{
    auto keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    warnRevocation(keys, ctx.err);
    auto const revocation = keys.finishRevoke(decodeSignature(args[0]));
    keys.writeToFile(ctx.options.keyFile);
    Output(std::nullopt, ctx.out).block("validator_key_revocation", nodePublic(keys), revocation);
    return EXIT_SUCCESS;
}

int
setDomain(std::string const& domain, Context& ctx)
{
    Output output(ctx.options.outFile, ctx.out);
    auto keys = loadUnrevoked(ctx.options.keyFile);

    if (domain == keys.domain())
    {
        ctx.out
            << (domain.empty() ? "The domain name was already cleared!\n"
                               : "The domain name was already set.\n");
        return EXIT_SUCCESS;
    }

    keys.domain(domain);
    if (!keys.hasSecret())
    {
        keys.writeToFile(ctx.options.keyFile);
        ctx.out << (domain.empty() ? "The domain name has been cleared.\n"
                                   : "The domain name has been set to: " + domain + "\n")
                << "The next token carries it: run start_token and finish_token.\n";
        return EXIT_SUCCESS;
    }

    auto const token = keys.createToken(ctx.options.tokenKeyType);
    keys.writeToFile(ctx.options.keyFile);

    ctx.out
        << (domain.empty() ? "The domain name has been cleared.\n"
                           : "The domain name has been set to: " + domain + "\n\n");
    emitAttestation(keys, ctx);
    ctx.out << "\nYou also need to update the xrpld.cfg file to add a new\n"
               "validator token and restart xrpld:\n\n";
    output.block("validator_token", nodePublic(keys), tokenToBase64(token));
    return EXIT_SUCCESS;
}

int
cmdSetDomain(Args const& args, Context& ctx)
{
    return setDomain(args[0], ctx);
}

int
cmdClearDomain(Args const&, Context& ctx)
{
    return setDomain("", ctx);
}

int
cmdAttestDomain(Args const&, Context& ctx)
{
    emitAttestation(loadUnrevoked(ctx.options.keyFile), ctx);
    return EXIT_SUCCESS;
}

int
sign(std::string const& data, bool hex, Context& ctx)
{
    if (data.empty())
        throw std::runtime_error("Syntax error: Must specify data string to sign");
    auto const keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    if (keys.revoked())
        ctx.err << "WARNING: Validator keys have been revoked!\n\n";
    ctx.out << (hex ? keys.signHex(data) : keys.sign(data)) << std::endl;
    return EXIT_SUCCESS;
}

int
cmdSign(Args const& args, Context& ctx)
{
    return sign(args[0], false, ctx);
}

int
cmdSignHex(Args const& args, Context& ctx)
{
    return sign(args[0], true, ctx);
}

int
cmdShowManifest(Args const& args, Context& ctx)
{
    auto const keys = SigningKeys::make_SigningKeys(ctx.options.keyFile);
    auto const& m = keys.manifest();
    if (m.empty())
    {
        ctx.out << "The last manifest generated is unavailable. You can\n"
                   "generate a new one.\n\n";
        return EXIT_SUCCESS;
    }
    if (args[0] == "base64")
    {
        ctx.out << "Manifest #" << keys.sequence() << " (Base64):\n"
                << base64Encode(m.data(), m.size()) << "\n\n";
        return EXIT_SUCCESS;
    }
    if (args[0] == "hex")
    {
        ctx.out << "Manifest #" << keys.sequence() << " (Hex):\n" << strHex(makeSlice(m)) << "\n\n";
        return EXIT_SUCCESS;
    }
    throw std::runtime_error("Unknown encoding '" + args[0] + "'");
}

// The manifest a list is signed under when the signing key is external.
Manifest
loadSigningManifest(Context const& ctx, char const* command)
{
    if (!ctx.options.manifestFile)
        throw std::runtime_error(std::string(command) + " needs --manifest-file");
    auto manifest = loadManifestFile(*ctx.options.manifestFile);
    if (manifest.revoked() || !manifest.signingKey)
        throw std::runtime_error("The manifest is revoked");
    return manifest;
}

void
emitSignedList(
    std::string const& manifestBase64,
    PublicKey const& masterKey,
    UnsignedList const& list,
    std::string const& signatureHex,
    Resigner const& resign,
    Context& ctx,
    Output& output)
{
    std::optional<json::Value> append;
    if (ctx.options.appendFile)
        append = readJsonFile(*ctx.options.appendFile);
    output.json(makeSignedList(
        manifestBase64, masterKey, list, signatureHex, ctx.options.listVersion, append, resign));
}

int
cmdSignList(Args const& args, Context& ctx)
{
    if (!ctx.options.tokenFile)
        throw std::runtime_error("sign_list needs --token-file");
    Output output(ctx.options.outFile, ctx.out);

    auto const token = loadTokenFile(*ctx.options.tokenFile);
    auto const manifest = deserializeManifest(base64Decode(token.manifest));
    if (!manifest || !manifest->verify() || manifest->revoked() || !manifest->signingKey)
        throw std::runtime_error("The token's manifest is not valid");

    auto const signingKey = *manifest->signingKey;
    auto const keyType = publicKeyType(signingKey);
    if (!keyType || derivePublicKey(*keyType, token.validationSecret) != signingKey)
        throw std::runtime_error("The token's secret does not match its manifest");

    auto const list = loadUnsignedList(args[0]);
    auto const resign = [&](std::string const& blobBytes) {
        return strHex(xrpl::sign(signingKey, token.validationSecret, makeSlice(blobBytes)));
    };
    emitSignedList(
        token.manifest, manifest->masterKey, list, resign(list.canonical), resign, ctx, output);
    return EXIT_SUCCESS;
}

int
cmdStartSignList(Args const& args, Context& ctx)
{
    loadSigningManifest(ctx, "start_sign_list");
    auto const list = loadUnsignedList(args[0]);
    ctx.out << strHex(makeSlice(list.canonical)) << std::endl;
    return EXIT_SUCCESS;
}

int
cmdFinishSignList(Args const& args, Context& ctx)
{
    auto const manifest = loadSigningManifest(ctx, "finish_sign_list");
    Output output(ctx.options.outFile, ctx.out);

    auto const list = loadUnsignedList(args[1]);
    auto const sig = decodeSignature(args[0]);
    if (!verify(*manifest.signingKey, makeSlice(list.canonical), makeSlice(sig)))
        throw std::runtime_error("The signature does not verify under the manifest's signing key");

    emitSignedList(
        base64Encode(manifest.serialized), manifest.masterKey, list, strHex(sig), {}, ctx, output);
    return EXIT_SUCCESS;
}

int
cmdVerifyList(Args const& args, Context& ctx)
{
    std::optional<UnsignedList> roster;
    if (ctx.options.validatorsFile)
        roster = loadUnsignedList(*ctx.options.validatorsFile);

    auto const report =
        verifyList(readJsonFile(args[0]), roster, ctx.options.expectedKey, netClockNow());
    ctx.out << report.toStyledString() << std::endl;
    return report["ok"].asBool() ? EXIT_SUCCESS : EXIT_FAILURE;
}

struct Command
{
    char const* name;
    std::size_t minArgs;
    std::size_t maxArgs;
    int (*run)(Args const&, Context&);
};

constexpr std::array<Command, 18> kCommands{{
    {"create_keys", 0, 0, cmdCreateKeys},
    {"create_external", 1, 1, cmdCreateExternal},
    {"create_token", 0, 0, cmdCreateToken},
    {"start_token", 0, 0, cmdStartToken},
    {"finish_token", 1, 2, cmdFinishToken},
    {"revoke_keys", 0, 0, cmdRevokeKeys},
    {"start_revoke_keys", 0, 0, cmdStartRevokeKeys},
    {"finish_revoke_keys", 1, 1, cmdFinishRevokeKeys},
    {"set_domain", 1, 1, cmdSetDomain},
    {"clear_domain", 0, 0, cmdClearDomain},
    {"attest_domain", 0, 0, cmdAttestDomain},
    {"sign", 1, 1, cmdSign},
    {"sign_hex", 1, 1, cmdSignHex},
    {"show_manifest", 1, 1, cmdShowManifest},
    {"sign_list", 1, 1, cmdSignList},
    {"start_sign_list", 1, 1, cmdStartSignList},
    {"finish_sign_list", 2, 2, cmdFinishSignList},
    {"verify_list", 1, 1, cmdVerifyList},
}};

}  // namespace

std::string const&
getVersionString()
{
    static std::string const kValue = [] {
        std::string const s = kVersionString;
        beast::SemanticVersion v;
        if (!v.parse(s) || v.print() != s)
            throw std::logic_error(s + ": Bad version string");  // LCOV_EXCL_LINE
        return s;
    }();
    return kValue;
}

int
runCommand(
    std::string const& command,
    std::vector<std::string> const& args,
    ToolOptions const& options,
    std::ostream& out,
    std::ostream& err)
{
    auto const it = std::find_if(
        kCommands.begin(), kCommands.end(), [&](Command const& c) { return command == c.name; });
    if (it == kCommands.end())
        throw std::runtime_error("Unknown command: " + command);
    if (args.size() < it->minArgs || args.size() > it->maxArgs)
        throw std::runtime_error("Syntax error: Wrong number of arguments");

    Context ctx{options, out, err};
    return it->run(args, ctx);
}

}  // namespace xrpl::tools
