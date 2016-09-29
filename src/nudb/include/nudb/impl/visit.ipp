//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_VISIT_IPP
#define NUDB_IMPL_VISIT_IPP

#include <nudb/concepts.hpp>
#include <nudb/error.hpp>
#include <nudb/type_traits.hpp>
#include <nudb/native_file.hpp>
#include <nudb/detail/bulkio.hpp>
#include <nudb/detail/format.hpp>
#include <algorithm>
#include <cstddef>
#include <string>

namespace nudb {

template<
    class Callback,
    class Progress>
void
visit(
    path_type const& path,
    Callback&& callback,
    Progress&& progress,
    error_code& ec)
{
    // VFALCO Need concept check for Callback
    static_assert(is_Progress<Progress>::value,
        "Progress requirements not met");
    using namespace detail;
    using File = native_file;
    auto const readSize = 1024 * block_size(path);
    File df;
    df.open(file_mode::scan, path, ec);
    if(ec)
        return;
    dat_file_header dh;
    read(df, dh, ec);
    if(ec)
        return;
    verify(dh, ec);
    if(ec)
        return;
    auto const fileSize = df.size(ec);
    if(ec)
        return;
    bulk_reader<File> r(df,
        dat_file_header::size, fileSize, readSize);
    progress(0, fileSize);
    while(! r.eof())
    {
        // Data Record or Spill Record
        nsize_t size;
        auto is = r.prepare(
            field<uint48_t>::size, ec); // Size
        if(ec)
            return;
        detail::read_size48(is, size);
        if(size > 0)
        {
            // Data Record
            is = r.prepare(
                dh.key_size +           // Key
                size, ec);              // Data
            std::uint8_t const* const key =
                is.data(dh.key_size);
            callback(key, dh.key_size,
                is.data(size), size, ec);
            if(ec)
                return;
        }
        else
        {
            // Spill Record
            is = r.prepare(
                field<std::uint16_t>::size, ec);
            if(ec)
                return;
            read<std::uint16_t>(is, size);  // Size
            r.prepare(size, ec); // skip bucket
            if(ec)
                return;
        }
        progress(r.offset(), fileSize);
    }
}

} // nudb

#endif
