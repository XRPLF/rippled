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
#include <sys/file.h>
#include <sys/wait.h>
#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>

#include <Fixtures.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <future>
#include <ios>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// The environment the spawned tool inherits.
extern char** environ;

namespace xrpl::tools::test {

namespace {

constexpr std::string_view kArgError = "Syntax error: Wrong number of arguments";
constexpr std::string_view kRevokedOperation =
    "Operation error: The specified master key has been revoked!";
constexpr std::string_view kExhausted =
    "Maximum number of tokens have already been generated.\n"
    "Revoke validator keys if previous token has been compromised.";
constexpr std::string_view kForPublishers =
    "This token's signing key is ed25519: it is for sign_list --token-file; "
    "xrpld loads only secp256k1 validator tokens";
constexpr std::string_view kUpdateConfig = "Update xrpld.cfg";
constexpr std::string_view kAlsoUpdateConfig = "You also need to update the xrpld.cfg file";

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

    // The base64 inside a [validator_token] or [validator_key_revocation] block.
    static std::string
    blockBody(std::string const& text, std::string const& section)
    {
        auto const pos = text.find("[" + section + "]\n");
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

// An exclusive flock on a directory, the lock a command updating a key file
// in that directory waits for; closing the directory releases it.
class HeldDirectory
{
    DIR* dir_;

public:
    explicit HeldDirectory(std::filesystem::path const& dir) : dir_(::opendir(dir.c_str()))
    {
        if (dir_ == nullptr || ::flock(::dirfd(dir_), LOCK_EX) != 0)
        {
            // The destructor does not run when the constructor throws.
            release();
            throw std::runtime_error("cannot lock " + dir.string());
        }
    }

    ~HeldDirectory()
    {
        release();
    }

    HeldDirectory(HeldDirectory const&) = delete;
    HeldDirectory&
    operator=(HeldDirectory const&) = delete;

    void
    release()
    {
        if (dir_ != nullptr)
            ::closedir(dir_);
        dir_ = nullptr;
    }
};

// Runs the built validator-keys binary with @p args, its stdout and stderr
// written to files in @p dir.
Run
runBinary(TempDir const& dir, std::vector<std::string> const& args)
{
    std::string const outFile = dir.file("cli-stdout.txt");
    std::string const errFile = dir.file("cli-stderr.txt");
    std::string const binary = VALIDATOR_KEYS_BINARY;
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(binary.c_str()));
    for (auto const& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0)
        throw std::runtime_error("posix_spawn_file_actions_init failed");
    ::posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, outFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    ::posix_spawn_file_actions_addopen(
        &actions, STDERR_FILENO, errFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t pid = 0;
    int const spawned =
        ::posix_spawn(&pid, binary.c_str(), &actions, nullptr, argv.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0)
        throw std::runtime_error("cannot spawn " + binary);

    int status = 0;
    if (::waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
        throw std::runtime_error(binary + " did not exit normally");
    return {WEXITSTATUS(status), readFile(outFile), readFile(errFile)};
}

}  // namespace

TEST_F(CommandsTest, dispatch)
{
    EXPECT_EQ(commandError("unknown", {}, options_), "Unknown command: unknown");
    EXPECT_EQ(commandError("create_keys", {"x"}, options_), kArgError);
    EXPECT_EQ(commandError("finish_token", {}, options_), kArgError);
    EXPECT_EQ(commandError("finish_token", {"a", "b", "c"}, options_), kArgError);
    EXPECT_EQ(commandError("finish_sign_list", {"a"}, options_), kArgError);
}

TEST_F(CommandsTest, parse_public_key)
{
    SigningKeys const external(KeyType::Ed25519);
    auto const& key = external.publicKey();
    EXPECT_EQ(parsePublicKey(toBase58(TokenType::NodePublic, key)), key);
    EXPECT_EQ(parsePublicKey(strHex(key)), key);
    EXPECT_EQ(parsePublicKey(base64Encode(key.data(), key.size())), key);
    EXPECT_EQ(errorOf([] { parsePublicKey("abcd"); }), "Unable to parse public key: abcd");
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

    // A secp256k1 master key is written with its own key type
    SigningKeys const secp(KeyType::Secp256k1);
    ToolOptions const secpOpts = optionsFor(file("external-secp256k1.json"));
    EXPECT_EQ(
        run("create_external", {toBase58(TokenType::NodePublic, secp.publicKey())}, secpOpts).rc,
        EXIT_SUCCESS);
    json::Value written;
    ASSERT_TRUE(json::Reader().parse(readFile(secpOpts.keyFile), written));
    EXPECT_EQ(written["key_type"].asString(), "secp256k1");
    auto const loadedSecp = keys(secpOpts);
    EXPECT_FALSE(loadedSecp.hasSecret());
    EXPECT_EQ(loadedSecp.publicKey(), secp.publicKey());
    EXPECT_EQ(publicKeyType(loadedSecp.publicKey()), KeyType::Secp256k1);
    EXPECT_EQ(
        commandError("create_external", {"abcd"}, options_), "Unable to parse public key: abcd");
    auto badHex = strHex(key);
    badHex.insert(badHex.size() / 2, "n");
    EXPECT_EQ(
        commandError("create_external", {badHex}, options_),
        "Unable to parse public key: " + badHex);
    auto const trailing = base64Encode(key.data(), key.size()) + "!";
    EXPECT_EQ(
        commandError("create_external", {trailing}, options_),
        "Unable to parse public key: " + trailing);
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
    // A missing directory is reported as the missing key file, not as a lock failure
    auto const inMissingDir = file("missing") / "k.json";
    EXPECT_EQ(
        commandError("create_token", {}, optionsFor(inMissingDir)),
        "Failed to open key file: " + inMissingDir.string());
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
    auto const pending = run("start_token", {}, options_);
    EXPECT_EQ(
        commandError("finish_token", {signer.sign("foo")}, options_),
        "Manifest is not properly signed");
    EXPECT_EQ(keys(options_).sequence(), 2u);

    // The token stays pending: start_token prints the same bytes again, and a
    // start with another signing key is refused until it is finished
    std::string const pendingError =
        "A token is pending: finish it with finish_token before starting another";
    EXPECT_EQ(run("start_token", {}, options_).out, pending.out);
    SigningKeys const signingKey(KeyType::Ed25519);
    ToolOptions withSigningKey = options_;
    withSigningKey.signingKey = signingKey.publicKey();
    EXPECT_EQ(commandError("start_token", {}, withSigningKey), pendingError);
    auto const pendingBytes = pending.out.substr(0, pending.out.find('\n'));
    EXPECT_EQ(run("finish_token", {signer.signHex(pendingBytes)}, options_).rc, EXIT_SUCCESS);
    EXPECT_EQ(keys(options_).sequence(), 3u);

    // An external signing key too: two signatures, and a manifest without a
    // secret printed as base64 for a list publisher's --manifest-file
    auto const start = run("start_token", {}, withSigningKey);
    EXPECT_EQ(run("start_token", {}, withSigningKey).out, start.out);
    EXPECT_EQ(commandError("start_token", {}, options_), pendingError);
    auto const bytes = start.out.substr(0, start.out.find('\n'));
    EXPECT_EQ(
        commandError("finish_token", {signer.signHex(bytes)}, options_),
        "The pending token's signing key is external; pass its signature too");
    auto const finish =
        run("finish_token", {signer.signHex(bytes), signingKey.signHex(bytes)}, options_);
    EXPECT_EQ(finish.rc, EXIT_SUCCESS);
    EXPECT_NE(finish.out.find("--manifest-file"), std::string::npos);
    EXPECT_EQ(finish.out.find('['), std::string::npos);
    {
        auto const lastLine = finish.out.substr(finish.out.rfind('\n', finish.out.size() - 2) + 1);
        auto const printed =
            required(deserializeManifest(base64Decode(lastLine.substr(0, lastLine.find('\n')))));
        EXPECT_EQ(required(printed.signingKey), signingKey.publicKey());
        EXPECT_EQ(printed.sequence, 4u);
    }

    // The current manifest's signing key is refused for the next token; with
    // another key the manifest goes to --out as the base64 alone
    EXPECT_EQ(
        commandError("start_token", {}, withSigningKey),
        "The signing key is the current manifest's; a server rejects a manifest that reuses it");
    SigningKeys const signingKey2(KeyType::Ed25519);
    withSigningKey.signingKey = signingKey2.publicKey();
    auto const start2 = run("start_token", {}, withSigningKey);
    auto const bytes2 = start2.out.substr(0, start2.out.find('\n'));
    ToolOptions manifestOut = options_;
    manifestOut.outFile = file("manifest.txt");
    EXPECT_NE(
        run("finish_token", {signer.signHex(bytes2), signingKey2.signHex(bytes2)}, manifestOut)
            .out.find("The manifest written to"),
        std::string::npos);
    auto const manifest = loadManifestFile(*manifestOut.outFile);
    EXPECT_EQ(required(manifest.signingKey), signingKey2.publicKey());
    EXPECT_EQ(manifest.sequence, 5u);
    EXPECT_EQ(readFile(*manifestOut.outFile), base64Encode(manifest.serialized) + "\n");

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
    // The pending manifest is fixed, so its domain cannot change before finish_token
    EXPECT_EQ(
        commandError("set_domain", {"other.example.com"}, options_),
        "A token is pending: finish it with finish_token before changing the domain");
    EXPECT_EQ(keys(options_).domain(), "validator.example.com");
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
    ToolOptions toFile = options_;
    toFile.outFile = file("revocation.txt");
    auto const revoked = run("finish_revoke_keys", {signer.signHex(revokeBytes)}, toFile);
    EXPECT_NE(revoked.out.find("written to"), std::string::npos);
    EXPECT_NE(readFile(*toFile.outFile).find("[validator_key_revocation]"), std::string::npos);
    EXPECT_TRUE(keys(options_).revoked());
    EXPECT_NE(
        run("start_revoke_keys", {}, options_).err.find("already been revoked"), std::string::npos);
    EXPECT_EQ(commandError("start_token", {}, options_), "Validator keys have been revoked.");
    EXPECT_EQ(commandError("finish_token", {"00"}, options_), "Validator keys have been revoked.");
}

TEST_F(CommandsTest, output_failure)
{
    run("create_keys", {}, options_);
    std::ostringstream out;
    out.setstate(std::ios::badbit);
    std::ostringstream err;
    EXPECT_EQ(
        errorOf([&] { runCommand("create_token", {}, options_, out, err); }),
        "Cannot write the output");
}

TEST_F(CommandsTest, key_file_lock)
{
    run("create_keys", {}, options_);
    std::promise<int> promise;
    auto result = promise.get_future();
    // Declared before the lock so unwinding on a failed expectation releases
    // the lock before the thread is joined.
    std::jthread worker;
    HeldDirectory held(options_.keyFile.parent_path());
    worker = std::jthread([&] {
        try
        {
            promise.set_value(run("create_token", {}, options_).rc);
        }
        catch (...)
        {
            promise.set_exception(std::current_exception());
        }
    });

    // Blocked before loading the key file while the directory lock is held
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(300)), std::future_status::timeout);
    EXPECT_EQ(keys(options_).sequence(), 0u);

