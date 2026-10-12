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
    // File to write a token, a manifest, a revocation or a signed list to
    // instead of stdout; a signed list is readable by everyone, the rest by
    // the owner only.
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
 * Parses a public key given as base58 (`nHB...`), hex or base64: the
 * encodings `create_external`, `--signing-key` and `--expected-key` take.
 *
 * @throws std::runtime_error if none of the encodings yields a public key
 */
PublicKey
parsePublicKey(std::string const& data);

/**
 * The key file used when `--keyfile` is not given:
 * `<home>/.xrpld/validator-keys.json`, or the legacy
 * `<home>/.ripple/validator-keys.json` when only that one exists, the same
 * order xrpld reads its config file under its current and legacy names.
 */
std::filesystem::path
defaultKeyFile(std::filesystem::path const& home);

/**
 * Runs one command. Results go to @p out, warnings and notes to @p err.
 * A command that updates the key file holds an exclusive lock on the key
 * file's directory for its whole run, so concurrent runs are serialized.
 *
 * @return The process exit code
 *
 * @throws std::runtime_error naming what went wrong. Nothing has been
 *         written to a key file or an output file when it throws before
 *         that write. A failed write to @p out is thrown after the key
 *         file was updated: the undelivered token is not recoverable and
 *         the next token takes the next sequence.
 */
int
runCommand(
    std::string const& command,
    std::vector<std::string> const& args,
    ToolOptions const& options,
    std::ostream& out,
    std::ostream& err);

}  // namespace xrpl::tools
