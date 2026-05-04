#pragma once

#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace beast {

/** Generate a unique, non-existing path under @p base with a random hex suffix.

    Attempts up to @p maxAttempts paths. Throws `std::runtime_error` if a
    unique path cannot be found or if the filesystem returns an error while
    checking for existence.
*/
inline std::filesystem::path
uniqueRandomPath(std::filesystem::path const& base, std::size_t maxAttempts = 100)
{
    std::random_device rd;
    for (std::size_t attempt = 0; attempt < maxAttempts; ++attempt)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(8) << rd() << std::setw(8) << rd();
        auto candidate = base / oss.str();
        std::error_code ec;
        bool const exists = std::filesystem::exists(candidate, ec);
        if (ec)
        {
            throw std::runtime_error(
                "Unable to check path '" + candidate.string() + "': " + ec.message());
        }
        if (!exists)
            return candidate;
    }
    throw std::runtime_error(
        "Unable to generate a unique path under '" + base.string() + "'");
}

/** RAII temporary directory.

    The directory and all its contents are deleted when
    the instance of `TempDir` is destroyed.
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

    /// Construct a temporary directory.
    TempDir()
    {
        path_ = uniqueRandomPath(std::filesystem::temp_directory_path());
        std::filesystem::create_directory(path_);
    }

    /// Destroy a temporary directory.
    ~TempDir()
    {
        // use non-throwing calls in the destructor
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        // TODO: warn/notify if ec set ?
    }

    /// Get the native path for the temporary directory
    [[nodiscard]] std::string
    path() const
    {
        return path_.string();
    }

    /** Get the native path for the a file.

        The file does not need to exist.
    */
    [[nodiscard]] std::string
    file(std::string const& name) const
    {
        return (path_ / name).string();
    }
};

}  // namespace beast
