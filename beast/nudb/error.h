//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_ERROR_H_INCLUDED
#define BEAST_NUDB_ERROR_H_INCLUDED

#include <beast/nudb/detail/config.h>
#include <beast/utility/noexcept.h>
#include <stdexcept>
#include <string>

namespace beast {
namespace nudb {

// All exceptions thrown by nudb are derived
// from std::exception except for fail_error

/** Base class for all errors thrown by file classes. */
struct file_error : std::runtime_error
{
    explicit
    file_error (char const* s)
        : std::runtime_error(s)
    {
    }

    explicit
    file_error (std::string const& s)
        : std::runtime_error(s)
    {
    }
};

/** Thrown when file bytes read are less than requested. */
struct file_short_read_error : file_error
{
    file_short_read_error()
        : file_error (
            "nudb: short read")
    {
    }
};

/** Thrown when file bytes written are less than requested. */
struct file_short_write_error : file_error
{
    file_short_write_error()
        : file_error (
            "nudb: short write")
    {
    }
};

/** Base class for all exceptions thrown by store. */
class store_error : public std::runtime_error
{
public:
    explicit
    store_error (char const* m)
        : std::runtime_error(
            std::string("nudb: ") + m)
    {
    }

    explicit
    store_error (std::string const& m)
        : std::runtime_error(
            std::string("nudb: ") + m)
    {
    }
};

/** Thrown when corruption in a file is detected. */
class store_corrupt_error : public store_error
{
public:
    explicit
    store_corrupt_error (char const* m)
        : store_error (m)
    {
    }

    explicit
    store_corrupt_error (std::string const& m)
        : store_error (m)
    {
    }
};

} // nudb
} // beast

#endif
