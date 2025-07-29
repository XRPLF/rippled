//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <xrpl/basics/FileUtilities.h>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/errc.hpp>

#include <cerrno>
#include <cstddef>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>

namespace ripple {

std::string
getFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& sourcePath,
    std::optional<std::size_t> maxSize)
{
    using namespace boost::filesystem;
    using namespace boost::system::errc;

    path fullPath{canonical(sourcePath, ec)};
    if (ec)
        return {};

    if (maxSize && (file_size(fullPath, ec) > *maxSize || ec))
    {
        if (!ec)
            ec = make_error_code(file_too_large);
        return {};
    }

    std::ifstream fileStream(fullPath.string(), std::ios::in);

    if (!fileStream)
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return {};
    }

    std::string const result{
        std::istreambuf_iterator<char>{fileStream},
        std::istreambuf_iterator<char>{}};

    if (fileStream.bad())
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return {};
    }

    return result;
}

void
writeFileContents(
    boost::system::error_code& ec,
    boost::filesystem::path const& destPath,
    std::string const& contents)
{
    using namespace boost::filesystem;
    using namespace boost::system::errc;

    std::ofstream fileStream(
        destPath.string(), std::ios::out | std::ios::trunc);

    if (!fileStream)
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return;
    }

    fileStream << contents;

    if (fileStream.bad())
    {
        ec = make_error_code(static_cast<errc_t>(errno));
        return;
    }
}

}  // namespace ripple
