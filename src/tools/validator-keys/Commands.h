#pragma once

#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace xrpl::tools {

/**
 * The command-line options every command may read.
 */
struct ToolOptions
{
    // The master key file.
    std::filesystem::path keyFile;
    // Key type of a token's signing key. xrpld loads only secp256k1 validator
    // tokens; ed25519 is for a publisher's signing key.
    KeyType tokenKeyType = KeyType::Secp256k1;
    // External signing key a token delegates to.
    std::optional<PublicKey> signingKey;
    // File holding a [validator_token] block.
    std::optional<std::filesystem::path> tokenFile;
    // File holding a base64 manifest.
    std::optional<std::filesystem::path> manifestFile;
    // File to write a token or a signed list to instead of stdout.
    std::optional<std::filesystem::path> outFile;
    // Version of the signed list document.
    unsigned listVersion = 1;
    // Version 2 list to add a blob to.
    std::optional<std::filesystem::path> appendFile;
    // Unsigned list whose validators a published list must carry.
    std::optional<std::filesystem::path> validatorsFile;
    // Master key a published list must be signed under.
    std::optional<PublicKey> expectedKey;
};

/**
 * The tool's version, checked to be a semantic version.
 */
std::string const&
getVersionString();

/**
 * Runs one command. Results go to @p out, warnings and notes to @p err.
 *
 * @return The process exit code
 *
 * @throws std::runtime_error naming what went wrong; nothing has been written
 *         to a key file or an output file when it throws before that point
 */
int
runCommand(
    std::string const& command,
    std::vector<std::string> const& args,
    ToolOptions const& options,
    std::ostream& out,
    std::ostream& err);

}  // namespace xrpl::tools
