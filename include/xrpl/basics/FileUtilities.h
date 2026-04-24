#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

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
