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

#ifndef BEAST_NUDB_VISIT_H_INCLUDED
#define BEAST_NUDB_VISIT_H_INCLUDED

#include <beast/nudb/common.h>
#include <beast/nudb/file.h>
#include <beast/nudb/detail/buffer.h>
#include <beast/nudb/detail/bulkio.h>
#include <beast/nudb/detail/format.h>
#include <algorithm>
#include <cstddef>
#include <string>

namespace beast {
namespace nudb {

/** Visit each key/data pair in a database file.

    Function will be called with this signature:
        bool(void const* key, std::size_t key_size,
             void const* data, std::size_t size)

    If Function returns false, the visit is terminated.

    @return `true` if the visit completed
    This only requires the data file.
*/
template <class Codec, class Function>
bool
visit(
    path_type const& path,
    std::size_t read_size,
    Function&& f)
{
    using namespace detail;
    using File = native_file;
    File df;
    df.open (file_mode::scan, path);
    dat_file_header dh;
    read (df, dh);
    verify (dh);
    Codec codec;
    // Iterate Data File
    bulk_reader<File> r(
        df, dat_file_header::size,
            df.actual_size(), read_size);
    buffer buf;
    try
    {
        while (! r.eof())
        {
            // Data Record or Spill Record
            std::size_t size;
            auto is = r.prepare(
                field<uint48_t>::size); // Size
            read<uint48_t>(is, size);
            if (size > 0)
            {
                // Data Record
                is = r.prepare(
                    dh.key_size +           // Key
                    size);                  // Data
                std::uint8_t const* const key =
                    is.data(dh.key_size);
                auto const result = codec.decompress(
                    is.data(size), size, buf);
                if (! f(key, dh.key_size,
                        result.first, result.second))
                    return false;
            }
            else
            {
                // Spill Record
                is = r.prepare(
                    field<std::uint16_t>::size);
                read<std::uint16_t>(is, size);  // Size
                r.prepare(size); // skip bucket
            }
        }
    }
    catch (file_short_read_error const&)
    {
        throw store_corrupt_error(
            "nudb: data short read");
    }

    return true;
}

} // nudb
} // beast

#endif