    held.release();
    worker.join();
    EXPECT_EQ(result.get(), EXIT_SUCCESS);
    EXPECT_EQ(keys(options_).sequence(), 1u);
}

TEST_F(CommandsTest, key_file_lock_relative_path)
{
    WorkingDirectory const cwd(dir_.path());
    ToolOptions const opts = optionsFor("validator_keys.json");
    EXPECT_EQ(run("create_keys", {}, opts).rc, EXIT_SUCCESS);
    EXPECT_EQ(run("create_token", {}, opts).rc, EXIT_SUCCESS);
    EXPECT_EQ(keys(options_).sequence(), 1u);
}

TEST_F(CommandsTest, key_file_lock_open_failures)
{
    // A key file under a regular file: opendir fails with ENOTDIR and loading
    // reports the key file.
    auto const regular = file("regular");
    writeFile(regular, "x");
    ToolOptions const underFile = optionsFor(regular / "validator_keys.json");
    EXPECT_EQ(
        commandError("create_token", {}, underFile),
        "Failed to open key file: " + underFile.keyFile.string());
    EXPECT_EQ(
        commandError("create_keys", {}, underFile), "Cannot create directory: " + regular.string());

    // A directory component longer than NAME_MAX: opendir fails with
    // ENAMETOOLONG, which root gets too.
    ToolOptions const longName = optionsFor(file(std::string(300, 'a')) / "validator_keys.json");
    EXPECT_EQ(
        commandError("create_token", {}, longName),
        "Cannot lock key file: " + longName.keyFile.string());
}

TEST_F(CommandsTest, key_file_lock_retries_after_signal)
{
    // A SIGUSR1 handler without SA_RESTART, so the signal interrupts the
    // worker's flock; the previous handler is restored at the end.
    class SignalHandler
    {
        struct sigaction previous_ = {};

    public:
        SignalHandler()
        {
            struct sigaction action = {};
            action.sa_handler = [](int) {};
            action.sa_flags = 0;
            sigemptyset(&action.sa_mask);
            if (::sigaction(SIGUSR1, &action, &previous_) != 0)
                throw std::runtime_error("cannot install the SIGUSR1 handler");
        }
        ~SignalHandler()
        {
            ::sigaction(SIGUSR1, &previous_, nullptr);
        }
        SignalHandler(SignalHandler const&) = delete;
        SignalHandler&
        operator=(SignalHandler const&) = delete;
    };

    run("create_keys", {}, options_);
    SignalHandler const handler;
    std::promise<int> promise;
    auto result = promise.get_future();
    // Declared before the lock so unwinding on a failed expectation releases
    // the lock before the thread is joined.
    std::jthread worker;
    HeldDirectory held(options_.keyFile.parent_path());
    worker = std::jthread([&] {
        try
        {
            promise.set_value(run("create_token", {}, options_).rc);
        }
        catch (...)
        {
            promise.set_exception(std::current_exception());
        }
    });

    // The worker waits on the lock; the signal interrupts its flock
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(300)), std::future_status::timeout);
    EXPECT_EQ(::pthread_kill(worker.native_handle(), SIGUSR1), 0);
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(300)), std::future_status::timeout);
    EXPECT_EQ(keys(options_).sequence(), 0u);

    held.release();
    worker.join();
    EXPECT_EQ(result.get(), EXIT_SUCCESS);
    EXPECT_EQ(keys(options_).sequence(), 1u);
}

