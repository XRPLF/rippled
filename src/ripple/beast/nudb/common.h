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

#ifndef BEAST_NUDB_COMMON_H_INCLUDED
#define BEAST_NUDB_COMMON_H_INCLUDED

#include <stdexcept>
#include <string>

namespace beast {
namespace nudb {

// Commonly used types

enum class file_mode
{
    scan,         // read sequential
    read,         // read random
    append,       // read random, write append
    write         // read random, write random
};

using path_type = std::string;

// All exceptions thrown by nudb are derived
// from std::runtime_error except for fail_error

/** Thrown when a codec fails, e.g. corrupt data. */
struct codec_error : std::runtime_error
{
    template <class String>
    explicit
    codec_error (String const& s)
        : runtime_error(s)
    {
    }
};

/** Base class for all errors thrown by file classes. */
struct file_error : std::runtime_error
{
    template <class String>
    explicit
    file_error (String const& s)
        : runtime_error(s)
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

/** Thrown when end of istream reached while reading. */
struct short_read_error : std::runtime_error
{
    short_read_error()
        : std::runtime_error(
            "nudb: short read")
    {
    }
};

/** Base class for all exceptions thrown by store. */
class store_error : public std::runtime_error
{
public:
    template <class String>
    explicit
    store_error (String const& s)
        : runtime_error(s)
    {
    }
};

/** Thrown when corruption in a file is detected. */
class store_corrupt_error : public store_error
{
public:
    template <class String>
    explicit
    store_corrupt_error (String const& s)
        : store_error(s)
    {
    }
};

} // nudb
} // beast

#endif
