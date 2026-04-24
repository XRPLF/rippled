#pragma once

#include <filesystem>
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
        do
        {
            std::ostringstream oss;
            oss << std::hex << rd() << rd();
            path_ = dir / oss.str();
        } while (std::filesystem::exists(path_));
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
    std::string
    path() const
    {
        return path_.string();
    }

    /** Get the native path for the a file.

        The file does not need to exist.
    */
    std::string
    file(std::string const& name) const
    {
        return (path_ / name).string();
    }
};

}  // namespace beast
