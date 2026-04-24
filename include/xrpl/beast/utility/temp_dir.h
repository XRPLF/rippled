#pragma once

#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

namespace beast {

/** RAII temporary directory.

    The directory and all its contents are deleted when
    the instance of `temp_dir` is destroyed.
*/
class temp_dir
{
    std::filesystem::path path_;

public:
#if !GENERATING_DOCS
    temp_dir(temp_dir const&) = delete;
    temp_dir&
    operator=(temp_dir const&) = delete;
#endif

    /// Construct a temporary directory.
    temp_dir()
    {
        auto const dir = std::filesystem::temp_directory_path();
        std::random_device rd;
        constexpr std::size_t maxAttempts = 100;
        for (std::size_t attempt = 0; attempt < maxAttempts; ++attempt)
        {
            std::error_code ec;
            std::ostringstream oss;
            oss << std::hex << std::setfill('0') << std::setw(8) << rd() << std::setw(8) << rd();
            path_ = dir / oss.str();
            if (!std::filesystem::exists(path_, ec) && !ec)
                break;
            path_.clear();
        }
        if (path_.empty())
            throw std::runtime_error("Unable to generate a unique temporary directory path");
        std::filesystem::create_directory(path_);
    }

    /// Destroy a temporary directory.
    ~temp_dir()
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
