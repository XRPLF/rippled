//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

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
    temp_dir(const temp_dir&) = delete;
    temp_dir&
    operator=(const temp_dir&) = delete;
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