TEST_F(CommandsTest, default_key_file)
{
    std::filesystem::path const home = dir_.path();
    auto const keyFile = home / ".xrpld" / "validator-keys.json";
    auto const legacy = home / ".ripple" / "validator-keys.json";
    EXPECT_EQ(defaultKeyFile(home), keyFile);

    std::filesystem::create_directories(legacy.parent_path());
    writeFile(legacy, "{}");
    EXPECT_EQ(defaultKeyFile(home), legacy);

    std::filesystem::create_directories(keyFile.parent_path());
    writeFile(keyFile, "{}");
    EXPECT_EQ(defaultKeyFile(home), keyFile);
}

TEST_F(CommandsTest, revoke_keys)
{
    run("create_keys", {}, options_);

    // An unwritable output path fails before the keys are revoked
    ToolOptions unwritable = options_;
    unwritable.outFile = file("missing/revocation.txt");
    EXPECT_EQ(
        commandError("revoke_keys", {}, unwritable),
        "Cannot write output file: " + unwritable.outFile->string());
    EXPECT_FALSE(keys(options_).revoked());

    // The revocation goes to the output file
    ToolOptions toFile = options_;
    toFile.outFile = file("revocation.txt");
    auto const first = run("revoke_keys", {}, toFile);
    EXPECT_NE(first.err.find("This will revoke"), std::string::npos);
    EXPECT_NE(first.out.find("written to"), std::string::npos);
    EXPECT_NE(readFile(*toFile.outFile).find("[validator_key_revocation]"), std::string::npos);
    EXPECT_TRUE(keys(options_).revoked());

    auto const again = run("revoke_keys", {}, options_);
    EXPECT_NE(again.err.find("already been revoked"), std::string::npos);
    EXPECT_NE(again.out.find("[validator_key_revocation]"), std::string::npos);
    EXPECT_EQ(commandError("set_domain", {"validator.example.com"}, options_), kRevokedOperation);
    EXPECT_EQ(commandError("attest_domain", {}, options_), kRevokedOperation);
    EXPECT_NE(run("sign", {"data"}, options_).err.find("have been revoked"), std::string::npos);
}

