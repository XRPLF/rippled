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

#ifndef BEAST_NUDB_VERIFY_H_INCLUDED
#define BEAST_NUDB_VERIFY_H_INCLUDED

#include <beast/nudb/common.h>
#include <beast/nudb/file.h>
#include <beast/nudb/detail/bucket.h>
#include <beast/nudb/detail/bulkio.h>
#include <beast/nudb/detail/format.h>
#include <algorithm>
#include <cstddef>
#include <string>

namespace beast {
namespace nudb {

/** Reports database information during verify mode. */
struct verify_info
{
    // Configured
    std::size_t version = 0;            // API version
    std::size_t uid = 0;                // UID
    std::size_t appnum = 0;             // Appnum
    std::size_t key_size = 0;           // Size of a key in bytes
    std::size_t salt = 0;               // Salt
    std::size_t pepper = 0;             // Pepper
    std::size_t block_size = 0;         // Block size in bytes
    float load_factor = 0;              // Target bucket fill fraction

    // Calculated
    std::size_t capacity = 0;           // Max keys per bucket
    std::size_t buckets = 0;            // Number of buckets
    std::size_t bucket_size = 0;        // Size of bucket in bytes

    // Measured
    std::size_t key_file_size = 0;      // Key file size in bytes
    std::size_t dat_file_size = 0;      // Data file size in bytes
    std::size_t key_count = 0;          // Keys in buckets and active spills
    std::size_t value_count = 0;        // Count of values in the data file
    std::size_t value_bytes = 0;        // Sum of value bytes in the data file
    std::size_t spill_count = 0;        // used number of spill records
    std::size_t spill_count_tot = 0;    // Number of spill records in data file
    std::size_t spill_bytes = 0;        // used byte of spill records
    std::size_t spill_bytes_tot = 0;    // Sum of spill record bytes in data file

    // Performance
    float avg_fetch = 0;                // average reads per fetch (excluding value)
    float waste = 0;                    // fraction of data file bytes wasted (0..100)
    float overhead = 0;                 // percent of extra bytes per byte of value
    float actual_load = 0;              // actual bucket fill fraction

    // number of buckets having n spills
    std::array<std::size_t, 10> hist;

