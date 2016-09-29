//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_VERIFY_IPP
#define NUDB_IMPL_VERIFY_IPP

#include <nudb/concepts.hpp>
#include <nudb/native_file.hpp>
#include <nudb/type_traits.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/bulkio.hpp>
#include <nudb/detail/format.hpp>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>

namespace nudb {

namespace detail {

// Normal verify that does not require a buffer
//
template<
    class Hasher,
    class File,
    class Progress>
void
verify_normal(
    verify_info& info,
    File& df,
    File& kf,
    dat_file_header& dh,
    key_file_header& kh,
    Progress&& progress,
    error_code& ec)
{
    static_assert(is_File<File>::value,
        "File requirements not met");
    static_assert(is_Hasher<Hasher>::value,
        "Hasher requirements not met");
    static_assert(is_Progress<Progress>::value,
        "Progress requirements not met");
    info.algorithm = 0;
    auto const readSize = 1024 * kh.block_size;

    // This ratio balances the 2 work phases.
    // The number is determined empirically.
    auto const adjust = 1.75;
    
    // Calculate the work required
    auto const keys = static_cast<std::uint64_t>(
        double(kh.load_factor) / 65536.0 * kh.buckets * kh.capacity);
    std::uint64_t const nwork = static_cast<std::uint64_t>(
        info.dat_file_size + keys * kh.block_size +
        adjust * (info.key_file_size + keys * kh.block_size));
    std::uint64_t work = 0;
    progress(0, nwork);

    // Iterate Data File
    // Data Record
    auto const dh_len =
        field<uint48_t>::size + // Size
        kh.key_size;            // Key
    std::uint64_t fetches = 0;
    buffer buf{kh.block_size + dh_len};
    bucket b{kh.block_size, buf.get()};
    std::uint8_t* pd = buf.get() + kh.block_size;
    {
        bulk_reader<File> r{df, dat_file_header::size,
            info.dat_file_size, readSize};
        while(! r.eof())
        {
            auto const offset = r.offset();
            // Data Record or Spill Record
            auto is = r.prepare(
                field<uint48_t>::size, ec); // Size
            if(ec)
                return;
            nsize_t size;
            read_size48(is, size);
            if(size > 0)
            {
                // Data Record
                is = r.prepare(
                    kh.key_size +           // Key
                    size, ec);              // Data
                if(ec)
                    return;
                std::uint8_t const* const key =
                    is.data(kh.key_size);
                std::uint8_t const* const data =
                    is.data(size);
                (void)data;
                auto const h = hash<Hasher>(
                    key, kh.key_size, kh.salt);
                // Check bucket and spills
                auto const n = bucket_index(
                    h, kh.buckets, kh.modulus);
                b.read(kf,
                       static_cast<noff_t>(n + 1) * kh.block_size, ec);
                if(ec)
                    return;
                work += kh.block_size;
                ++fetches;
                for(;;)
                {
                    for(auto i = b.lower_bound(h);
                        i < b.size(); ++i)
                    {
                        auto const item = b[i];
                        if(item.hash != h)
                            break;
                        if(item.offset == offset)
                            goto found;
                        ++fetches;
                    }
                    auto const spill = b.spill();
                    if(! spill)
                    {
                        ec = error::orphaned_value;
                        return;
                    }
                    b.read(df, spill, ec);
                    if(ec == error::short_read)
                    {
                        ec = error::short_spill;
                        return;
                    }
                    if(ec)
                        return;
                    ++fetches;
                }
            found:
                // Update
                ++info.value_count;
                info.value_bytes += size;
            }
            else
            {
                // Spill Record
                is = r.prepare(
                    field<std::uint16_t>::size, ec);
                if(ec == error::short_read)
                {
                    ec = error::short_spill;
                    return;
                }
                if(ec)
                    return;
                read<std::uint16_t>(is, size);  // Size
                if(size != info.bucket_size)
                {
                    ec = error::invalid_spill_size;
                    return;
                }
                if(ec)
                    return;
                b.read(r, ec);                  // Bucket
                if(ec == error::short_read)
                {
                    ec = error::short_spill;
                    return;
                }
                if(ec)
                    return;
                ++info.spill_count_tot;
                info.spill_bytes_tot +=
                    field<uint48_t>::size +     // Zero
                    field<uint16_t>::size +     // Size
                    b.actual_size();            // Bucket
            }
            progress(work + offset, nwork);
        }
        work += info.dat_file_size;
    }

    // Iterate Key File
    {
        for(std::size_t n = 0; n < kh.buckets; ++n)
        {
            std::size_t nspill = 0;
            b.read(kf, static_cast<noff_t>(
                n + 1) * kh.block_size, ec);
            if(ec)
                return;
            work += static_cast<std::uint64_t>(
                adjust * kh.block_size);
            bool spill = false;
            for(;;)
            {
                info.key_count += b.size();
                for(nkey_t i = 0; i < b.size(); ++i)
                {
                    auto const e = b[i];
                    df.read(e.offset, pd, dh_len, ec);
                    if(ec == error::short_read)
                    {
                        ec = error::missing_value;
                        return;
                    }
                    if(ec)
                        return;
                    if(! spill)
                        work += static_cast<std::uint64_t>(
                            adjust * kh.block_size);
                    // Data Record
                    istream is{pd, dh_len};
                    std::uint64_t size;
                    // VFALCO This should really be a 32-bit field
                    read<uint48_t>(is, size);   // Size
                    void const* key =
                        is.data(kh.key_size);   // Key
                    if(size != e.size)
                    {
                        ec = error::size_mismatch;
                        return;
                    }
                    auto const h = hash<Hasher>(key,
                        kh.key_size, kh.salt);
                    if(h != e.hash)
                    {
                        ec = error::hash_mismatch;
                        return;
                    }
                }
                if(! b.spill())
                    break;
                b.read(df, b.spill(), ec);
                if(ec)
                    return;
                spill = true;
                ++nspill;
                ++info.spill_count;
                info.spill_bytes +=
                    field<uint48_t>::size + // Zero
                    field<uint16_t>::size + // Size
                    b.actual_size();        // SpillBucket
            }
            if(nspill >= info.hist.size())
                nspill = info.hist.size() - 1;
            ++info.hist[nspill];
            progress(work, nwork);
        }
    }
    float sum = 0;
    for(size_t i = 0; i < info.hist.size(); ++i)
        sum += info.hist[i] * (i + 1);
    if(info.value_count)
        info.avg_fetch =
            float(fetches) / info.value_count;
    else
        info.avg_fetch = 0;
    info.waste = (info.spill_bytes_tot - info.spill_bytes) /
        float(info.dat_file_size);
    if(info.value_count)
        info.overhead =
            float(info.key_file_size + info.dat_file_size) /
            (
                info.value_bytes +
                info.key_count *
                    (info.key_size +
                    // Data Record
                     field<uint48_t>::size) // Size
                        ) - 1;
    else
        info.overhead = 0;
    info.actual_load = info.key_count / float(
        info.capacity * info.buckets);
}

// Fast version of verify that uses a buffer
//
template<class Hasher, class File, class Progress>
void
verify_fast(
    verify_info& info,
    File& df,
    File& kf,
    dat_file_header& dh,
    key_file_header& kh,
    std::size_t bufferSize,
    Progress&& progress,
    error_code& ec)
{
    info.algorithm = 1;
    auto const readSize = 1024 * kh.block_size;

    // Counts unverified keys per bucket
    if(kh.buckets > std::numeric_limits<nbuck_t>::max())
    {
        ec = error::too_many_buckets;
        return;
    }
    std::unique_ptr<nkey_t[]> nkeys(
        new nkey_t[kh.buckets]);

    // Verify contiguous sequential sections of the
    // key file using multiple passes over the data.
    //
    if(bufferSize < 2 * kh.block_size + sizeof(nkey_t))
        throw std::logic_error("invalid buffer size");
    auto chunkSize = std::min(kh.buckets,
        (bufferSize - kh.block_size) /
            (kh.block_size + sizeof(nkey_t)));
    auto const passes =
        (kh.buckets + chunkSize - 1) / chunkSize;

    // Calculate the work required
    std::uint64_t work = 0;
    std::uint64_t const nwork =
        passes * info.dat_file_size + info.key_file_size;
    progress(0, nwork);

    std::uint64_t fetches = 0;
    buffer buf{(chunkSize + 1) * kh.block_size};
    bucket tmp{kh.block_size,
        buf.get() + chunkSize * kh.block_size};
    for(nsize_t b0 = 0; b0 < kh.buckets; b0 += chunkSize)
    {
        // Load key file chunk to buffer
        auto const b1 = std::min(b0 + chunkSize, kh.buckets);
        // Buffered range is [b0, b1)
        auto const bn = b1 - b0;
        kf.read(
            static_cast<noff_t>(b0 + 1) * kh.block_size,
            buf.get(),
            static_cast<noff_t>(bn * kh.block_size),
            ec);
        if(ec)
            return;
        work += bn * kh.block_size;
        progress(work, nwork);
        // Count keys in buckets, including spills
        for(nbuck_t i = 0 ; i < bn; ++i)
        {
            bucket b{kh.block_size,
                buf.get() + i * kh.block_size};
            nkeys[i] = b.size();
            std::size_t nspill = 0;
            auto spill = b.spill();
            while(spill != 0)
            {
                tmp.read(df, spill, ec);
                if(ec == error::short_read)
                {
                    ec = error::short_spill;
                    return;
                }
                if(ec)
                    return;
                nkeys[i] += tmp.size();
                spill = tmp.spill();
                ++nspill;
                ++info.spill_count;
                info.spill_bytes +=
                    field<uint48_t>::size + // Zero
                    field<uint16_t>::size + // Size
                    tmp.actual_size();      // SpillBucket
            }
            if(nspill >= info.hist.size())
                nspill = info.hist.size() - 1;
            ++info.hist[nspill];
            info.key_count += nkeys[i];
        }
        // Iterate Data File
        bulk_reader<File> r(df, dat_file_header::size,
            info.dat_file_size, readSize);
        while(! r.eof())
        {
            auto const offset = r.offset();
            // Data Record or Spill Record
            auto is = r.prepare(
                field<uint48_t>::size, ec); // Size
            if(ec == error::short_read)
            {
                ec = error::short_data_record;
                return;
            }
            if(ec)
                return;
            nsize_t size;
            detail::read_size48(is, size);
            if(size > 0)
            {
                // Data Record
                is = r.prepare(
                    kh.key_size +           // Key
                    size, ec);              // Data
                if(ec == error::short_read)
                {
                    ec = error::short_value;
                    return;
                }
                if(ec)
                    return;
                std::uint8_t const* const key =
                    is.data(kh.key_size);
                std::uint8_t const* const data =
                    is.data(size);
                (void)data;
                auto const h = hash<Hasher>(
                    key, kh.key_size, kh.salt);
                auto const n = bucket_index(
                    h, kh.buckets, kh.modulus);
                if(n < b0 || n >= b1)
                    continue;
                // Check bucket and spills
                bucket b{kh.block_size, buf.get() +
                    (n - b0) * kh.block_size};
                ++fetches;
                for(;;)
                {
                    for(auto i = b.lower_bound(h);
                        i < b.size(); ++i)
                    {
                        auto const item = b[i];
                        if(item.hash != h)
                            break;
                        if(item.offset == offset)
                            goto found;
                        ++fetches;
                    }
                    auto const spill = b.spill();
                    if(! spill)
                    {
                        ec = error::orphaned_value;
                        return;
                    }
                    b = tmp;
                    b.read(df, spill, ec);
                    if(ec == error::short_read)
                    {
                        ec = error::short_spill;
                        return;
                    }
                    if(ec)
                        return;
                    ++fetches;
                }
            found:
                // Update
                ++info.value_count;
                info.value_bytes += size;
                if(nkeys[n - b0]-- == 0)
                {
                    ec = error::orphaned_value;
                    return;
                }
            }
            else
            {
                // Spill Record
                is = r.prepare(
                    field<std::uint16_t>::size, ec);
                if(ec == error::short_read)
                {
                    ec = error::short_spill;
                    return;
                }
                if(ec)
                    return;
                read<std::uint16_t>(is, size);      // Size
                if(bucket_size(
                    bucket_capacity(size)) != size)
                {
                    ec = error::invalid_spill_size;
                    return;
                }
                r.prepare(size, ec);                // Bucket
                if(ec == error::short_read)
                {
                    ec = error::short_spill;
                    return;
                }
                if(ec)
                    return;
                if(b0 == 0)
                {
                    ++info.spill_count_tot;
                    info.spill_bytes_tot +=
                        field<uint48_t>::size +     // Zero
                        field<uint16_t>::size +     // Size
                        tmp.actual_size();          // Bucket
                }
            }
            progress(work + offset, nwork);
        }
        // Make sure every key in every bucket was visited
        for(std::size_t i = 0; i < bn; ++i)
        {
            if(nkeys[i] != 0)
            {
                ec = error::missing_value;
                return;
            }
        }
        work += info.dat_file_size;
    }

    float sum = 0;
    for(std::size_t i = 0; i < info.hist.size(); ++i)
        sum += info.hist[i] * (i + 1);
    if(info.value_count)
        info.avg_fetch =
            float(fetches) / info.value_count;
    else
        info.avg_fetch = 0;
    info.waste = (info.spill_bytes_tot - info.spill_bytes) /
        float(info.dat_file_size);
    if(info.value_count)
        info.overhead =
            float(info.key_file_size + info.dat_file_size) /
            (
                info.value_bytes +
                info.key_count *
                    (info.key_size +
                    // Data Record
                     field<uint48_t>::size) // Size
                        ) - 1;
    else
        info.overhead = 0;
    info.actual_load = info.key_count / float(
        info.capacity * info.buckets);
}

} // detail

template<class Hasher, class Progress>
void
verify(
    verify_info& info,
    path_type const& dat_path,
    path_type const& key_path,
    std::size_t bufferSize,
    Progress&& progress,
    error_code& ec)
{
    static_assert(is_Hasher<Hasher>::value,
        "Hasher requirements not met");
    static_assert(is_Progress<Progress>::value,
        "Progress requirements not met");
    info = {};
    using namespace detail;
    using File = native_file;
    File df;
    df.open(file_mode::scan, dat_path, ec);
    if(ec)
        return;
    File kf;
    kf.open (file_mode::read, key_path, ec);
    if(ec)
        return;
    dat_file_header dh;
    read(df, dh, ec);
    if(ec)
        return;
    verify(dh, ec);
    if(ec)
        return;
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
    info.dat_path = dat_path;
    info.key_path = key_path;
    info.version = dh.version;
    info.uid = dh.uid;
    info.appnum = dh.appnum;
    info.key_size = dh.key_size;
    info.salt = kh.salt;
    info.pepper = kh.pepper;
    info.block_size = kh.block_size;
    info.load_factor = kh.load_factor / 65536.f;
    info.capacity = kh.capacity;
    info.buckets = kh.buckets;
    info.bucket_size = bucket_size(kh.capacity);
    info.key_file_size = kf.size(ec);
    if(ec)
        return;
    info.dat_file_size = df.size(ec);
    if(ec)
        return;

    // Determine which algorithm requires the least amount
    // of file I/O given the available buffer size
    std::size_t chunkSize;
    if(bufferSize >= 2 * kh.block_size + sizeof(nkey_t))
        chunkSize = std::min(kh.buckets,
            (bufferSize - kh.block_size) /
                (kh.block_size + sizeof(nkey_t)));
    else
        chunkSize = 0;
    std::size_t passes;
    if(chunkSize > 0)
        passes = (kh.buckets + chunkSize - 1) / chunkSize;
    else
        passes = 0;
    if(! chunkSize ||
        ((
            info.dat_file_size +
            (kh.buckets * kh.load_factor * kh.capacity * kh.block_size) +
            info.key_file_size
        ) < (
            passes * info.dat_file_size + info.key_file_size
        )))
    {
        detail::verify_normal<Hasher>(info,
            df, kf, dh, kh, progress, ec);
    }
    else
    {
        detail::verify_fast<Hasher>(info,
            df, kf, dh, kh, bufferSize, progress, ec);
    }
}

} // nudb

#endif
