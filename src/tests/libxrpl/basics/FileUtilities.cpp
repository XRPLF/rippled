#include <xrpl/basics/FileUtilities.h>

#include <xrpl/basics/ByteUtilities.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace xrpl {

namespace {

class TempFile
{
public:
    explicit TempFile(std::filesystem::path file, std::string const& contents)
        : dir_(uniqueRandomPath(std::filesystem::temp_directory_path(), "xrpl-file-utilities-"))
        , file_(dir_ / file)
    {
        std::filesystem::create_directory(dir_);

        std::ofstream output(file_);
        if (!output)
            throw std::runtime_error("Unable to create temporary test file");

        output << contents;
    }

    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(file_, ec);
        std::filesystem::remove(dir_, ec);
    }

    [[nodiscard]] std::filesystem::path const&
    file() const
    {
        return file_;
    }

private:
    std::filesystem::path dir_;
    std::filesystem::path file_;
};

}  // namespace

TEST(FileUtilitiesTest, get_file_contents)
{
    constexpr char const* kExpectedContents = "This file is very short. That's all we need.";

    TempFile const file("test_file", "This is temporary text that should get overwritten");

    std::error_code ec;
    auto const path = file.file();

    writeFileContents(ec, path, kExpectedContents);
    EXPECT_FALSE(ec);

    {
        // Test with no max
        auto const good = getFileContents(ec, path);
        EXPECT_FALSE(ec);
        EXPECT_EQ(good, kExpectedContents);
    }

    {
        // Test with large max
        auto const good = getFileContents(ec, path, kilobytes(1));
        EXPECT_FALSE(ec);
        EXPECT_EQ(good, kExpectedContents);
    }

    {
        // Test with small max
        auto const bad = getFileContents(ec, path, 16);
        EXPECT_TRUE(ec && ec.value() == static_cast<int>(std::errc::file_too_large));
        EXPECT_TRUE(bad.empty());
    }
}

}  // namespace xrpl
