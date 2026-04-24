#include <xrpl/basics/FileUtilities.h>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
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

    std::ifstream fileStream(fullPath.string(), std::ios::in);

    if (!fileStream)
    {
        ec = make_error_code(static_cast<std::errc>(errno));
        return {};
    }

    std::string result{
        std::istreambuf_iterator<char>{fileStream}, std::istreambuf_iterator<char>{}};

    if (fileStream.bad())
    {
        ec = make_error_code(static_cast<std::errc>(errno));
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
    std::ofstream fileStream(destPath.string(), std::ios::out | std::ios::trunc);

    if (!fileStream)
    {
        ec = make_error_code(static_cast<std::errc>(errno));
        return;
    }

    fileStream << contents;

    if (fileStream.bad())
    {
        ec = make_error_code(static_cast<std::errc>(errno));
        return;
    }
}

}  // namespace xrpl
