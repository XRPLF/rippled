#pragma once

#include <cstddef>
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

/**
 * Generate a unique, non-existing path under @p base whose filename starts with
 * @p prefix and ends with a random hex suffix.
 *
 * Attempts up to @p maxAttempts paths. Throws `std::runtime_error` if a unique
 * path cannot be found or if the filesystem returns an error while checking for
 * existence.
 */
std::filesystem::path
uniqueRandomPath(
    std::filesystem::path const& base,
    std::string const& prefix = "",
    std::size_t maxAttempts = 100);

/**
 * RAII temporary directory.
 *
 * The directory and all its contents are deleted when
 * the instance of `TempDir` is destroyed.
 */
class TempDir
{
    std::filesystem::path path_;

public:
#if !GENERATING_DOCS
    TempDir(TempDir const&) = delete;
    TempDir&
    operator=(TempDir const&) = delete;
#endif

    /**
     * Construct a temporary directory.
     */
    TempDir();

    /**
     * Destroy a temporary directory.
     */
    ~TempDir();

    /**
     * Get the native path for the temporary directory.
     */
    [[nodiscard]] std::filesystem::path
    path() const;

    /**
     * Get the native path for a file.
     *
     * The file does not need to exist.
     */
    [[nodiscard]] std::filesystem::path
    file(std::filesystem::path const& name) const;
};

}  // namespace xrpl
