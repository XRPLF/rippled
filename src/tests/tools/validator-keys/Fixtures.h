#pragma once

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/server/Manifest.h>

#include <gtest/gtest.h>
#include <tools/validator-keys/Commands.h>
#include <tools/validator-keys/ListSigning.h>
#include <tools/validator-keys/SigningKeys.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::tools::test {

/**
 * What one command run produced.
 */
struct Run
{
    int rc;
    std::string out;
    std::string err;
};

inline Run
run(std::string const& command, std::vector<std::string> const& args, ToolOptions const& options)
{
    std::ostringstream out;
    std::ostringstream err;
    int const rc = runCommand(command, args, options, out, err);
    return {.rc = rc, .out = out.str(), .err = err.str()};
}

/**
 * The message of the std::runtime_error @p f throws, or an empty string.
 */
inline std::string
errorOf(std::function<void()> const& f)
{
    try
    {
        f();
    }
    catch (std::runtime_error const& e)
    {
        return e.what();
    }
    return {};
}

inline ToolOptions
optionsFor(std::filesystem::path const& keyFile)
{
    ToolOptions options;
    options.keyFile = keyFile;
    return options;
}

inline void
writeFile(std::filesystem::path const& file, std::string const& text)
{
    std::error_code ec;
    writeFileContents(ec, file, text);
    ASSERT_FALSE(ec) << file;
}

inline std::string
readFile(std::filesystem::path const& file)
{
    std::error_code ec;
    auto const text = getFileContents(ec, file);
    EXPECT_FALSE(ec) << file;
    return text;
}

inline bool
sameSecret(SecretKey const& a, SecretKey const& b)
{
    return std::equal(a.begin(), a.end(), b.begin());
}

/**
 * The value a test relies on being present; an empty optional fails the test
 * with an exception at the line that expected it.
 */
template <class T>
T
required(std::optional<T> value)
{
    if (!value)
        throw std::runtime_error("required value is missing");
    return std::move(*value);
}

/**
 * A publisher: master keys and the token carrying its ed25519 signing key.
 */
struct Publisher
{
    SigningKeys keys{KeyType::Ed25519};
    ValidatorToken token;
    Manifest manifest;
    PublicKey signingKey;

    Publisher()
        : token(keys.createToken(KeyType::Ed25519))
        , manifest(required(deserializeManifest(base64Decode(token.manifest))))
        , signingKey(required(manifest.signingKey))
    {
    }
};

inline std::vector<ValidatorToken>
makeValidators(std::size_t count)
{
    std::vector<ValidatorToken> validators;
    for (std::size_t i = 0; i < count; ++i)
    {
        SigningKeys keys(KeyType::Ed25519);
        validators.push_back(keys.createToken(KeyType::Secp256k1));
    }
    return validators;
}

/**
 * The unsigned list text a publisher's prepare step writes: one validator per
 * token, each with its manifest.
 */
inline std::string
unsignedListText(
    std::vector<ValidatorToken> const& validators,
    std::uint32_t sequence,
    std::uint32_t expiration,
    std::optional<std::uint32_t> effective = std::nullopt)
{
    std::string text = "{\n  \"sequence\": " + std::to_string(sequence);
    if (effective)
        text += ",\n  \"effective\": " + std::to_string(*effective);
    text += ",\n  \"expiration\": " + std::to_string(expiration) + ",\n  \"validators\": [";
    bool first = true;
    for (auto const& v : validators)
    {
        auto const m = required(deserializeManifest(base64Decode(v.manifest)));
        text += first ? "\n" : ",\n";
        first = false;
        text += R"(    {"validation_public_key": ")" + strHex(m.masterKey) + R"(", "manifest": ")" +
            v.manifest + "\"}";
    }
    text += "\n  ]\n}\n";
    return text;
}

}  // namespace xrpl::tools::test