TEST_F(CommandsTest, revocation_loads)
{
    // The printed revocation revokes the master key in a server's manifest cache
    run("create_keys", {}, options_);
    auto const masterKey = keys(options_).publicKey();
    auto const printed = run("revoke_keys", {}, options_);
    auto m = required(
        deserializeManifest(base64Decode(blockBody(printed.out, "validator_key_revocation"))));
    EXPECT_TRUE(m.revoked());
    EXPECT_EQ(m.masterKey, masterKey);

    ManifestCache cache;
    EXPECT_EQ(
        cache.applyManifest(std::move(m), ManifestRateLimitCapPolicy::Uncapped),
        ManifestDisposition::Accepted);
    EXPECT_TRUE(cache.revoked(masterKey));
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
    // Each attestation printed is the master key's signature over the attestation data
    auto const loaded = keys(options_);
    for (auto const& out : {set.out, run("attest_domain", {}, options_).out})
    {
        std::string const open = "attestation=\"";
        auto const begin = out.find(open);
        ASSERT_NE(begin, std::string::npos) << out;
        auto const end = out.find('"', begin + open.size());
        ASSERT_NE(end, std::string::npos) << out;
        auto const signature =
            required(strUnHex(out.substr(begin + open.size(), end - begin - open.size())));
        EXPECT_TRUE(
            verify(loaded.publicKey(), makeSlice(loaded.attestationData()), makeSlice(signature)))
            << out;
    }
    EXPECT_NE(run("show_manifest", {"base64"}, options_).out.find("(Base64)"), std::string::npos);
    EXPECT_NE(run("show_manifest", {}, options_).out.find("(Base64)"), std::string::npos);
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

TEST_F(CommandsTest, external_domain_cleared)
{
    // A key file without the master secret records the domain for the next token
    SigningKeys const signer(KeyType::Ed25519);
    run("create_external", {toBase58(TokenType::NodePublic, signer.publicKey())}, options_);

    auto const set = run("set_domain", {"validator.example.com"}, options_);
    EXPECT_EQ(set.rc, EXIT_SUCCESS);
    EXPECT_NE(
        set.out.find("The domain name has been set to: validator.example.com\n"), std::string::npos)
        << set.out;
    EXPECT_EQ(keys(options_).domain(), "validator.example.com");

    auto const cleared = run("clear_domain", {}, options_);
    EXPECT_EQ(cleared.rc, EXIT_SUCCESS);
    EXPECT_EQ(
        cleared.out,
        "The domain name has been cleared.\n"
        "The next token carries it: run start_token and finish_token.\n");
    EXPECT_EQ(keys(options_).domain(), "");
}

TEST_F(CommandsTest, ed25519_token_is_for_publishers)
{
    run("create_keys", {}, options_);

    ToolOptions ed25519 = options_;
    ed25519.tokenKeyType = KeyType::Ed25519;
    auto const created = run("create_token", {}, ed25519).out;
    EXPECT_EQ(created.find(kUpdateConfig), std::string::npos) << created;
    EXPECT_NE(created.find(kForPublishers), std::string::npos) << created;
    EXPECT_NE(created.find("[validator_token]"), std::string::npos) << created;

    auto const domain = run("set_domain", {"validator.example.com"}, ed25519).out;
    EXPECT_EQ(domain.find(kUpdateConfig), std::string::npos) << domain;
    EXPECT_EQ(domain.find(kAlsoUpdateConfig), std::string::npos) << domain;
    EXPECT_NE(domain.find(kForPublishers), std::string::npos) << domain;

    // A secp256k1 token keeps the xrpld.cfg instructions
    ToolOptions secp256k1 = options_;
    secp256k1.tokenKeyType = KeyType::Secp256k1;
    auto const createdSecp = run("create_token", {}, secp256k1).out;
    EXPECT_NE(createdSecp.find(kUpdateConfig), std::string::npos) << createdSecp;
    EXPECT_EQ(createdSecp.find(kForPublishers), std::string::npos) << createdSecp;

    auto const domainSecp = run("set_domain", {"other.example.com"}, secp256k1).out;
    EXPECT_NE(domainSecp.find(kUpdateConfig), std::string::npos) << domainSecp;
    EXPECT_NE(domainSecp.find(kAlsoUpdateConfig), std::string::npos) << domainSecp;
    EXPECT_EQ(domainSecp.find(kForPublishers), std::string::npos) << domainSecp;
}

TEST_F(CommandsTest, block_line_wrapping)
{
    // An ed25519 token's body length depends on the domain length alone; the
    // first domain whose body fills whole 72-character lines is printed
    // without an empty line inside the block.
    run("create_keys", {}, options_);
    ToolOptions ed25519 = options_;
    ed25519.tokenKeyType = KeyType::Ed25519;

    std::string const suffix = ".example.com";
    std::string out;
    std::string body;
    for (std::size_t length = 13; length <= 128; ++length)
    {
        // Labels of 'a' of at most 63 characters joined by '.', never an empty label
        std::string domain;
        for (std::size_t remaining = length - suffix.size(); remaining > 0;)
        {
            std::size_t label = std::min<std::size_t>(63, remaining);
            if (remaining - label == 1)
                --label;
            domain.append(label, 'a');
            remaining -= label;
            if (remaining > 0)
            {
                domain.push_back('.');
                --remaining;
            }
        }
        domain += suffix;
        ASSERT_EQ(domain.size(), length);

        auto const set = run("set_domain", {domain}, ed25519);
        ASSERT_EQ(set.rc, EXIT_SUCCESS) << domain;
        body = blockBody(set.out, "validator_token");
        if (!body.empty() && body.size() % 72 == 0)
        {
            out = set.out;
            break;
        }
    }
    ASSERT_FALSE(out.empty()) << "no domain length from 13 to 128 gives a body of whole lines";

    std::string expected =
        "# validator public key: " + toBase58(TokenType::NodePublic, keys(options_).publicKey()) +
        "\n\n[validator_token]\n";
    for (std::size_t i = 0; i < body.size(); i += 72)
        expected += body.substr(i, 72) + "\n";
    expected += "\n";

    auto const header = out.find("# validator public key: ");
    ASSERT_NE(header, std::string::npos) << out;
    EXPECT_EQ(out.substr(header), expected);
}

TEST_F(CommandsTest, ed25519_token_file_is_for_publishers)
{
    run("create_keys", {}, options_);
    ToolOptions toFile = options_;
    toFile.tokenKeyType = KeyType::Ed25519;
    toFile.outFile = file("token.txt");
    auto const out = run("create_token", {}, toFile).out;
    auto const written = out.find("written to");
    ASSERT_NE(written, std::string::npos) << out;
    EXPECT_NE(out.find(kForPublishers, written), std::string::npos) << out;
    EXPECT_EQ(out.find(kUpdateConfig), std::string::npos) << out;
    EXPECT_NE(readFile(*toFile.outFile).find("[validator_token]"), std::string::npos);
}

TEST_F(CommandsTest, external_ed25519_token_is_for_publishers)
{
    // The signer stands in for the hardware holding the master key
    SigningKeys const signer(KeyType::Ed25519);
    run("create_external", {toBase58(TokenType::NodePublic, signer.publicKey())}, options_);
    ToolOptions ed25519 = options_;
    ed25519.tokenKeyType = KeyType::Ed25519;
    auto const start = run("start_token", {}, ed25519).out;
    auto const finish =
        run("finish_token", {signer.signHex(start.substr(0, start.find('\n')))}, options_).out;
    EXPECT_NE(finish.find(kForPublishers), std::string::npos) << finish;
    EXPECT_EQ(finish.find(kUpdateConfig), std::string::npos) << finish;
    EXPECT_NE(finish.find("[validator_token]"), std::string::npos) << finish;
}

TEST_F(CommandsTest, external_secp256k1_token_updates_config)
{
    // The signer stands in for the hardware holding the master key
    SigningKeys const signer(KeyType::Ed25519);
    run("create_external", {toBase58(TokenType::NodePublic, signer.publicKey())}, options_);
    ToolOptions secp256k1 = options_;
    secp256k1.tokenKeyType = KeyType::Secp256k1;
    auto const start = run("start_token", {}, secp256k1).out;
    auto const finish =
        run("finish_token", {signer.signHex(start.substr(0, start.find('\n')))}, options_).out;
    EXPECT_NE(finish.find(kUpdateConfig), std::string::npos) << finish;
    EXPECT_EQ(finish.find(kForPublishers), std::string::npos) << finish;
}

TEST_F(CommandsTest, sign)
{
    run("create_keys", {}, options_);
    EXPECT_EQ(
        commandError("sign", {""}, options_), "Syntax error: Must specify data string to sign");
    auto const loaded = keys(options_);
    EXPECT_EQ(run("sign", {"data"}, options_).out, loaded.sign("data") + "\n");
    auto const plain = run("sign_hex", {"00FF"}, options_);
    EXPECT_EQ(plain.out, loaded.signHex("00FF") + "\n");
    EXPECT_EQ(plain.err, "");
    EXPECT_EQ(
        commandError("sign_hex", {" "}, options_),
        "Syntax error: Must specify data string to sign");

    // Bytes with the manifest prefix that do not parse are still signed
    auto const unparsed = run("sign_hex", {"4D414E00FF"}, options_);
    EXPECT_EQ(unparsed.out, loaded.signHex("4D414E00FF") + "\n");
    EXPECT_EQ(
        unparsed.err,
        "These bytes start with the manifest prefix and do not parse as manifest fields\n");

    // A master key field holding no valid key type does not parse either
    auto const repeat = [](std::string const& hex, int n) {
        std::string result;
        for (int i = 0; i < n; ++i)
            result += hex;
        return result;
    };
    auto const badKey =
        "4D414E00"
        "2400000001"
        "7121" +
        repeat("01", 33);
    auto const invalidKey = run("sign_hex", {badKey}, options_);
    EXPECT_EQ(invalidKey.out, loaded.signHex(badKey) + "\n");
    EXPECT_EQ(
        invalidKey.err,
        "These bytes start with the manifest prefix and do not parse as manifest fields\n");

    // A signing key field holding no valid key type does not parse
    auto const badSigningKey =
        "4D414E00"
        "2400000001"
        "7121ED" +
        repeat("01", 32) + "7321" + repeat("01", 33);
    auto const invalidSigningKey = run("sign_hex", {badSigningKey}, options_);
    EXPECT_EQ(invalidSigningKey.out, loaded.signHex(badSigningKey) + "\n");
    EXPECT_EQ(
        invalidSigningKey.err,
        "These bytes start with the manifest prefix and do not parse as manifest fields\n");

    // Manifest signing data: sign_hex prints the fields it signs
    auto const masterKey = toBase58(TokenType::NodePublic, loaded.publicKey());
    SigningKeys const signingKey(KeyType::Ed25519);
    ToolOptions withSigningKey = options_;
    withSigningKey.signingKey = signingKey.publicKey();
    ToolOptions const noDomainKeys = optionsFor(file("no-domain.json"));
    run("create_keys", {}, noDomainKeys);
    ToolOptions noDomainStart = noDomainKeys;
    noDomainStart.signingKey = signingKey.publicKey();
    auto const startNoDomain = run("start_token", {}, noDomainStart);
    auto const noDomainBytes = startNoDomain.out.substr(0, startNoDomain.out.find('\n'));
    auto const noDomain = run("sign_hex", {noDomainBytes}, noDomainKeys);
    EXPECT_EQ(noDomain.out, keys(noDomainKeys).signHex(noDomainBytes) + "\n");
    EXPECT_NE(noDomain.err.find("domain: none\n"), std::string::npos) << noDomain.err;

    run("set_domain", {"validator.example.com"}, options_);
    auto const sequence = keys(options_).sequence() + 1;
    auto const start = run("start_token", {}, withSigningKey);
    auto const bytes = start.out.substr(0, start.out.find('\n'));
    auto const token = run("sign_hex", {bytes}, options_);
    EXPECT_EQ(token.out, keys(options_).signHex(bytes) + "\n");
    EXPECT_NE(token.err.find("These bytes are a manifest's signing data:\n"), std::string::npos)
        << token.err;
    EXPECT_NE(token.err.find("master key: " + masterKey), std::string::npos) << token.err;
    EXPECT_NE(
        token.err.find("signing key: " + toBase58(TokenType::NodePublic, signingKey.publicKey())),
        std::string::npos)
        << token.err;
    EXPECT_NE(token.err.find("sequence: " + std::to_string(sequence)), std::string::npos)
        << token.err;
    EXPECT_NE(token.err.find("domain: validator.example.com"), std::string::npos) << token.err;

    auto const startRevoke = run("start_revoke_keys", {}, options_);
    auto const revokeBytes = startRevoke.out.substr(0, startRevoke.out.find('\n'));
    auto const revocation = run("sign_hex", {revokeBytes}, options_);
    EXPECT_EQ(revocation.out, keys(options_).signHex(revokeBytes) + "\n");
    EXPECT_NE(revocation.err.find("signing key: none (revocation)\n"), std::string::npos)
        << revocation.err;
    EXPECT_NE(revocation.err.find("master key: " + masterKey), std::string::npos) << revocation.err;
}

TEST_F(CommandsTest, sign_hex_without_manifest_prefix)
{
    run("create_keys", {}, options_);
    auto const loaded = keys(options_);

    // Exactly the prefix's size and longer data that is not a manifest's are
    // signed with nothing described
    for (std::string const hex : {"01020304", "DEADBEEF00FF"})
    {
        auto const result = run("sign_hex", {hex}, options_);
        EXPECT_EQ(result.out, loaded.signHex(hex) + "\n");
        EXPECT_EQ(result.err, "");
    }
}

TEST_F(CommandsTest, sign_hex_refuses_undecodable_hex)
{
    run("create_keys", {}, options_);
    EXPECT_EQ(commandError("sign_hex", {"ZZ"}, options_), "Could not decode hex string: ZZ");
    EXPECT_EQ(commandError("sign_hex", {" 0G "}, options_), "Could not decode hex string: 0G");
}

TEST_F(CommandsTest, sign_hex_warns_when_revoked)
{
    run("create_keys", {}, options_);
    run("revoke_keys", {}, options_);
    auto const result = run("sign_hex", {"00FF"}, options_);
    EXPECT_EQ(result.err, "WARNING: Validator keys have been revoked!\n\n");
    EXPECT_EQ(result.out, keys(options_).signHex("00FF") + "\n");
}

TEST_F(CommandsTest, list_commands)
{
    // The publisher: a key file and a token carrying an ed25519 signing key
    ToolOptions const publisher = optionsFor(file("publisher.json"));
    run("create_keys", {}, publisher);
    ToolOptions tokenOut = publisher;
    tokenOut.tokenKeyType = KeyType::Ed25519;
    tokenOut.outFile = file("publisher-token.txt");
    run("create_token", {}, tokenOut);
    ToolOptions listSigner = publisher;
    listSigner.tokenFile = tokenOut.outFile;
    auto const master = keys(publisher).publicKey();

    // Three lists with rising sequences, over the same validators
    auto const validators = makeValidators(2);
    auto const now = netClockNow();
    auto const unsignedList = file("unsigned.json");
    auto const unsignedList2 = file("unsigned2.json");
    auto const unsignedList3 = file("unsigned3.json");
    writeFile(unsignedList, unsignedListText(validators, 2026091301, now + 3600));
    writeFile(unsignedList2, unsignedListText(validators, 2026091302, now + 3600));
    writeFile(unsignedList3, unsignedListText(validators, 2026091303, now + 3600));

    EXPECT_EQ(
        commandError("sign_list", {unsignedList.string()}, publisher),
        "sign_list needs --token-file");

    // Sign to a file readable by everyone, then verify with every check on
    ToolOptions signer = listSigner;
    signer.outFile = file("vl.json");
    EXPECT_NE(
        run("sign_list", {unsignedList.string()}, signer).out.find("written to"),
        std::string::npos);
    EXPECT_EQ(
        std::filesystem::status(*signer.outFile).permissions() &
            std::filesystem::perms::others_read,
        std::filesystem::perms::others_read);

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
    {
        // Without --validators the list is verified without a roster to compare
        ToolOptions noRoster = verifier;
        noRoster.validatorsFile.reset();
        auto const r = run("verify_list", {signer.outFile->string()}, noRoster);
        EXPECT_EQ(r.rc, EXIT_SUCCESS) << r.out;
    }

    // To stdout without --out
    {
        auto const r = run("sign_list", {unsignedList.string()}, listSigner);
        json::Reader reader;
        json::Value jv;
        EXPECT_TRUE(reader.parse(r.out, jv)) << r.out;
        EXPECT_EQ(jv[jss::public_key].asString(), strHex(master));
    }

    // Version 2, the next list appended in place; after a key rotation the
    // document under the older manifest is refused
    ToolOptions v2 = signer;
    v2.listVersion = 2;
    v2.outFile = file("vl2.json");
    run("sign_list", {unsignedList.string()}, v2);
    v2.appendFile = v2.outFile;
    EXPECT_EQ(run("sign_list", {unsignedList2.string()}, v2).rc, EXIT_SUCCESS);
    {
        auto const r = run("verify_list", {v2.outFile->string()}, verifier);
        EXPECT_EQ(r.rc, EXIT_SUCCESS) << r.out;
        json::Reader reader;
        json::Value report;
        reader.parse(r.out, report);
        EXPECT_EQ(report["blobs"].size(), 2u);
    }
    {
        ToolOptions rotated = tokenOut;
        rotated.outFile = file("publisher-token-2.txt");
        run("create_token", {}, rotated);
        ToolOptions append = v2;
        append.tokenFile = rotated.outFile;
        append.appendFile = v2.outFile;
        append.outFile = file("vl2c.json");
        EXPECT_EQ(
            commandError("sign_list", {unsignedList3.string()}, append),
            "The list to append to holds a blob under an older manifest; sign its lists again "
            "with this token, without --append");
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
        for (auto const& entry : std::filesystem::directory_iterator(bad.outFile->parent_path()))
        {
            EXPECT_FALSE(entry.path().filename().string().starts_with("kept.json."))
                << entry.path();
        }

        // The output may not replace the token it signs with
        bad.outFile = *signer.tokenFile;
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, bad),
            "--out names an input file: " + signer.tokenFile->string());
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

        // A manifest that deserializes but whose master signature does not verify
        auto const forger = generateKeyPair(KeyType::Ed25519, randomSeed());
        auto const signing = generateKeyPair(KeyType::Ed25519, randomSeed());
        auto const forged =
            makeManifest(forger.first, randomSecretKey(), signing.first, signing.second, 1);
        ASSERT_TRUE(deserializeManifest(forged).has_value());
        ToolOptions unverified = signer;
        unverified.tokenFile = file("unverified-token.txt");
        writeToken(
            *unverified.tokenFile,
            ValidatorToken{.manifest = base64Encode(forged), .validationSecret = signing.second});
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, unverified),
            "The token's manifest is not valid");

        // A revocation carries no signing key
        auto const revocation = makeRevocation(forger.first, forger.second);
        ASSERT_TRUE(required(deserializeManifest(revocation)).verify());
        ToolOptions revoked = signer;
        revoked.tokenFile = file("revocation-token.txt");
        writeToken(
            *revoked.tokenFile,
            ValidatorToken{.manifest = base64Encode(revocation), .validationSecret = secret});
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, revoked),
            "The token's manifest is not valid");
        ToolOptions wrongSecret = signer;
        wrongSecret.tokenFile = file("wrong-secret-token.txt");
        writeToken(
            *wrongSecret.tokenFile,
            ValidatorToken{
                .manifest = loadTokenFile(*listSigner.tokenFile).manifest,
                .validationSecret = secret,
            });
        EXPECT_EQ(
            commandError("sign_list", {unsignedList.string()}, wrongSecret),
            "The token's secret does not match its manifest");
    }
}

