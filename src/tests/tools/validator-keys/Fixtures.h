#pragma once

#include <xrpl/protocol/PublicKey.h>
#include <xrpl/server/Manifest.h>

#include <tools/validator-keys/Commands.h>
#include <tools/validator-keys/SigningKeys.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
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

Run
run(std::string const& command, std::vector<std::string> const& args, ToolOptions const& options);

/**
 * The message of the std::runtime_error @p f throws, or an empty string.
 */
std::string
errorOf(std::function<void()> const& f);

ToolOptions
optionsFor(std::filesystem::path const& keyFile);

void
writeFile(std::filesystem::path const& file, std::string const& text);

std::string
readFile(std::filesystem::path const& file);

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

    Publisher();
};

std::vector<ValidatorToken>
makeValidators(std::size_t count);

/**
 * The unsigned list text a publisher's prepare step writes: one validator per
 * token, each with its manifest.
 */
std::string
unsignedListText(
    std::vector<ValidatorToken> const& validators,
    std::uint32_t sequence,
    std::uint32_t expiration,
    std::optional<std::uint32_t> effective = std::nullopt);

}  // namespace xrpl::tools::test
