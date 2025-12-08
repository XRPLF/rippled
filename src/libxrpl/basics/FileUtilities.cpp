#include <xrpl/basics/FileUtilities.h>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/errc.hpp>

#include <cerrno>
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>

namespace ripple {

std::string
getFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& sourcePath,
    std::optional<std::size_t> maxSize)
{
    using namespace boost::filesystem;
    using namespace boost::system::errc;

    path fullPath{canonical(sourcePath, ec)};
    if (ec)
        return {};

    if (maxSize && (file_size(fullPath, ec) > *maxSize || ec))
    {
        if (!ec)
            ec = make_error_code(file_too_large);
        return {};
    }

    std::ifstream fileStream(fullPath.string(), std::ios::in);

    if (!fileStream)
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return {};
    }

    std::string const result{
        std::istreambuf_iterator<char>{fileStream},
        std::istreambuf_iterator<char>{}};

    if (fileStream.bad())
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return {};
    }

    return result;
}

void
writeFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& destPath,
    std::string const& contents)
{
    using namespace boost::filesystem;
    using namespace boost::system::errc;

    std::ofstream fileStream(
        destPath.string(), std::ios::out | std::ios::trunc);

    if (!fileStream)
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return;
    }

    fileStream << contents;

    if (fileStream.bad())
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return;
    }
}

}  // namespace ripple
