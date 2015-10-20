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

#ifndef BEAST_NUDB_DETAIL_FORMAT_H_INCLUDED
#define BEAST_NUDB_DETAIL_FORMAT_H_INCLUDED

#include <beast/nudb/common.h>
#include <beast/nudb/detail/buffer.h>
#include <beast/nudb/detail/field.h>
#include <beast/nudb/detail/stream.h>
#include <beast/config/CompilerConfig.h> // for BEAST_CONSTEXPR
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace beast {
namespace nudb {
namespace detail {

// Format of the nudb files:

static std::size_t BEAST_CONSTEXPR currentVersion = 2;

struct dat_file_header
{
    static std::size_t BEAST_CONSTEXPR size =
        8 +     // Type
        2 +     // Version
        8 +     // UID
        8 +     // Appnum
        2 +     // KeySize

        64;     // (Reserved)

    char type[8];
    std::size_t version;
    std::uint64_t uid;
    std::uint64_t appnum;
    std::size_t key_size;
};

struct key_file_header
{
    static std::size_t BEAST_CONSTEXPR size =
        8 +     // Type
        2 +     // Version
        8 +     // UID
        8 +     // Appnum
        2 +     // KeySize

        8 +     // Salt
        8 +     // Pepper
        2 +     // BlockSize
        2 +     // LoadFactor

        56;     // (Reserved)

    char type[8];
    std::size_t version;
    std::uint64_t uid;
    std::uint64_t appnum;
    std::size_t key_size;

    std::uint64_t salt;
    std::uint64_t pepper;
    std::size_t block_size;
    std::size_t load_factor;

    // Computed values
    std::size_t capacity;
    std::size_t bucket_size;
    std::size_t buckets;
    std::size_t modulus;
};

struct log_file_header
{
    static std::size_t BEAST_CONSTEXPR size =
        8 +     // Type
        2 +     // Version
        8 +     // UID
        8 +     // Appnum
        2 +     // KeySize

        8 +     // Salt
        8 +     // Pepper
        2 +     // BlockSize

        8 +     // KeyFileSize
        8;      // DataFileSize

    char type[8];
    std::size_t version;
    std::uint64_t uid;
    std::uint64_t appnum;
    std::size_t key_size;
    std::uint64_t salt;
    std::uint64_t pepper;
    std::size_t block_size;
    std::size_t key_file_size;
    std::size_t dat_file_size;
};

// Type used to store hashes in buckets.
// This can be smaller than the output
// of the hash function.
//
using hash_t = uint48_t;

static_assert(field<hash_t>::size <=
    sizeof(std::size_t), "");

template <class T>
std::size_t
make_hash (std::size_t h);

template<>
inline
std::size_t
make_hash<uint48_t>(std::size_t h)
{
    return (h>>16)&0xffffffffffff;
}

// Returns the hash of a key given the salt.
// Note: The hash is expressed in hash_t units
//
template <class Hasher>
inline
std::size_t
hash (void const* key,
    std::size_t key_size, std::size_t salt)
{
    Hasher h (salt);
    h (key, key_size);
    return make_hash<hash_t>(static_cast<
        typename Hasher::result_type>(h));
}

// Computes pepper from salt
//
template <class Hasher>
std::size_t
pepper (std::size_t salt)
{
    Hasher h (salt);
    h (&salt, sizeof(salt));
    return static_cast<std::size_t>(h);
}

// Returns the actual size of a bucket.
// This can be smaller than the block size.
//
template <class = void>
std::size_t
bucket_size (std::size_t capacity)
{
    // Bucket Record
    return
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size +         // Spill
        capacity * (
            field<uint48_t>::size +     // Offset
            field<uint48_t>::size +     // Size
            field<hash_t>::size);       // Hash
}

// Returns the number of entries that fit in a bucket
//
template <class = void>
std::size_t
bucket_capacity (std::size_t block_size)
{
    // Bucket Record
    auto const size =
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size;          // Spill
    auto const entry_size =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        field<hash_t>::size;            // Hash
    if (block_size < key_file_header::size ||
        block_size < size)
        return 0;
    return (block_size - size) / entry_size;
}

// Returns the number of bytes occupied by a value record
inline
std::size_t
value_size (std::size_t size,
    std::size_t key_size)
{
    // Data Record
    return
        field<uint48_t>::size + // Size
        key_size +              // Key
        size;                   // Data
}

// Returns the closest power of 2 not less than x
template <class = void>
std::size_t
ceil_pow2 (unsigned long long x)
{
    static const unsigned long long t[6] = {
        0xFFFFFFFF00000000ull,
        0x00000000FFFF0000ull,
        0x000000000000FF00ull,
        0x00000000000000F0ull,
        0x000000000000000Cull,
        0x0000000000000002ull
    };

    int y = (((x & (x - 1)) == 0) ? 0 : 1);
    int j = 32;
    int i;

    for(i = 0; i < 6; i++) {
        int k = (((x & t[i]) == 0) ? 0 : j);
        y += k;
        x >>= k;
        j >>= 1;
    }

    return std::size_t(1)<<y;
}

//------------------------------------------------------------------------------

// Read data file header from stream
template <class = void>
void
read (istream& is, dat_file_header& dh)
{
    read (is, dh.type, sizeof(dh.type));
    read<std::uint16_t>(is, dh.version);
    read<std::uint64_t>(is, dh.uid);
    read<std::uint64_t>(is, dh.appnum);
    read<std::uint16_t>(is, dh.key_size);
    std::array <std::uint8_t, 64> reserved;
    read (is,
        reserved.data(), reserved.size());
}

// Read data file header from file
template <class File>
void
read (File& f, dat_file_header& dh)
{
    std::array<std::uint8_t,
        dat_file_header::size> buf;
    try
    {
        f.read(0, buf.data(), buf.size());
    }
    catch (file_short_read_error const&)
    {
        throw store_corrupt_error(
            "short data file header");
    }
    istream is(buf);
    read (is, dh);
}

// Write data file header to stream
template <class = void>
void
write (ostream& os, dat_file_header const& dh)
{
    write (os, "nudb.dat", 8);
    write<std::uint16_t>(os, dh.version);
    write<std::uint64_t>(os, dh.uid);
    write<std::uint64_t>(os, dh.appnum);
    write<std::uint16_t>(os, dh.key_size);
    std::array <std::uint8_t, 64> reserved;
    reserved.fill(0);
    write (os,
        reserved.data(), reserved.size());
}

// Write data file header to file
template <class File>
void
write (File& f, dat_file_header const& dh)
{
    std::array <std::uint8_t,
        dat_file_header::size> buf;
    ostream os(buf);
    write(os, dh);
    f.write (0, buf.data(), buf.size());
}

// Read key file header from stream
template <class = void>
void
read (istream& is, std::size_t file_size,
    key_file_header& kh)
{
    read(is, kh.type, sizeof(kh.type));
    read<std::uint16_t>(is, kh.version);
    read<std::uint64_t>(is, kh.uid);
    read<std::uint64_t>(is, kh.appnum);
    read<std::uint16_t>(is, kh.key_size);
    read<std::uint64_t>(is, kh.salt);
    read<std::uint64_t>(is, kh.pepper);
    read<std::uint16_t>(is, kh.block_size);
    read<std::uint16_t>(is, kh.load_factor);
    std::array <std::uint8_t, 56> reserved;
    read (is,
        reserved.data(), reserved.size());

    // VFALCO These need to be checked to handle
    //        when the file size is too small
    kh.capacity = bucket_capacity(kh.block_size);
    kh.bucket_size = bucket_size(kh.capacity);
    if (file_size > kh.block_size)
    {
        // VFALCO This should be handled elsewhere.
        //        we shouldn't put the computed fields
        //        in this header.
        if (kh.block_size > 0)
            kh.buckets = (file_size - kh.bucket_size)
                / kh.block_size;
        else
            // VFALCO Corruption or logic error
            kh.buckets = 0;
    }
    else
    {
        kh.buckets = 0;
    }
    kh.modulus = ceil_pow2(kh.buckets);
}

// Read key file header from file
template <class File>
void
read (File& f, key_file_header& kh)
{
    std::array <std::uint8_t,
        key_file_header::size> buf;
    try
    {
        f.read(0, buf.data(), buf.size());
    }
    catch (file_short_read_error const&)
    {
        throw store_corrupt_error(
            "short key file header");
    }
    istream is(buf);
    read (is, f.actual_size(), kh);
}

// Write key file header to stream
template <class = void>
void
write (ostream& os, key_file_header const& kh)
{
    write (os, "nudb.key", 8);
    write<std::uint16_t>(os, kh.version);
    write<std::uint64_t>(os, kh.uid);
    write<std::uint64_t>(os, kh.appnum);
    write<std::uint16_t>(os, kh.key_size);
    write<std::uint64_t>(os, kh.salt);
    write<std::uint64_t>(os, kh.pepper);
    write<std::uint16_t>(os, kh.block_size);
    write<std::uint16_t>(os, kh.load_factor);
    std::array <std::uint8_t, 56> reserved;
    reserved.fill (0);
    write (os,
        reserved.data(), reserved.size());
}

// Write key file header to file
template <class File>
void
write (File& f, key_file_header const& kh)
{
    buffer buf;
    buf.reserve (kh.block_size);
    if (kh.block_size < key_file_header::size)
        throw std::logic_error(
            "nudb: block size too small");
    std::fill(buf.get(), buf.get() + buf.size(), 0);
    ostream os (buf.get(), buf.size());
    write (os, kh);
    f.write (0, buf.get(), buf.size());
}

// Read log file header from stream
template <class = void>
void
read (istream& is, log_file_header& lh)
{
    read (is, lh.type, sizeof(lh.type));
    read<std::uint16_t>(is, lh.version);
    read<std::uint64_t>(is, lh.uid);
    read<std::uint64_t>(is, lh.appnum);
    read<std::uint16_t>(is, lh.key_size);
    read<std::uint64_t>(is, lh.salt);
    read<std::uint64_t>(is, lh.pepper);
    read<std::uint16_t>(is, lh.block_size);
    read<std::uint64_t>(is, lh.key_file_size);
    read<std::uint64_t>(is, lh.dat_file_size);
}

// Read log file header from file
template <class File>
void
read (File& f, log_file_header& lh)
{
    std::array <std::uint8_t,
        log_file_header::size> buf;
    // Can throw file_short_read_error to callers
    f.read (0, buf.data(), buf.size());
    istream is(buf);
    read (is, lh);
}

// Write log file header to stream
template <class = void>
void
write (ostream& os, log_file_header const& lh)
{
    write (os, "nudb.log", 8);
    write<std::uint16_t>(os, lh.version);
    write<std::uint64_t>(os, lh.uid);
    write<std::uint64_t>(os, lh.appnum);
    write<std::uint16_t>(os, lh.key_size);
    write<std::uint64_t>(os, lh.salt);
    write<std::uint64_t>(os, lh.pepper);
    write<std::uint16_t>(os, lh.block_size);
    write<std::uint64_t>(os, lh.key_file_size);
    write<std::uint64_t>(os, lh.dat_file_size);
}

// Write log file header to file
template <class File>
void
write (File& f, log_file_header const& lh)
{
    std::array <std::uint8_t,
        log_file_header::size> buf;
    ostream os (buf);
    write (os, lh);
    f.write (0, buf.data(), buf.size());
}

template <class = void>
void
verify (dat_file_header const& dh)
{
    std::string const type (dh.type, 8);
    if (type != "nudb.dat")
        throw store_corrupt_error (
            "bad type in data file");
    if (dh.version != currentVersion)
        throw store_corrupt_error (
            "bad version in data file");
    if (dh.key_size < 1)
        throw store_corrupt_error (
            "bad key size in data file");
}

template <class Hasher>
void
verify (key_file_header const& kh)
{
    std::string const type (kh.type, 8);
    if (type != "nudb.key")
        throw store_corrupt_error (
            "bad type in key file");
    if (kh.version != currentVersion)
        throw store_corrupt_error (
            "bad version in key file");
    if (kh.key_size < 1)
        throw store_corrupt_error (
            "bad key size in key file");
    if (kh.pepper != pepper<Hasher>(kh.salt))
        throw store_corrupt_error(
            "wrong hash function for key file");
    if (kh.load_factor < 1)
        throw store_corrupt_error (
            "bad load factor in key file");
    if (kh.capacity < 1)
        throw store_corrupt_error (
            "bad capacity in key file");
    if (kh.buckets < 1)
        throw store_corrupt_error (
            "bad key file size");
}

template <class Hasher>
void
verify (log_file_header const& lh)
{
    std::string const type (lh.type, 8);
    if (type != "nudb.log")
        throw store_corrupt_error (
            "bad type in log file");
    if (lh.version != currentVersion)
        throw store_corrupt_error (
            "bad version in log file");
    if (lh.pepper != pepper<Hasher>(lh.salt))
        throw store_corrupt_error(
            "wrong hash function for log file");
    if (lh.key_size < 1)
        throw store_corrupt_error (
            "bad key size in log file");
}

// Make sure key file and value file headers match
template <class Hasher>
void
verify (dat_file_header const& dh,
    key_file_header const& kh)
{
    verify<Hasher> (kh);
    if (kh.uid != dh.uid)
        throw store_corrupt_error(
            "uid mismatch");
    if (kh.appnum != dh.appnum)
        throw store_corrupt_error(
            "appnum mismatch");
    if (kh.key_size != dh.key_size)
        throw store_corrupt_error(
            "key size mismatch");
}

template <class Hasher>
void
verify (key_file_header const& kh,
    log_file_header const& lh)
{
    verify<Hasher>(lh);
    if (kh.uid != lh.uid)
        throw store_corrupt_error (
            "uid mismatch in log file");
    if (kh.appnum != lh.appnum)
        throw store_corrupt_error(
            "appnum mismatch in log file");
    if (kh.key_size != lh.key_size)
        throw store_corrupt_error (
            "key size mismatch in log file");
    if (kh.salt != lh.salt)
        throw store_corrupt_error (
            "salt mismatch in log file");
    if (kh.pepper != lh.pepper)
        throw store_corrupt_error (
            "pepper mismatch in log file");
    if (kh.block_size != lh.block_size)
        throw store_corrupt_error (
            "block size mismatch in log file");
}

} // detail
} // nudb
} // beast

#endif
