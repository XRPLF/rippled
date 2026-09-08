#include <xrpl/basics/FileUtilities.h>

#include <xrpl/basics/contract.h>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace xrpl {

std::string
getFileContents(
    std::error_code& ec,
    std::filesystem::path const& sourcePath,
    std::optional<std::size_t> maxSize)
{
    using namespace std::filesystem;

    path const fullPath{canonical(sourcePath, ec)};
    if (ec)
        return {};

    if (maxSize && (file_size(fullPath, ec) > *maxSize || ec))
    {
        if (!ec)
            ec = make_error_code(std::errc::file_too_large);
        return {};
    }

    std::ifstream fileStream(fullPath, std::ios::in);

    if (!fileStream)
    {
        ec.assign(errno, std::generic_category());
        return {};
    }

    std::string result{
        std::istreambuf_iterator<char>{fileStream}, std::istreambuf_iterator<char>{}};

    if (fileStream.bad())
    {
        ec.assign(errno, std::generic_category());
        return {};
    }

    return result;
}

void
writeFileContents(
    std::error_code& ec,
    std::filesystem::path const& destPath,
    std::string const& contents)
{
    std::ofstream fileStream(destPath, std::ios::out | std::ios::trunc);

    if (!fileStream)
    {
        ec.assign(errno, std::generic_category());
        return;
    }

    fileStream << contents;

    if (fileStream.bad())
    {
        ec.assign(errno, std::generic_category());
        return;
    }
}

std::filesystem::path
uniqueRandomPath(
    std::filesystem::path const& base,
    std::string const& prefix,
    std::size_t maxAttempts)
{
    std::random_device rd;
    for (std::size_t attempt = 0; attempt < maxAttempts; ++attempt)
    {
        std::ostringstream oss;
        oss << prefix << std::hex << std::setfill('0') << std::setw(8) << rd() << std::setw(8)
            << rd();
        auto candidate = base / oss.str();
        std::error_code ec;
        bool const exists = std::filesystem::exists(candidate, ec);
        if (ec)
        {
            Throw<std::runtime_error>(
                "Unable to check path '" + candidate.string() + "': " + ec.message());
        }
        if (!exists)
            return candidate;
    }
    Throw<std::runtime_error>("Unable to generate a unique path under '" + base.string() + "'");
}

TempDir::TempDir() : path_(uniqueRandomPath(std::filesystem::temp_directory_path()))
{
    std::filesystem::create_directory(path_);
}

TempDir::~TempDir()
{
    // use non-throwing calls in the destructor
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    if (ec)
    {
        std::cerr << "Unable to remove temporary directory '" << path_.string()
                  << "': " << ec.message() << '\n';
    }
}

std::string
TempDir::path() const
{
    return path_.string();
}

std::string
TempDir::file(std::string const& name) const
{
    return (path_ / name).string();
}

}  // namespace xrpl
