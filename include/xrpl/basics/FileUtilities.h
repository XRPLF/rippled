#pragma once

<<<<<<< HEAD
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
=======
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <optional>
#include <string>
>>>>>>> origin/develop

namespace xrpl {

std::string
getFileContents(
    std::error_code& ec,
    std::filesystem::path const& sourcePath,
    std::optional<std::size_t> maxSize = std::nullopt);

void
writeFileContents(
    std::error_code& ec,
    std::filesystem::path const& destPath,
    std::string const& contents);

}  // namespace xrpl
