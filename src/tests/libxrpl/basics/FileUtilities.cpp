#include <xrpl/basics/FileUtilities.h>

#include <xrpl/basics/ByteUtilities.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>

namespace xrpl {

namespace {

class TempFile
{
public:
    explicit TempFile(boost::filesystem::path file, std::string const& contents)
        : dir_(
              boost::filesystem::temp_directory_path() /
              boost::filesystem::unique_path("xrpl-file-utilities-%%%%-%%%%-%%%%"))
        , file_(dir_ / file)
    {
        boost::filesystem::create_directory(dir_);

        std::ofstream output(file_.string());
        if (!output)
            throw std::runtime_error("Unable to create temporary test file");

        output << contents;
    }

    ~TempFile()
    {
        boost::system::error_code ec;
        boost::filesystem::remove(file_, ec);
        boost::filesystem::remove(dir_, ec);
    }

    [[nodiscard]] boost::filesystem::path const&
    file() const
    {
        return file_;
    }

private:
    boost::filesystem::path dir_;
    boost::filesystem::path file_;
};

}  // namespace

TEST(FileUtilitiesTest, get_file_contents)
{
    using namespace boost::system;

    constexpr char const* kExpectedContents = "This file is very short. That's all we need.";

    TempFile const file("test_file", "This is temporary text that should get overwritten");

    error_code ec;
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
        EXPECT_TRUE(ec && ec.value() == boost::system::errc::file_too_large);
        EXPECT_TRUE(bad.empty());
    }
}

}  // namespace xrpl
