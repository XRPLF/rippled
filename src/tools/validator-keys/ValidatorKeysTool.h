#pragma once

#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>

#include <boost/filesystem/path.hpp>

#include <optional>
#include <string>
#include <vector>

std::string const&
getVersionString();

/**
 * The command-line options every command may read.
 */
struct ToolOptions
{
    // The master key file.
    boost::filesystem::path keyFile;
    // Key type of a token's signing key.
    xrpl::KeyType tokenKeyType = xrpl::KeyType::Secp256k1;
    // External signing key a token delegates to.
    std::optional<xrpl::PublicKey> signingKey;
    // File holding a [validator_token] block.
    std::optional<boost::filesystem::path> tokenFile;
    // File holding a base64 manifest.
    std::optional<boost::filesystem::path> manifestFile;
    // File to write a token or a signed list to instead of stdout.
    std::optional<boost::filesystem::path> outFile;
    // Version of the signed list document.
    unsigned listVersion = 1;
    // Version 2 list to add a blob to.
    std::optional<boost::filesystem::path> appendFile;
    // Unsigned list whose validators a published list must carry.
    std::optional<boost::filesystem::path> validatorsFile;
    // Master key a published list must be signed under.
    std::optional<xrpl::PublicKey> expectedKey;
};

void
createKeyFile(boost::filesystem::path const& keyFile);

void
createToken(ToolOptions const& options);

void
createRevocation(boost::filesystem::path const& keyFile);

/*****************************************/
/* External signing support              */
void
createExternal(std::string const& data, boost::filesystem::path const& keyFile);

void
startToken(ToolOptions const& options);

void
finishToken(std::vector<std::string> const& signatures, ToolOptions const& options);

void
startRevocation(boost::filesystem::path const& keyFile);

void
finishRevocation(std::string const& data, boost::filesystem::path const& keyFile);

/*****************************************/
/* Validator lists                        */
void
signListFile(boost::filesystem::path const& unsignedList, ToolOptions const& options);

void
startSignList(boost::filesystem::path const& unsignedList, ToolOptions const& options);

void
finishSignList(
    std::string const& signature,
    boost::filesystem::path const& unsignedList,
    ToolOptions const& options);

int
verifyListFile(boost::filesystem::path const& list, ToolOptions const& options);

/*****************************************/

void
signData(std::string const& data, boost::filesystem::path const& keyFile);

void
signHexData(std::string const& data, boost::filesystem::path const& keyFile);

int
runCommand(
    std::string const& command,
    std::vector<std::string> const& args,
    ToolOptions const& options);