TEST_F(CommandsTest, unused_options_refused)
{
    run("create_keys", {}, options_);
    auto const sequence = keys(options_).sequence();

    ToolOptions withSigningKey = options_;
    withSigningKey.signingKey = SigningKeys(KeyType::Ed25519).publicKey();
    EXPECT_EQ(
        commandError("create_token", {}, withSigningKey),
        "Syntax error: --signing-key does not apply to create_token");
    EXPECT_EQ(keys(options_).sequence(), sequence);

    ToolOptions withOut = options_;
    withOut.outFile = file("out.txt");
    EXPECT_EQ(
        commandError("start_token", {}, withOut),
        "Syntax error: --out does not apply to start_token");
    EXPECT_EQ(
        commandError("attest_domain", {}, withOut),
        "Syntax error: --out does not apply to attest_domain");

    ToolOptions withTokenFile = options_;
    withTokenFile.tokenFile = file("token.txt");
    EXPECT_EQ(
        commandError("verify_list", {file("vl.json").string()}, withTokenFile),
        "Syntax error: --token-file does not apply to verify_list");

    ToolOptions withKeyType = optionsFor(file("other.json"));
    withKeyType.tokenKeyType = KeyType::Secp256k1;
    EXPECT_EQ(
        commandError("create_keys", {}, withKeyType),
        "Syntax error: --token-key-type does not apply to create_keys");
    EXPECT_FALSE(std::filesystem::exists(withKeyType.keyFile));
}

