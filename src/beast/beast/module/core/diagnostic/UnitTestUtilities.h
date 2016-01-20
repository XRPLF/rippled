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

#ifndef BEAST_MODULE_CORE_DIAGNOSTIC_UNITTESTUTILITIES_H_INCLUDED
#define BEAST_MODULE_CORE_DIAGNOSTIC_UNITTESTUTILITIES_H_INCLUDED

#include <boost/filesystem.hpp>
#include <string>

namespace beast {
namespace UnitTestUtilities {

class TempDirectory
{
public:
    TempDirectory ()
    {
        auto const tempDir =
            boost::filesystem::temp_directory_path();

        do
        {
            tempPath =
                tempDir / boost::filesystem::unique_path();
        } while (boost::filesystem::exists(tempPath));

        boost::filesystem::create_directory (tempPath);
    }

    ~TempDirectory()
    {
        boost::filesystem::remove_all (tempPath);
    }

    /** Returns the native path for the temporary folder */
    std::string path() const
    {
        return tempPath.string();
    }

    /** Returns the native for the given file */
    std::string file (std::string const& name) const
    {
        return (tempPath / name).string();
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

private:
    boost::filesystem::path tempPath;
};

} // UnitTestUtilities
} // beast

#endif
