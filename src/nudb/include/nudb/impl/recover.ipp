//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_RECOVER_IPP
#define NUDB_IMPL_RECOVER_IPP

#include <nudb/concepts.hpp>
#include <nudb/file.hpp>
#include <nudb/type_traits.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/bulkio.hpp>
#include <nudb/detail/format.hpp>
#include <boost/assert.hpp>
#include <algorithm>
#include <cstddef>
#include <string>

namespace nudb {

template<
    class Hasher,
    class File,
    class... Args>
void
recover(
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    error_code& ec,
    Args&&... args)
{
    static_assert(is_File<File>::value,
        "File requirements not met");
    static_assert(is_Hasher<Hasher>::value,
        "Hasher requirements not met");
    using namespace detail;

    // Open data file
    File df{args...};
    df.open(file_mode::write, dat_path, ec);
    if(ec)
        return;
    auto const dataFileSize = df.size(ec);
    if(ec)
        return;
    dat_file_header dh;
    read(df, dh, ec);
    if(ec)
        return;
    verify(dh, ec);
    if(ec)
        return;

    // Open key file
    File kf{args...};
    kf.open(file_mode::write, key_path, ec);
    if(ec)
        return;
    auto const keyFileSize = kf.size(ec);
    if(ec)
        return;
    if(keyFileSize <= key_file_header::size)
    {
        kf.close();
        erase_file(log_path, ec);
        if(ec)
            return;
        File::erase(key_path, ec);
        if(ec)
            return;
        ec = error::no_key_file;
        return;
    }

    // Open log file
    File lf{args...};
    lf.open(file_mode::append, log_path, ec);
    if(ec == errc::no_such_file_or_directory)
    {
        ec = {};
        return;
    }
    if(ec)
        return;
    auto const logFileSize = lf.size(ec);
    if(ec)
        return;
    // Read log file header
    log_file_header lh;
    read(lf, lh, ec);
    if(ec == error::short_read)
    {
        BOOST_ASSERT(keyFileSize > key_file_header::size);
        ec = {};
        goto clear_log;
    }
    if(ec)
        return;
    verify<Hasher>(lh, ec);
    if(ec)
        return;
    if(lh.key_file_size == 0)
        goto trunc_files;
    {
        // Read key file header
        key_file_header kh;
        read(kf, kh, ec);
        if(ec)
            return;
        verify<Hasher>(kh, ec);
        if(ec)
            return;
        verify<Hasher>(dh, kh, ec);
        if(ec)
            return;
        verify<Hasher>(kh, lh, ec);
        if(ec)
            return;

        auto const readSize = 1024 * kh.block_size;
        auto const bucketSize = bucket_size(kh.capacity);
        buffer buf{kh.block_size};
        bucket b{kh.block_size, buf.get()};
        bulk_reader<File> r{lf,
            log_file_header::size, logFileSize, readSize};
        while(! r.eof())
        {
            // Log Record
            auto is = r.prepare(field<std::uint64_t>::size, ec);
            // Log file is incomplete, so roll back.
            if(ec == error::short_read)
            {
                ec = {};
                break;
            }
            if(ec)
                return;
            nsize_t n;
            {
                std::uint64_t v;
                // VFALCO This should have been a uint32_t
                read<std::uint64_t>(is, v); // Index
                BOOST_ASSERT(v <= std::numeric_limits<std::uint32_t>::max());
                n = static_cast<nsize_t>(v);
            }
            b.read(r, ec);                  // Bucket
            if(ec == error::short_read)
            {
                ec = {};
                break;
            }
            if(b.spill() && b.spill() + bucketSize > dataFileSize)
            {
                ec = error::invalid_log_spill;
                return;
            }
            if(n > kh.buckets)
            {
                ec = error::invalid_log_index;
                return;
            }
            b.write(kf, static_cast<noff_t>(n + 1) * kh.block_size, ec);
            if(ec)
                return;
        }
    }
trunc_files:
    df.trunc(lh.dat_file_size, ec);
    if(ec)
        return;
    df.sync(ec);
    if(ec)
        return;
    if(lh.key_file_size != 0)
    {
        kf.trunc(lh.key_file_size, ec);
        if(ec)
            return;
        kf.sync(ec);
        if(ec)
            return;
    }
    else
    {
        kf.close();
        File::erase(key_path, ec);
        if(ec)
            return;
    }
clear_log:
    lf.trunc(0, ec);
    if(ec)
        return;
    lf.sync(ec);
    if(ec)
        return;
    lf.close();
    File::erase(log_path, ec);
    if(ec)
        return;
}

} // nudb

#endif
