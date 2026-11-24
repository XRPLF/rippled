#include <tools/validator-keys/Commands.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/Manifest.h>

#include <boost/algorithm/string/trim.hpp>

#include <sys/file.h>
#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/OwnerOnlyFile.h>
#include <tools/validator-keys/SigningKeys.h>

#include <dirent.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace xrpl::tools {

namespace {

constexpr std::size_t kMaxDocumentBytes = 4 * 1024 * 1024;
constexpr std::size_t kBlockLineLength = 72;
constexpr std::string_view kForPublishers =
    "This token's signing key is ed25519: it is for sign_list --token-file; "
    "xrpld loads only secp256k1 validator tokens";

using Args = std::vector<std::string>;

struct Context
{
    ToolOptions const& options;
    std::ostream& out;
    std::ostream& err;
};

// An exclusive flock on the key file's directory, held while a command loads,
// changes and rewrites the key file; OwnerOnlyFile::commit renames a new inode
// over the key file, so a lock on the file itself would not serialize.
class KeyFileLock
{
    // opendir gives a read-only, close-on-exec descriptor of the directory;
    // null when the directory does not exist.
    DIR* dir_ = nullptr;

public:
    explicit KeyFileLock(std::filesystem::path const& keyFile)
    {
        auto dir = keyFile.parent_path();
        if (dir.empty())
            dir = ".";
        dir_ = ::opendir(dir.c_str());
        if (dir_ == nullptr)
        {
            // No directory means no key file to protect; the command reports
            // the missing key file itself.
            if (errno == ENOENT || errno == ENOTDIR)
                return;
            throw std::runtime_error("Cannot lock key file: " + keyFile.string());
        }
        int rc = 0;
        do
        {
            rc = ::flock(::dirfd(dir_), LOCK_EX);
        } while (rc != 0 && errno == EINTR);
        if (rc != 0)
        {
            ::closedir(dir_);
            throw std::runtime_error("Cannot lock key file: " + keyFile.string());
        }
    }

    ~KeyFileLock()
    {
        if (dir_ != nullptr)
            ::closedir(dir_);
    }

    KeyFileLock(KeyFileLock const&) = delete;
    KeyFileLock&
    operator=(KeyFileLock const&) = delete;
};

/**
 * Where a command's result goes: the file named by `--out`, replaced whole
 * once the command has succeeded, or the output stream. The file is prepared
 * before the command changes any state so an unwritable path fails first.
 */
class Output
{
    // Null when the result goes to the output stream.
    std::unique_ptr<OwnerOnlyFile> file_;
    std::ostream& out_;

public:
    /**
     * @param input A file the command reads besides those in the options;
     *        `--out` may not name any input except `--append`, whose document
     *        the output replaces once it is complete.
     * @param mode The file's mode: `OwnerOnlyFile::kPublished` for a list
     */
    Output(
        Context const& ctx,
        std::optional<std::filesystem::path> const& input = std::nullopt,
        int mode = OwnerOnlyFile::kOwnerOnly)
        : out_(ctx.out)
    {
        auto const& options = ctx.options;
        if (!options.outFile)
            return;

        std::vector<std::filesystem::path> read{options.keyFile};
        if (input)
            read.push_back(*input);
        for (auto const* file :
             {&options.tokenFile, &options.manifestFile, &options.validatorsFile})
        {
            if (*file)
                read.push_back(**file);
        }
        for (auto const& file : read)
        {
            std::error_code ec;
            if (std::filesystem::equivalent(file, *options.outFile, ec))
                throw std::runtime_error("--out names an input file: " + options.outFile->string());
        }

        file_ = std::make_unique<OwnerOnlyFile>(
            *options.outFile, "output file", OwnerOnlyFile::Existing::Replace, mode);
    }

