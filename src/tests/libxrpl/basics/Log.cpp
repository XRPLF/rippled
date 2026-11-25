#include <xrpl/basics/Log.h>

#include <xrpl/basics/CredentialFields.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/beast/utility/Journal.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace xrpl {

namespace {

/**
 * Writes @p text through a Logs object and returns what its file then holds.
 *
 * @param text The message to log.
 * @return The formatted line, as the log file received it.
 */
std::string
logged(std::string const& text)
{
    auto const path = uniqueRandomPath(std::filesystem::temp_directory_path(), "xrpl-log-");
    std::string line;
    {
        Logs logs{beast::Severity::Trace};
        logs.silent(true);
        EXPECT_TRUE(logs.open(path));
        logs.write(beast::Severity::Info, "Test", text, false);

        std::ifstream input{path};
        line.assign(std::istreambuf_iterator<char>{input}, {});
    }
    std::filesystem::remove(path);
    return line;
}

}  // namespace

TEST(Log, scrubs_every_credential_field)
{
    for (auto const field : kCredentialFields)
    {
        auto const name = std::string{field};
        auto const value = "marker-for-" + name;
        std::string text{R"({"account":"rPublic",")"};
        text.append(name).append(R"(":")").append(value).append(R"("})");
        auto const line = logged(text);

        // The value is replaced by asterisks of the same length; the rest of the line is kept.
        std::string scrubbed{'"'};
        scrubbed.append(name).append(R"(":")").append(value.size(), '*').append(1, '"');
        EXPECT_FALSE(line.contains(value)) << name;
        EXPECT_TRUE(line.contains(scrubbed)) << name;
        EXPECT_TRUE(line.contains(R"("account":"rPublic")")) << name;
    }
}

}  // namespace xrpl
