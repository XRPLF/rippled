#pragma once

#include <xrpl/basics/FileUtilities.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace xrpl {

/**
 * RAII temporary directory.
 *
 * The directory and all its contents are deleted when
 * the instance of `TempDir` is destroyed.
 */
class TempDir
{
    std::filesystem::path path_{uniqueRandomPath(std::filesystem::temp_directory_path())};

public:
#if !GENERATING_DOCS
    TempDir(TempDir const&) = delete;
    TempDir&
    operator=(TempDir const&) = delete;
#endif

    /**
     * Construct a temporary directory.
     */
    TempDir()
    {
        std::filesystem::create_directory(path_);
    }

    /**
     * Destroy a temporary directory.
     */
    ~TempDir()
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

    /**
     * Get the native path for the temporary directory
     */
    [[nodiscard]] std::string
    path() const
    {
        return path_.string();
    }

    /**
     * Get the native path for the a file.
     *
     * The file does not need to exist.
     */
    [[nodiscard]] std::string
    file(std::string const& name) const
    {
        return (path_ / name).string();
    }
};

}  // namespace xrpl
