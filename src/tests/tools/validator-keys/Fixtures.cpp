#include <Fixtures.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/server/Manifest.h>

#include <gtest/gtest.h>
#include <tools/validator-keys/Commands.h>
#include <tools/validator-keys/SigningKeys.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace xrpl::tools::test {

Run
run(std::string const& command, std::vector<std::string> const& args, ToolOptions const& options)
{
    std::ostringstream out;
    std::ostringstream err;
    int const rc = runCommand(command, args, options, out, err);
    return {.rc = rc, .out = out.str(), .err = err.str()};
}

std::string
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

ToolOptions
optionsFor(std::filesystem::path const& keyFile)
{
    ToolOptions options;
    options.keyFile = keyFile;
    return options;
}

void
writeFile(std::filesystem::path const& file, std::string const& text)
{
    std::error_code ec;
    writeFileContents(ec, file, text);
    ASSERT_FALSE(ec) << file;
}

std::string
readFile(std::filesystem::path const& file)
{
    std::error_code ec;
    auto const text = getFileContents(ec, file);
    EXPECT_FALSE(ec) << file;
    return text;
}

Publisher::Publisher()
    : token(keys.createToken(KeyType::Ed25519))
    , manifest(required(deserializeManifest(base64Decode(token.manifest))))
    , signingKey(required(manifest.signingKey))
{
}

std::vector<ValidatorToken>
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

std::string
unsignedListText(
    std::vector<ValidatorToken> const& validators,
    std::uint32_t sequence,
    std::uint32_t expiration,
    std::optional<std::uint32_t> effective)
{
    std::string text = std::format("{{\n  \"sequence\": {}", sequence);
    if (effective)
        text += std::format(",\n  \"effective\": {}", *effective);
    text += std::format(",\n  \"expiration\": {},\n  \"validators\": [", expiration);
    char const* separator = "\n";
    for (auto const& v : validators)
    {
        auto const m = required(deserializeManifest(base64Decode(v.manifest)));
        text += std::format(
            R"({}    {{"validation_public_key": "{}", "manifest": "{}"}})",
            separator,
            strHex(m.masterKey),
            v.manifest);
        separator = ",\n";
    }
    text += "\n  ]\n}\n";
    return text;
}

}  // namespace xrpl::tools::test