    verify_info()
    {
        hist.fill(0);
    }
};

/** Verify consistency of the key and data files.
    Effects:
        Opens the key and data files in read-only mode.
        Throws file_error if a file can't be opened.
        Iterates the key and data files, throws store_corrupt_error
            on broken invariants.
*/
template <class Hasher>
verify_info
verify (
    path_type const& dat_path,
    path_type const& key_path,
    std::size_t read_size)
{
    using namespace detail;
    using File = native_file;
    File df;
    File kf;
    if (! df.open (file_mode::scan, dat_path))
        throw store_corrupt_error(
            "no data file");
    if (! kf.open (file_mode::read, key_path))
        throw store_corrupt_error(
            "no key file");
    key_file_header kh;
    dat_file_header dh;
    read (df, dh);
    read (kf, kh);
    verify(dh);
    verify<Hasher>(dh, kh);

    verify_info info;
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
    info.bucket_size = kh.bucket_size;
    info.key_file_size = kf.actual_size();
    info.dat_file_size = df.actual_size();

    // Data Record
    auto const dh_len =
        field<uint48_t>::size + // Size
        kh.key_size;            // Key

    std::size_t fetches = 0;

    // Iterate Data File
    buffer buf (kh.block_size + dh_len);
    bucket b (kh.block_size, buf.get());
    std::uint8_t* pd = buf.get() + kh.block_size;
    {
        bulk_reader<File> r(df,
            dat_file_header::size,
                df.actual_size(), read_size);
        while (! r.eof())
        {
            auto const offset = r.offset();
            // Data Record or Spill Record
            auto is = r.prepare(
                field<uint48_t>::size); // Size
            std::size_t size;
            read<uint48_t>(is, size);
            if (size > 0)
            {
                // Data Record
                is = r.prepare(
                    kh.key_size +           // Key
                    size);                  // Data
                std::uint8_t const* const key =
                    is.data(kh.key_size);
                std::uint8_t const* const data =
                    is.data(size);
                (void)data;
                auto const h = hash<Hasher>(
                    key, kh.key_size, kh.salt);
                // Check bucket and spills
                try
                {
                    auto const n = bucket_index(
                        h, kh.buckets, kh.modulus);
                    b.read (kf, (n + 1) * kh.block_size);
                    ++fetches;
                }
                catch (file_short_read_error const&)
                {
                    throw store_corrupt_error(
                        "short bucket");
                }
                for (;;)
                {
                    for (auto i = b.lower_bound(h);
                        i < b.size(); ++i)
                    {
                        auto const item = b[i];
                        if (item.hash != h)
                            break;
                        if (item.offset == offset)
                            goto found;
                        ++fetches;
                    }
                    auto const spill = b.spill();
                    if (! spill)
                        throw store_corrupt_error(
                            "orphaned value");
                    try
                    {
                        b.read (df, spill);
                        ++fetches;
                    }
                    catch (file_short_read_error const&)
                    {
                        throw store_corrupt_error(
                            "short spill");
                    }
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
                    field<std::uint16_t>::size);
                read<std::uint16_t>(is, size);  // Size
                if (size != kh.bucket_size)
                    throw store_corrupt_error(
                        "bad spill size");
                b.read(r);                      // Bucket
                ++info.spill_count_tot;
                info.spill_bytes_tot +=
                    field<uint48_t>::size +     // Zero
                    field<uint16_t>::size +     // Size
                    b.compact_size();           // Bucket
            }
        }
    }

    // Iterate Key File
    {
        for (std::size_t n = 0; n < kh.buckets; ++n)
        {
            std::size_t nspill = 0;
            b.read (kf, (n + 1) * kh.block_size);
            for(;;)
            {
                info.key_count += b.size();
                for (std::size_t i = 0; i < b.size(); ++i)
                {
                    auto const e = b[i];
                    try
                    {
                        df.read (e.offset, pd, dh_len);
                    }
                    catch (file_short_read_error const&)
                    {
                        throw store_corrupt_error(
                            "missing value");
                    }
                    // Data Record
                    istream is(pd, dh_len);
                    std::size_t size;
                    read<uint48_t>(is, size);   // Size
                    void const* key =
                        is.data(kh.key_size);   // Key
                    if (size != e.size)
                        throw store_corrupt_error(
                            "wrong size");
                    auto const h = hash<Hasher>(key,
                        kh.key_size, kh.salt);
                    if (h != e.hash)
                        throw store_corrupt_error(
                            "wrong hash");
                }
                if (! b.spill())
                    break;
                try
                {
                    b.read (df, b.spill());
                    ++nspill;
                    ++info.spill_count;
                    info.spill_bytes +=
                        field<uint48_t>::size + // Zero
                        field<uint16_t>::size + // Size
                        b.compact_size();       // SpillBucket
                }
                catch (file_short_read_error const&)
                {
                    throw store_corrupt_error(
                        "missing spill");
                }
            }
            if (nspill >= info.hist.size())
                nspill = info.hist.size() - 1;
            ++info.hist[nspill];
        }
    }

    float sum = 0;
    for (int i = 0; i < info.hist.size(); ++i)
        sum += info.hist[i] * (i + 1);
    //info.avg_fetch = sum / info.buckets;
    info.avg_fetch = float(fetches) / info.value_count;
    info.waste = (info.spill_bytes_tot - info.spill_bytes) /
        float(info.dat_file_size);
    info.overhead =
        float(info.key_file_size + info.dat_file_size) /
        (
            info.value_bytes +
            info.key_count *
                (info.key_size +
                // Data Record
                 field<uint48_t>::size) // Size
                    ) - 1;
    info.actual_load = info.key_count / float(
        info.capacity * info.buckets);
    return info;
}

/** Verify consistency of the key and data files.
    Effects:
        Opens the key and data files in read-only mode.
        Throws file_error if a file can't be opened.
        Iterates the key and data files, throws store_corrupt_error
            on broken invariants.
    This uses a different algorithm that depends on allocating
    a large buffer.
*/
template <class Hasher, class Progress>
verify_info
verify_fast (
    path_type const& dat_path,
    path_type const& key_path,
    std::size_t buffer_size,
    Progress&& progress)
{
    using namespace detail;
    using File = native_file;
    File df;
    File kf;
    if (! df.open (file_mode::scan, dat_path))
        throw store_corrupt_error(
            "no data file");
    if (! kf.open (file_mode::read, key_path))
        throw store_corrupt_error(
            "no key file");
    key_file_header kh;
    dat_file_header dh;
    read (df, dh);
    read (kf, kh);
    verify(dh);
    verify<Hasher>(dh, kh);

    verify_info info;
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
    info.bucket_size = kh.bucket_size;
    info.key_file_size = kf.actual_size();
    info.dat_file_size = df.actual_size();

    std::size_t fetches = 0;

    // Counts unverified keys per bucket
    std::unique_ptr<std::uint32_t[]> nkeys(
        new std::uint32_t[kh.buckets]);

    // Verify contiguous sequential sections of the
    // key file using multiple passes over the data.
    //
    auto const buckets = std::max<std::size_t>(1,
        buffer_size / kh.block_size);
    buffer buf((buckets + 1) * kh.block_size);
    bucket tmp(kh.block_size, buf.get() +
        buckets * kh.block_size);
    std::size_t const passes =
        (kh.buckets + buckets - 1) / buckets;
    auto const df_size = df.actual_size();
    std::size_t const work = passes * df_size;
    std::size_t npass = 0;
    for (std::size_t b0 = 0; b0 < kh.buckets;
        b0 += buckets)
    {
        auto const b1 = std::min(
            b0 + buckets, kh.buckets);
        // Buffered range is [b0, b1)
        auto const bn = b1 - b0;
        kf.read((b0 + 1) * kh.block_size,
            buf.get(), bn * kh.block_size);
        // Count keys in buckets
        for (std::size_t i = b0 ; i < b1; ++i)
        {
            bucket b(kh.block_size, buf.get() +
                (i - b0) * kh.block_size);
            nkeys[i] = b.size();
            std::size_t nspill = 0;
            auto spill = b.spill();
            while (spill != 0)
            {
                tmp.read(df, spill);
                nkeys[i] += tmp.size();
                spill = tmp.spill();
                ++nspill;
                ++info.spill_count;
                info.spill_bytes +=
                    field<uint48_t>::size + // Zero
                    field<uint16_t>::size + // Size
                    tmp.compact_size();     // SpillBucket
            }
            if (nspill >= info.hist.size())
                nspill = info.hist.size() - 1;
            ++info.hist[nspill];
            info.key_count += nkeys[i];
        }
        // Iterate Data File
        bulk_reader<File> r(df,
            dat_file_header::size, df_size,
                64 * 1024 * 1024);
        while (! r.eof())
        {
            auto const offset = r.offset();
            progress(npass * df_size + offset, work);
            // Data Record or Spill Record
            auto is = r.prepare(
                field<uint48_t>::size); // Size
            std::size_t size;
            read<uint48_t>(is, size);
            if (size > 0)
            {
                // Data Record
                is = r.prepare(
                    kh.key_size +           // Key
                    size);                  // Data
                std::uint8_t const* const key =
                    is.data(kh.key_size);
                std::uint8_t const* const data =
                    is.data(size);
                (void)data;
                auto const h = hash<Hasher>(
                    key, kh.key_size, kh.salt);
                auto const n = bucket_index(
                    h, kh.buckets, kh.modulus);
                if (n < b0 || n >= b1)
                    continue;
                // Check bucket and spills
                bucket b (kh.block_size, buf.get() +
                    (n - b0) * kh.block_size);
                ++fetches;
                for (;;)
                {
                    for (auto i = b.lower_bound(h);
                        i < b.size(); ++i)
                    {
                        auto const item = b[i];
                        if (item.hash != h)
                            break;
                        if (item.offset == offset)
                            goto found;
                        ++fetches;
                    }
                    auto const spill = b.spill();
                    if (! spill)
                        throw store_corrupt_error(
                            "orphaned value");
                    b = tmp;
                    try
                    {
                        b.read (df, spill);
                        ++fetches;
                    }
                    catch (file_short_read_error const&)
                    {
                        throw store_corrupt_error(
                            "short spill");
                    }
                }
            found:
                // Update
                ++info.value_count;
                info.value_bytes += size;
                if (nkeys[n]-- == 0)
                    throw store_corrupt_error(
                        "duplicate value");
            }
            else
            {
                // Spill Record
                is = r.prepare(
                    field<std::uint16_t>::size);
                read<std::uint16_t>(is, size);      // Size
                if (size != kh.bucket_size)
                    throw store_corrupt_error(
                        "bad spill size");
                tmp.read(r);                        // Bucket
                if (b0 == 0)
                {
                    ++info.spill_count_tot;
                    info.spill_bytes_tot +=
                        field<uint48_t>::size +     // Zero
                        field<uint16_t>::size +     // Size
                        tmp.compact_size();         // Bucket
                }
            }
        }
        ++npass;
    }

    // Make sure every key in every bucket was visited
    for (std::size_t i = 0;
            i < kh.buckets; ++i)
        if (nkeys[i] != 0)
            throw store_corrupt_error(
                "orphan value");

    float sum = 0;
    for (int i = 0; i < info.hist.size(); ++i)
        sum += info.hist[i] * (i + 1);
    //info.avg_fetch = sum / info.buckets;
    info.avg_fetch = float(fetches) / info.value_count;
    info.waste = (info.spill_bytes_tot - info.spill_bytes) /
        float(info.dat_file_size);
    info.overhead =
        float(info.key_file_size + info.dat_file_size) /
        (
            info.value_bytes +
            info.key_count *
                (info.key_size +
                // Data Record
                 field<uint48_t>::size) // Size
                    ) - 1;
    info.actual_load = info.key_count / float(
        info.capacity * info.buckets);
    return info;
}

} // nudb
} // beast

#endif