    // A config block in 72-character lines; an ed25519 token is for list
    // signing, since xrpld loads only secp256k1 validator tokens.
    void
    block(
        std::string const& section,
        std::string const& publicKey,
        std::string const& body,
        std::optional<KeyType> tokenKeyType)
    {
        std::string text = "# validator public key: " + publicKey + "\n\n[" + section + "]\n";
        for (std::size_t i = 0; i < body.size(); i += kBlockLineLength)
            text.append(body, i, kBlockLineLength).push_back('\n');

        bool const forPublishers = tokenKeyType == KeyType::Ed25519;
        if (!file_)
        {
            if (forPublishers)
            {
                out_ << kForPublishers << ":\n\n";
            }
            else
            {
                out_ << "Update xrpld.cfg file with these values and restart xrpld:\n\n";
            }
            out_ << text << std::endl;
            return;
        }
        write(text, "[" + section + "]");
        if (forPublishers)
            out_ << kForPublishers << "\n";
    }

    // A base64 manifest alone: the file a list publisher names with --manifest-file.
    void
    manifest(std::string const& base64)
    {
        if (!file_)
        {
            out_ << "The manifest for a validator-list publisher; save it and pass it to\n"
                    "start_sign_list and finish_sign_list with --manifest-file:\n\n"
                 << base64 << std::endl;
            return;
        }
        write(base64 + "\n", "The manifest");
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
        file_->write(text);
        file_->commit();
        out_ << what << " written to " << file_->target().string() << "\n";
    }
};

/**
 * Decodes a signature given as hex or base64. Only using it shows whether it
 * is right, so any string that decodes is accepted.
 */
Blob
decodeSignature(std::string const& data)
{
    if (auto const bytes = strUnHex(data))
        return *bytes;
    if (auto const bytes = decodeBase64Exact(data))
        return Blob(bytes->begin(), bytes->end());
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
storedNotice(std::filesystem::path const& keyFile, std::ostream& out)
{
    out << "Validator keys stored in " << keyFile.string()
        << "\n\nThis file should be stored securely and not shared.\n\n";
}

SigningKeys
loadUnrevoked(std::filesystem::path const& keyFile)
{
    auto const keys = SigningKeys::makeSigningKeys(keyFile);
    if (keys.revoked())
        throw std::runtime_error("Operation error: The specified master key has been revoked!");
    return keys;
}

void
warnRevocation(SigningKeys const& keys, std::ostream& err)
{
    if (keys.revoked())
    {
        err << "WARNING: Validator keys have already been revoked!\n\n";
    }
    else
    {
        err << "WARNING: This will revoke your validator keys!\n\n";
    }
}

void
emitFinished(SigningKeys const& keys, SigningKeys::Finished const& finished, Output& output)
{
    if (finished.secret)
    {
        auto const keyType =
            deserializeManifest(keys.manifest())
                .and_then([](Manifest const& m) { return m.signingKey; })
                .and_then([](PublicKey const& signingKey) { return publicKeyType(signingKey); });
        output.block(
            "validator_token",
            nodePublic(keys),
            tokenToBase64(
                ValidatorToken{
                    .manifest = finished.manifest,
                    .validationSecret = *finished.secret,
                }),
            keyType);
    }
    else
    {
        output.manifest(finished.manifest);
    }
}

void
emitAttestation(SigningKeys const& keys, Context const& ctx)
{
    if (keys.domain().empty())
    {
        ctx.out << "No attestation is necessary if no domain is specified!\n"
                   "If you have an attestation in your xrp-ledger.toml\n"
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
cmdCreateKeys(Args const&, Context const& ctx)
{
    SigningKeys const keys(KeyType::Ed25519);
    keys.writeToFile(ctx.options.keyFile, OwnerOnlyFile::Existing::Refuse);
    storedNotice(ctx.options.keyFile, ctx.out);
    return EXIT_SUCCESS;
}

int
cmdCreateExternal(Args const& args, Context const& ctx)
{
    auto const publicKey = parsePublicKey(args[0]);
    // Every PublicKey has a type, so value_or never falls back.
    SigningKeys const keys(publicKeyType(publicKey).value_or(KeyType::Secp256k1), publicKey);
    keys.writeToFile(ctx.options.keyFile, OwnerOnlyFile::Existing::Refuse);
    storedNotice(ctx.options.keyFile, ctx.out);
    return EXIT_SUCCESS;
}

int
cmdCreateToken(Args const&, Context const& ctx)
{
    Output output(ctx);
    auto keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    auto const keyType = ctx.options.tokenKeyType.value_or(KeyType::Secp256k1);
    auto const token = keys.createToken(keyType);
    keys.writeToFile(ctx.options.keyFile);
    output.block("validator_token", nodePublic(keys), tokenToBase64(token), keyType);
    return EXIT_SUCCESS;
}

int
cmdStartToken(Args const&, Context const& ctx)
{
    auto keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    auto const data = keys.startToken(
        ctx.options.tokenKeyType.value_or(KeyType::Secp256k1), ctx.options.signingKey);
    keys.writeToFile(ctx.options.keyFile);
    ctx.out << data << std::endl;
    return EXIT_SUCCESS;
}

int
cmdFinishToken(Args const& args, Context const& ctx)
{
    Output output(ctx);
    auto keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    std::optional<Blob> signingSig;
    if (args.size() == 2)
        signingSig = decodeSignature(args[1]);
    auto const finished = keys.finishToken(decodeSignature(args[0]), signingSig);
    keys.writeToFile(ctx.options.keyFile);
    emitFinished(keys, finished, output);
    return EXIT_SUCCESS;
}

int
cmdRevokeKeys(Args const&, Context const& ctx)
{
    Output output(ctx);
    auto keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    warnRevocation(keys, ctx.err);
    auto const revocation = keys.revoke();
    keys.writeToFile(ctx.options.keyFile);
    output.block("validator_key_revocation", nodePublic(keys), revocation, std::nullopt);
    return EXIT_SUCCESS;
}

int
cmdStartRevokeKeys(Args const&, Context const& ctx)
{
    auto const keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    warnRevocation(keys, ctx.err);
    ctx.out << keys.startRevoke() << std::endl;
    return EXIT_SUCCESS;
}

int
cmdFinishRevokeKeys(Args const& args, Context const& ctx)
{
    Output output(ctx);
    auto keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    warnRevocation(keys, ctx.err);
    auto const revocation = keys.finishRevoke(decodeSignature(args[0]));
    keys.writeToFile(ctx.options.keyFile);
    output.block("validator_key_revocation", nodePublic(keys), revocation, std::nullopt);
    return EXIT_SUCCESS;
}

int
setDomain(std::string const& domain, Context const& ctx)
{
    Output output(ctx);
    auto keys = loadUnrevoked(ctx.options.keyFile);

    if (domain == keys.domain())
    {
        if (domain.empty())
        {
            ctx.out << "The domain name was already cleared!\n";
        }
        else
        {
            ctx.out << "The domain name was already set.\n";
        }
        return EXIT_SUCCESS;
    }

    keys.domain(domain);
    if (!keys.hasSecret())
    {
        keys.writeToFile(ctx.options.keyFile);
        if (domain.empty())
        {
            ctx.out << "The domain name has been cleared.\n";
        }
        else
        {
            ctx.out << "The domain name has been set to: " << domain << "\n";
        }
        ctx.out << "The next token carries it: run start_token and finish_token.\n";
        return EXIT_SUCCESS;
    }

    auto const keyType = ctx.options.tokenKeyType.value_or(KeyType::Secp256k1);
    auto const token = keys.createToken(keyType);
    keys.writeToFile(ctx.options.keyFile);

    if (domain.empty())
    {
        ctx.out << "The domain name has been cleared.\n";
    }
    else
    {
        ctx.out << "The domain name has been set to: " << domain << "\n\n";
    }
    emitAttestation(keys, ctx);
    ctx.out << "\n";
    if (keyType == KeyType::Secp256k1)
    {
        ctx.out << "You also need to update the xrpld.cfg file to add a new\n"
                   "validator token and restart xrpld:\n\n";
    }
    output.block("validator_token", nodePublic(keys), tokenToBase64(token), keyType);
    return EXIT_SUCCESS;
}

int
cmdSetDomain(Args const& args, Context const& ctx)
{
    return setDomain(args[0], ctx);
}

int
cmdClearDomain(Args const&, Context const& ctx)
{
    return setDomain("", ctx);
}

int
cmdAttestDomain(Args const&, Context const& ctx)
{
    emitAttestation(loadUnrevoked(ctx.options.keyFile), ctx);
    return EXIT_SUCCESS;
}

int
cmdSign(Args const& args, Context const& ctx)
{
    auto const& data = args[0];
    if (data.empty())
        throw std::runtime_error("Syntax error: Must specify data string to sign");
    auto const keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    if (keys.revoked())
        ctx.err << "WARNING: Validator keys have been revoked!\n\n";
    ctx.out << keys.sign(data) << std::endl;
    return EXIT_SUCCESS;
}

// Writes the fields of manifest signing data to err, so whoever signs sees the
// key, sequence and domain the signature binds.
void
describeManifestSigningData(Blob const& bytes, std::ostream& err)
{
    auto constexpr prefixSize = sizeof(std::uint32_t);
    if (bytes.size() < prefixSize)
        return;
    std::uint32_t prefix = 0;
    for (std::size_t i = 0; i < prefixSize; ++i)
        prefix = (prefix << 8) | bytes[i];
    if (prefix != static_cast<std::uint32_t>(HashPrefix::Manifest))
        return;

    // PublicKey aborts on a slice of no key type, so the type is checked first
    auto const keyOf = [](STObject const& st, SField const& field) {
        auto const vl = st.getFieldVL(field);
        if (!publicKeyType(makeSlice(vl)))
            throw std::runtime_error("not a public key");
        return toBase58(TokenType::NodePublic, PublicKey(makeSlice(vl)));
    };

    std::ostringstream fields;
    try
    {
        STObject const st(
            SerialIter{bytes.data() + prefixSize, bytes.size() - prefixSize}, sfGeneric);
        fields << "master key: " << keyOf(st, sfPublicKey) << "\n";
        if (st.isFieldPresent(sfSigningPubKey))
        {
            fields << "signing key: " << keyOf(st, sfSigningPubKey) << "\n";
        }
        else
        {
            fields << "signing key: none (revocation)\n";
        }
        fields << "sequence: " << st.getFieldU32(sfSequence) << "\n";
        if (st.isFieldPresent(sfDomain))
        {
            auto const domain = st.getFieldVL(sfDomain);
            fields << "domain: " << std::string(domain.begin(), domain.end()) << "\n";
        }
        else
        {
            fields << "domain: none\n";
        }
    }
    catch (std::exception const&)
    {
        err << "These bytes start with the manifest prefix and do not parse as manifest fields\n";
        return;
    }
    err << "These bytes are a manifest's signing data:\n" << fields.str();
}

int
cmdSignHex(Args const& args, Context const& ctx)
{
    auto const data = boost::algorithm::trim_copy(args[0]);
    if (data.empty())
        throw std::runtime_error("Syntax error: Must specify data string to sign");
    auto const bytes = strUnHex(data);
    if (!bytes)
        throw std::runtime_error("Could not decode hex string: " + data);
    auto const keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    if (keys.revoked())
        ctx.err << "WARNING: Validator keys have been revoked!\n\n";
    describeManifestSigningData(*bytes, ctx.err);
    ctx.out << keys.signHex(data) << std::endl;
    return EXIT_SUCCESS;
}

int
cmdShowManifest(Args const& args, Context const& ctx)
{
    auto const keys = SigningKeys::makeSigningKeys(ctx.options.keyFile);
    auto const& m = keys.manifest();
    // The encoding is optional; base64 is what a list's "manifest" field and
    // --manifest-file take.
    std::string encoding = "base64";
    if (!args.empty())
        encoding = args[0];
    if (m.empty())
    {
        ctx.out << "The last manifest generated is unavailable. You can\n"
                   "generate a new one.\n\n";
        return EXIT_SUCCESS;
    }
    if (encoding == "base64")
    {
        ctx.out << "Manifest #" << keys.sequence() << " (Base64):\n"
                << base64Encode(m.data(), m.size()) << "\n\n";
        return EXIT_SUCCESS;
    }
    if (encoding == "hex")
    {
        ctx.out << "Manifest #" << keys.sequence() << " (Hex):\n" << strHex(makeSlice(m)) << "\n\n";
        return EXIT_SUCCESS;
    }
    throw std::runtime_error("Unknown encoding '" + encoding + "'");
}

// The manifest a list is signed under when the signing key is external, and
// the signing key it delegates to.
std::pair<Manifest, PublicKey>
loadSigningManifest(Context const& ctx, char const* command)
{
    if (!ctx.options.manifestFile)
        throw std::runtime_error(std::string(command) + " needs --manifest-file");
    auto manifest = loadManifestFile(*ctx.options.manifestFile);
    if (!manifest.signingKey)
        throw std::runtime_error("The manifest is revoked");
    auto const signingKey = *manifest.signingKey;
    return {std::move(manifest), signingKey};
}

void
emitSignedList(
    std::string const& manifestBase64,
    PublicKey const& masterKey,
    UnsignedList const& list,
    std::string const& signatureHex,
    Context const& ctx,
    Output& output)
{
    std::optional<json::Value> append;
    if (ctx.options.appendFile)
        append = readJsonFile(*ctx.options.appendFile);
    output.json(makeSignedList(
        manifestBase64,
        masterKey,
        list,
        signatureHex,
        ctx.options.listVersion.value_or(1),
        append,
        netClockNow()));
}

int
cmdSignList(Args const& args, Context const& ctx)
{
    if (!ctx.options.tokenFile)
        throw std::runtime_error("sign_list needs --token-file");
    Output output(ctx, args[0], OwnerOnlyFile::kPublished);

    auto const token = loadTokenFile(*ctx.options.tokenFile);
    auto const manifest = deserializeManifest(base64Decode(token.manifest));
    if (!manifest || !manifest->verify() || !manifest->signingKey)
        throw std::runtime_error("The token's manifest is not valid");

    auto const signingKey = *manifest->signingKey;
    // Every PublicKey has a type, so value_or never falls back.
    if (derivePublicKey(
            publicKeyType(signingKey).value_or(KeyType::Secp256k1), token.validationSecret) !=
        signingKey)
        throw std::runtime_error("The token's secret does not match its manifest");

    auto const list = loadUnsignedList(args[0]);
    auto const signature =
        strHex(xrpl::sign(signingKey, token.validationSecret, makeSlice(list.canonical)));
    emitSignedList(token.manifest, manifest->masterKey, list, signature, ctx, output);
    return EXIT_SUCCESS;
}

int
cmdStartSignList(Args const& args, Context const& ctx)
{
    loadSigningManifest(ctx, "start_sign_list");
    auto const list = loadUnsignedList(args[0]);
    requireUnexpired(list, netClockNow());
    ctx.out << strHex(makeSlice(list.canonical)) << std::endl;
    return EXIT_SUCCESS;
}

int
cmdFinishSignList(Args const& args, Context const& ctx)
{
    auto const [manifest, signingKey] = loadSigningManifest(ctx, "finish_sign_list");
    Output output(ctx, args[1], OwnerOnlyFile::kPublished);

    auto const list = loadUnsignedList(args[1]);
    auto const sig = decodeSignature(args[0]);
    if (!verify(signingKey, makeSlice(list.canonical), makeSlice(sig)))
        throw std::runtime_error("The signature does not verify under the manifest's signing key");

    emitSignedList(
        base64Encode(manifest.serialized), manifest.masterKey, list, strHex(sig), ctx, output);
    return EXIT_SUCCESS;
}

int
cmdVerifyList(Args const& args, Context const& ctx)
{
    std::optional<UnsignedList> roster;
    if (ctx.options.validatorsFile)
        roster = loadUnsignedList(*ctx.options.validatorsFile);

    auto const report =
        verifyList(readJsonFile(args[0]), roster, ctx.options.expectedKey, netClockNow());
    ctx.out << report.toStyledString() << std::endl;
    return report["ok"].asBool() ? EXIT_SUCCESS : EXIT_FAILURE;
}

// The options a command reads, as bits of Command::options.
enum class Option : unsigned {
    TokenKeyType = 1U << 0,
    SigningKey = 1U << 1,
    TokenFile = 1U << 2,
    ManifestFile = 1U << 3,
    Out = 1U << 4,
    ListVersion = 1U << 5,
    Append = 1U << 6,
    Validators = 1U << 7,
    ExpectedKey = 1U << 8,
};

// The Command::options mask of the given options.
template <std::same_as<Option>... Options>
constexpr unsigned
optionMask(Options... options)
{
    return (0U | ... | static_cast<unsigned>(options));
}

struct OptionFlag
{
    Option option;
    char const* flag;
    bool (*isSet)(ToolOptions const&);
};

constexpr std::array<OptionFlag, 9> kOptionFlags{
    {
        {
            .option = Option::TokenKeyType,
            .flag = "--token-key-type",
            .isSet = [](ToolOptions const& o) { return o.tokenKeyType.has_value(); },
        },
        {
            .option = Option::SigningKey,
            .flag = "--signing-key",
            .isSet = [](ToolOptions const& o) { return o.signingKey.has_value(); },
        },
        {
            .option = Option::TokenFile,
            .flag = "--token-file",
            .isSet = [](ToolOptions const& o) { return o.tokenFile.has_value(); },
        },
        {
            .option = Option::ManifestFile,
            .flag = "--manifest-file",
            .isSet = [](ToolOptions const& o) { return o.manifestFile.has_value(); },
        },
        {
            .option = Option::Out,
            .flag = "--out",
            .isSet = [](ToolOptions const& o) { return o.outFile.has_value(); },
        },
        {
            .option = Option::ListVersion,
            .flag = "--list-version",
            .isSet = [](ToolOptions const& o) { return o.listVersion.has_value(); },
        },
        {
            .option = Option::Append,
            .flag = "--append",
            .isSet = [](ToolOptions const& o) { return o.appendFile.has_value(); },
        },
        {
            .option = Option::Validators,
            .flag = "--validators",
            .isSet = [](ToolOptions const& o) { return o.validatorsFile.has_value(); },
        },
        {
            .option = Option::ExpectedKey,
            .flag = "--expected-key",
            .isSet = [](ToolOptions const& o) { return o.expectedKey.has_value(); },
        },
    },
};

struct Command
{
    char const* name;
    std::size_t minArgs;
    std::size_t maxArgs;
    // Loads, changes and rewrites the key file, so runs under KeyFileLock.
    bool updatesKeyFile;
    // The Option bits of the options the command reads; any other is refused.
    unsigned options;
    int (*run)(Args const&, Context const&);
};

constexpr std::array<Command, 18> kCommands{
    {
        {
            .name = "create_keys",
            .minArgs = 0,
            .maxArgs = 0,
            .updatesKeyFile = false,
            .options = optionMask(),
            .run = cmdCreateKeys,
        },
        {
            .name = "create_external",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = false,
            .options = optionMask(),
            .run = cmdCreateExternal,
        },
        {
            .name = "create_token",
            .minArgs = 0,
            .maxArgs = 0,
            .updatesKeyFile = true,
            .options = optionMask(Option::TokenKeyType, Option::Out),
            .run = cmdCreateToken,
        },
        {
            .name = "start_token",
            .minArgs = 0,
            .maxArgs = 0,
            .updatesKeyFile = true,
            .options = optionMask(Option::TokenKeyType, Option::SigningKey),
            .run = cmdStartToken,
        },
        {
            .name = "finish_token",
            .minArgs = 1,
            .maxArgs = 2,
            .updatesKeyFile = true,
            .options = optionMask(Option::Out),
            .run = cmdFinishToken,
        },
        {
            .name = "revoke_keys",
            .minArgs = 0,
            .maxArgs = 0,
            .updatesKeyFile = true,
            .options = optionMask(Option::Out),
            .run = cmdRevokeKeys,
        },
        {
            .name = "start_revoke_keys",
            .minArgs = 0,
            .maxArgs = 0,
            .updatesKeyFile = false,
            .options = optionMask(),
            .run = cmdStartRevokeKeys,
        },
        {
            .name = "finish_revoke_keys",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = true,
            .options = optionMask(Option::Out),
            .run = cmdFinishRevokeKeys,
        },
        {
            .name = "set_domain",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = true,
            .options = optionMask(Option::TokenKeyType, Option::Out),
            .run = cmdSetDomain,
        },
        {
            .name = "clear_domain",
            .minArgs = 0,
            .maxArgs = 0,
            .updatesKeyFile = true,
            .options = optionMask(Option::TokenKeyType, Option::Out),
            .run = cmdClearDomain,
        },
        {
            .name = "attest_domain",
            .minArgs = 0,
            .maxArgs = 0,
            .updatesKeyFile = false,
            .options = optionMask(),
            .run = cmdAttestDomain,
        },
        {
            .name = "sign",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = false,
            .options = optionMask(),
            .run = cmdSign,
        },
        {
            .name = "sign_hex",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = false,
            .options = optionMask(),
            .run = cmdSignHex,
        },
        {
            .name = "show_manifest",
            .minArgs = 0,
            .maxArgs = 1,
            .updatesKeyFile = false,
            .options = optionMask(),
            .run = cmdShowManifest,
        },
        {
            .name = "sign_list",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = false,
            .options =
                optionMask(Option::TokenFile, Option::Out, Option::ListVersion, Option::Append),
            .run = cmdSignList,
        },
        {
            .name = "start_sign_list",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = false,
            .options = optionMask(Option::ManifestFile),
            .run = cmdStartSignList,
        },
        {
            .name = "finish_sign_list",
            .minArgs = 2,
            .maxArgs = 2,
            .updatesKeyFile = false,
            .options =
                optionMask(Option::ManifestFile, Option::Out, Option::ListVersion, Option::Append),
            .run = cmdFinishSignList,
        },
        {
            .name = "verify_list",
            .minArgs = 1,
            .maxArgs = 1,
            .updatesKeyFile = false,
            .options = optionMask(Option::Validators, Option::ExpectedKey),
            .run = cmdVerifyList,
        },
    },
};

}  // namespace

PublicKey
parsePublicKey(std::string const& data)
{
    if (auto const key = parseBase58<PublicKey>(TokenType::NodePublic, data))
        return *key;
    if (auto const key = parseHexKey(data))
        return *key;
    if (auto const bytes = decodeBase64Exact(data); bytes && publicKeyType(makeSlice(*bytes)))
        return PublicKey(makeSlice(*bytes));
    throw std::runtime_error("Unable to parse public key: " + data);
}

std::filesystem::path
defaultKeyFile(std::filesystem::path const& home)
{
    auto keyFile = home / ".xrpld" / "validator-keys.json";
    auto legacy = home / ".ripple" / "validator-keys.json";
    if (!std::filesystem::exists(keyFile) && std::filesystem::exists(legacy))
        return legacy;
    return keyFile;
}

int
runCommand(
    std::string const& command,
    std::vector<std::string> const& args,
    ToolOptions const& options,
    std::ostream& out,
    std::ostream& err)
{
    auto const it =
        std::ranges::find_if(kCommands, [&](Command const& c) { return command == c.name; });
    if (it == kCommands.end())
        throw std::runtime_error("Unknown command: " + command);
    if (args.size() < it->minArgs || args.size() > it->maxArgs)
        throw std::runtime_error("Syntax error: Wrong number of arguments");
    for (auto const& o : kOptionFlags)
    {
        if (o.isSet(options) && (it->options & optionMask(o.option)) == 0)
        {
            throw std::runtime_error(
                std::string("Syntax error: ") + o.flag + " does not apply to " + command);
        }
    }

    std::optional<KeyFileLock> lock;
    if (it->updatesKeyFile)
        lock.emplace(options.keyFile);
    Context const ctx{.options = options, .out = out, .err = err};
    int const rc = it->run(args, ctx);
    // A result that never reached the caller, such as a token, is a failure.
    out.flush();
    if (!out)
        throw std::runtime_error("Cannot write the output");
    return rc;
}

}  // namespace xrpl::tools
