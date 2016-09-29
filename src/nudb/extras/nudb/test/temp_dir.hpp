//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_TEST_TEMP_DIR_HPP
#define NUDB_TEST_TEMP_DIR_HPP

#include <boost/filesystem.hpp>
#include <string>

namespace nudb {
namespace test {

/** RAII temporary directory path.

    The directory and all its contents are deleted when
    the instance of `temp_dir` is destroyed.
*/
class temp_dir
{
    boost::filesystem::path path_;

public:
    temp_dir(const temp_dir&) = delete;
    temp_dir& operator=(const temp_dir&) = delete;

    /// Construct a temporary directory.
    explicit
    temp_dir(boost::filesystem::path dir)
    {
        if (dir.empty())
            dir = boost::filesystem::temp_directory_path();

        do
        {
            path_ =
                dir / boost::filesystem::unique_path();
        }
        while(boost::filesystem::exists(path_));
        boost::filesystem::create_directory(path_);
    }

    /// Destroy a temporary directory.
    ~temp_dir()
    {
        boost::filesystem::remove_all(path_);
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

} // test
} // nudb

#endif
