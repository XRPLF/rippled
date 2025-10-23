#ifndef XRPL_BASICS_FILEUTILITIES_H_INCLUDED
#define XRPL_BASICS_FILEUTILITIES_H_INCLUDED

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <optional>

namespace ripple {

std::string
getFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& sourcePath,
    std::optional<std::size_t> maxSize = std::nullopt);

void
writeFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& destPath,
    std::string const& contents);

}  // namespace ripple

#endif
