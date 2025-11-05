#ifndef BEAST_UTILITY_TEMP_DIR_H_INCLUDED
#define BEAST_UTILITY_TEMP_DIR_H_INCLUDED

#include <boost/filesystem.hpp>

#include <string>

namespace beast {

/** RAII temporary directory.

    The directory and all its contents are deleted when
    the instance of `temp_dir` is destroyed.
*/
class temp_dir
{
    boost::filesystem::path path_;

public:
#if !GENERATING_DOCS
    temp_dir(temp_dir const&) = delete;
    temp_dir&
    operator=(temp_dir const&) = delete;
#endif

    /// Construct a temporary directory.
    temp_dir()
    {
        auto const dir = boost::filesystem::temp_directory_path();
        do
        {
            path_ = dir / boost::filesystem::unique_path();
        } while (boost::filesystem::exists(path_));
        boost::filesystem::create_directory(path_);
    }

    /// Destroy a temporary directory.
    ~temp_dir()
    {
        // use non-throwing calls in the destructor
        boost::system::error_code ec;
        boost::filesystem::remove_all(path_, ec);
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

#endif