TEST_F(CommandsTest, sign_list_refuses_expired_list)
{
    std::string const expired =
        R"(The list's "expiration" is not after now; a server would take it as expired)";

    ToolOptions const keyFile = optionsFor(file("publisher.json"));
    run("create_keys", {}, keyFile);
    ToolOptions tokenOut = keyFile;
    tokenOut.tokenKeyType = KeyType::Ed25519;
    tokenOut.outFile = file("publisher-token.txt");
    run("create_token", {}, tokenOut);
    ToolOptions publisher = keyFile;
    publisher.tokenFile = tokenOut.outFile;

    // An external signing key delegated by a second master key
    ToolOptions const delegator = optionsFor(file("delegator.json"));
    run("create_keys", {}, delegator);
    SigningKeys const external(KeyType::Ed25519);
    ToolOptions delegate = delegator;
    delegate.signingKey = external.publicKey();
    auto const start = run("start_token", {}, delegate);
    auto const bytes = start.out.substr(0, start.out.find('\n'));
    ToolOptions manifestOut = delegator;
    manifestOut.outFile = file("manifest.txt");
    run("finish_token", {keys(delegator).signHex(bytes), external.signHex(bytes)}, manifestOut);

    auto const validators = makeValidators(2);
    auto const now = netClockNow();
    auto const current = file("current.json");
    auto const stale = file("expired.json");
    writeFile(current, unsignedListText(validators, 1, now + 3600));
    writeFile(stale, unsignedListText(validators, 2, now - 1));

    ToolOptions signer = publisher;
    signer.outFile = file("vl.json");
    EXPECT_EQ(commandError("sign_list", {stale.string()}, signer), expired);
    EXPECT_FALSE(std::filesystem::exists(*signer.outFile));

    ToolOptions hardware = delegator;
    hardware.manifestFile = manifestOut.outFile;
    EXPECT_EQ(commandError("start_sign_list", {stale.string()}, hardware), expired);
    hardware.outFile = file("vl-external.json");
    auto const staleSig = external.signHex(strHex(makeSlice(loadUnsignedList(stale).canonical)));
    EXPECT_EQ(commandError("finish_sign_list", {staleSig, stale.string()}, hardware), expired);
    EXPECT_FALSE(std::filesystem::exists(*hardware.outFile));

    // A version 2 document holding a current list refuses an expired one appended
    ToolOptions v2 = publisher;
    v2.listVersion = 2;
    v2.outFile = file("vl2.json");
    ASSERT_EQ(run("sign_list", {current.string()}, v2).rc, EXIT_SUCCESS);
    v2.appendFile = v2.outFile;
    v2.outFile = file("vl2b.json");
    EXPECT_EQ(commandError("sign_list", {stale.string()}, v2), expired);
    EXPECT_FALSE(std::filesystem::exists(*v2.outFile));
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
    auto const start = run("start_token", {}, delegate);
    auto const bytes = start.out.substr(0, start.out.find('\n'));
    ToolOptions manifestOut = publisher;
    manifestOut.outFile = file("manifest.txt");
    run("finish_token", {keys(publisher).signHex(bytes), external.signHex(bytes)}, manifestOut);

    auto const validators = makeValidators(2);
    auto const now = netClockNow();
    auto const unsignedList = file("unsigned.json");
    auto const unsignedList2 = file("unsigned2.json");
    auto const unsignedList3 = file("unsigned3.json");
    writeFile(unsignedList, unsignedListText(validators, 2026091301, now + 3600));
    writeFile(unsignedList2, unsignedListText(validators, 2026091302, now + 3600));
    writeFile(unsignedList3, unsignedListText(validators, 2026091303, now + 3600));
    auto const signExternally = [&](std::filesystem::path const& list) {
        return external.signHex(strHex(makeSlice(loadUnsignedList(list).canonical)));
    };

    ToolOptions hardware = publisher;
    EXPECT_EQ(
        commandError("start_sign_list", {unsignedList.string()}, hardware),
        "start_sign_list needs --manifest-file");
    EXPECT_EQ(
        commandError("finish_sign_list", {"00", unsignedList.string()}, hardware),
        "finish_sign_list needs --manifest-file");
    hardware.manifestFile = manifestOut.outFile;

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
    ToolOptions aliasedManifest = hardware;
    aliasedManifest.outFile = *hardware.manifestFile;
    EXPECT_EQ(
        commandError(
            "finish_sign_list",
            {external.signHex(listBytes), unsignedList.string()},
            aliasedManifest),
        "--out names an input file: " + hardware.manifestFile->string());
    ToolOptions aliasedList = hardware;
    aliasedList.outFile = unsignedList;
    EXPECT_EQ(
        commandError(
            "finish_sign_list", {external.signHex(listBytes), unsignedList.string()}, aliasedList),
        "--out names an input file: " + unsignedList.string());
    ToolOptions verifier = optionsFor(publisher.keyFile);
    verifier.validatorsFile = unsignedList;
    EXPECT_EQ(run("verify_list", {hardware.outFile->string()}, verifier).rc, EXIT_SUCCESS);

    // A version 2 append under the same manifest works; after a rotation the
    // document under the older manifest is refused
    ToolOptions v2 = hardware;
    v2.listVersion = 2;
    v2.outFile = file("vl2.json");
    run("finish_sign_list", {external.signHex(listBytes), unsignedList.string()}, v2);
    v2.appendFile = v2.outFile;
    v2.outFile = file("vl2b.json");
    run("finish_sign_list", {signExternally(unsignedList2), unsignedList2.string()}, v2);
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
        EXPECT_EQ(
            commandError("sign_list", {unsignedList3.string()}, append),
            "The list to append to holds a blob under an older manifest; sign its lists again "
            "with this token, without --append");
    }

    // A revoked manifest signs nothing
    SigningKeys revokedKeys(KeyType::Ed25519);
    ToolOptions revoked = publisher;
    revoked.manifestFile = file("revoked-manifest.txt");
    writeFile(*revoked.manifestFile, revokedKeys.revoke() + "\n");
    EXPECT_EQ(
        commandError("start_sign_list", {unsignedList.string()}, revoked),
        "The manifest is revoked");
    EXPECT_EQ(
        commandError("finish_sign_list", {"00", unsignedList.string()}, revoked),
        "The manifest is revoked");
}

TEST_F(CommandsTest, cli)
{
    auto const version = runBinary(dir_, {"--version"});
    EXPECT_EQ(version.rc, EXIT_SUCCESS);
    EXPECT_NE(version.out.find("validator-keys version"), std::string::npos);

    auto const help = runBinary(dir_, {"--help"});
    EXPECT_EQ(help.rc, EXIT_SUCCESS);
    EXPECT_NE(help.err.find("Commands:"), std::string::npos);

    auto const noCommand = runBinary(dir_, {});
    EXPECT_EQ(noCommand.rc, EXIT_SUCCESS);
    EXPECT_NE(noCommand.err.find("Commands:"), std::string::npos);

    auto const bogus = runBinary(dir_, {"--bogus"});
    EXPECT_EQ(bogus.rc, EXIT_FAILURE);
    EXPECT_NE(bogus.err.find("validator-keys: Incorrect command line syntax."), std::string::npos);

    auto const keyType = runBinary(
        dir_, {"--keyfile", options_.keyFile.string(), "--token-key-type", "foo", "create_token"});
    EXPECT_EQ(keyType.rc, EXIT_FAILURE);
    EXPECT_NE(keyType.err.find("Unknown key type: foo"), std::string::npos);
}

}  // namespace xrpl::tools::test
