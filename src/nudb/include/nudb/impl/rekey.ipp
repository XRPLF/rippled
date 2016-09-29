//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_REKEY_IPP
#define NUDB_IMPL_REKEY_IPP

#include <nudb/concepts.hpp>
#include <nudb/create.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/bulkio.hpp>
#include <nudb/detail/format.hpp>
#include <cmath>

namespace nudb {

// VFALCO Should this delete the key file on an error?
template<
    class Hasher,
    class File,
    class Progress,
    class... Args
>
void
rekey(
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    std::size_t blockSize,
    float loadFactor,
    std::uint64_t itemCount,
    std::size_t bufferSize,
    error_code& ec,
    Progress&& progress,
    Args&&... args)
{
    static_assert(is_File<File>::value,
        "File requirements not met");
    static_assert(is_Hasher<Hasher>::value,
        "Hasher requirements not met");
    static_assert(is_Progress<Progress>::value,
        "Progress requirements not met");
    using namespace detail;
    auto const readSize = 1024 * block_size(dat_path);
    auto const writeSize = 16 * block_size(key_path);

    // Open data file for reading and appending
    File df{args...};
    df.open(file_mode::append, dat_path, ec);
    if(ec)
        return;
    dat_file_header dh;
    read(df, dh, ec);
    if(ec)
        return;
    verify(dh, ec);
    if(ec)
        return;
    auto const dataFileSize = df.size(ec);
    if(ec)
        return;

    // Make sure log file doesn't exist
    File lf{args...};
    lf.open(file_mode::read, log_path, ec);
    if(! ec)
        ec = error::log_file_exists;
    if(ec != errc::no_such_file_or_directory)
        return;
    ec = {};

    // Set up key file header
    key_file_header kh;
    kh.version = currentVersion;
    kh.uid = dh.uid;
    kh.appnum = dh.appnum;
    kh.key_size = dh.key_size;
    kh.salt = make_salt();
    kh.pepper = pepper<Hasher>(kh.salt);
    kh.block_size = blockSize;
    kh.load_factor = std::min<std::size_t>(
        static_cast<std::size_t>(65536.0 * loadFactor), 65535);
    kh.buckets = static_cast<std::size_t>(
        std::ceil(itemCount /(
            bucket_capacity(kh.block_size) * loadFactor)));
    kh.modulus = ceil_pow2(kh.buckets);
    // Create key file
    File kf{args...};
    kf.create(file_mode::write, key_path, ec);
    if(ec)
        return;
    // Write key file header
    // Note, file size is less than any valid block_size here
    {
        std::array<std::uint8_t, key_file_header::size> buf;
        ostream os{buf.data(), buf.size()};
        write(os, kh);
        kf.write(0, buf.data(), buf.size(), ec);
        if(ec)
            return;
        kf.sync(ec);
        if(ec)
            return;
    }

    // Create log file
    lf.create(file_mode::append, log_path, ec);
    if(ec)
        return;
    // Write log file header
    {
        log_file_header lh;
        lh.version = currentVersion;            // Version
        lh.uid = kh.uid;                        // UID
        lh.appnum = kh.appnum;                  // Appnum
        lh.key_size = kh.key_size;              // Key Size
        lh.salt = kh.salt;                      // Salt
        lh.pepper = pepper<Hasher>(kh.salt);    // Pepper
        lh.block_size = kh.block_size;          // Block Size
        lh.key_file_size = 0;                   // Key File Size
        lh.dat_file_size = dataFileSize;        // Data File Size
        write(lf, lh, ec);
        if(ec)
            return;
        lf.sync(ec);
        if(ec)
            return;
    }

    // Create full key file
    buffer buf{kh.block_size};
    {
        // Write key file header
        std::memset(buf.get(), 0, kh.block_size);
        ostream os{buf.get(), kh.block_size};
        write(os, kh);
        kf.write(0, buf.get(), buf.size(), ec);
        if(ec)
            return;
        kf.sync(ec);
        if(ec)
            return;
        // Pre-allocate space for the entire key file
        std::uint8_t zero = 0;
        kf.write(
            static_cast<noff_t>(kh.buckets + 1) * kh.block_size - 1,
                &zero, 1, ec);
        if(ec)
            return;
        kf.sync(ec);
        if(ec)
            return;
    }
    
    // Build contiguous sequential sections of the
    // key file using multiple passes over the data.
    //
    auto const chunkSize = std::max<std::size_t>(1,
        bufferSize / kh.block_size);
    // Calculate work required
    auto const passes =
       (kh.buckets + chunkSize - 1) / chunkSize;
    auto const nwork = passes * dataFileSize;
    progress(0, nwork);

    buf.reserve(chunkSize * kh.block_size);
    bulk_writer<File> dw{df, dataFileSize, writeSize};
    for(nbuck_t b0 = 0; b0 < kh.buckets; b0 += chunkSize)
    {
        auto const b1 = std::min<std::size_t>(b0 + chunkSize, kh.buckets);
        // Buffered range is [b0, b1)
        auto const bn = b1 - b0;
        // Create empty buckets
        for(std::size_t i = 0; i < bn; ++i)
            bucket b{kh.block_size,
                buf.get() + i * kh.block_size, empty};
        // Insert all keys into buckets
        // Iterate Data File
        bulk_reader<File> r{df,
            dat_file_header::size, dataFileSize, readSize};
        while(! r.eof())
        {
            auto const offset = r.offset();
            // Data Record or Spill Record
            nsize_t size;
            auto is = r.prepare(
                field<uint48_t>::size, ec); // Size
            if(ec)
                return;
            progress((b0 / chunkSize) * dataFileSize + r.offset(), nwork);
            read_size48(is, size);
            if(size > 0)
            {
                // Data Record
                is = r.prepare(
                    dh.key_size +           // Key
                    size, ec);              // Data
                if(ec)
                    return;
                std::uint8_t const* const key =
                    is.data(dh.key_size);
                auto const h = hash<Hasher>(
                    key, dh.key_size, kh.salt);
                auto const n = bucket_index(
                    h, kh.buckets, kh.modulus);
                if(n < b0 || n >= b1)
                    continue;
                bucket b{kh.block_size, buf.get() +
                   (n - b0) * kh.block_size};
                maybe_spill(b, dw, ec);
                if(ec)
                    return;
                b.insert(offset, size, h);
            }
            else
            {
                // VFALCO Should never get here
                // Spill Record
                is = r.prepare(
                    field<std::uint16_t>::size, ec);
                if(ec)
                    return;
                read<std::uint16_t>(is, size);  // Size
                r.prepare(size, ec); // skip
                if(ec)
                    return;
            }
        }
        kf.write((b0 + 1) * kh.block_size, buf.get(),
            static_cast<std::size_t>(bn * kh.block_size), ec);
        if(ec)
            return;
    }
    dw.flush(ec);
    if(ec)
        return;
    lf.close();
    File::erase(log_path, ec);
    if(ec)
        return;
}

} // nudb

#endif
