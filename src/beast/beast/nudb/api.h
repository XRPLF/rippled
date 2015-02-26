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

#ifndef BEAST_NUDB_API_H_INCLUDED
#define BEAST_NUDB_API_H_INCLUDED

#include <beast/nudb/create.h>
#include <beast/nudb/identity.h>
#include <beast/nudb/store.h>
#include <beast/nudb/recover.h>
#include <beast/nudb/verify.h>
#include <beast/nudb/visit.h>
#include <cstdint>

namespace beast {
namespace nudb {

// Convenience for consolidating template arguments
//
template <
    class Hasher,
    class Codec = identity,
    class File = native_file,
    std::size_t BufferSize = 16 * 1024 * 1024
>
struct api
{
    using hash_type = Hasher;
    using codec_type = Codec;
    using file_type = File;
    using store = nudb::store<Hasher, Codec, File>;

    static std::size_t const buffer_size = BufferSize;

    template <class... Args>
    static
    bool
    create (
        path_type const& dat_path,
        path_type const& key_path,
        path_type const& log_path,
        std::uint64_t appnum,
        std::uint64_t salt,
        std::size_t key_size,
        std::size_t block_size,
        float load_factor,
        Args&&... args)
    {
        return nudb::create<Hasher, Codec, File>(
            dat_path, key_path, log_path,
                appnum, salt, key_size, block_size,
                    load_factor, args...);
    }               

    template <class... Args>
    static
    bool
    recover (
        path_type const& dat_path,
        path_type const& key_path,
        path_type const& log_path,
        Args&&... args)
    {
        return nudb::recover<Hasher, Codec, File>(
            dat_path, key_path, log_path, BufferSize,
                args...);
    }

    static
    verify_info
    verify (
        path_type const& dat_path,
        path_type const& key_path)
    {
        return nudb::verify<Hasher>(
            dat_path, key_path, BufferSize);
    }
    
    template <class Function>
    static
    bool
    visit(
        path_type const& path,
        Function&& f)
    {
        return nudb::visit<Codec>(
            path, BufferSize, f);
    }
};

} // nudb
} // beast

#endif
