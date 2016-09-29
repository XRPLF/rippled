//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_FORMAT_HPP
#define NUDB_DETAIL_FORMAT_HPP

#include <nudb/error.hpp>
#include <nudb/type_traits.hpp>
#include <nudb/detail/buffer.hpp>
#include <nudb/detail/endian.hpp>
#include <nudb/detail/field.hpp>
#include <nudb/detail/stream.hpp>
#include <boost/assert.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace nudb {
namespace detail {

// Format of the nudb files:

/*

Integer sizes

block_size          less than 32 bits (maybe restrict it to 16 bits)
buckets             more than 32 bits
capacity            (same as bucket index)
file offsets        63 bits
hash                up to 64 bits (48 currently)
item index          less than 32 bits (index of item in bucket)
modulus             (same as buckets)
value size          up to 32 bits (or 32-bit builds can't read it)

*/

static std::size_t constexpr currentVersion = 2;

struct dat_file_header
{
    static std::size_t constexpr size =
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
    nsize_t key_size;
};

struct key_file_header
{
    static std::size_t constexpr size =
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
    nsize_t key_size;

    std::uint64_t salt;
    std::uint64_t pepper;
    nsize_t block_size;
    std::size_t load_factor;

    // Computed values
    nkey_t capacity;            // Entries per bucket
    nbuck_t buckets;            // Number of buckets
    nbuck_t modulus;            // pow(2,ceil(log2(buckets)))
};

struct log_file_header
{
    static std::size_t constexpr size =
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
    nsize_t key_size;
    std::uint64_t salt;
    std::uint64_t pepper;
    nsize_t block_size;
    noff_t key_file_size;
    noff_t dat_file_size;
};

// Type used to store hashes in buckets.
// This can be smaller than the output
// of the hash function.
//
using f_hash = uint48_t;

static_assert(field<f_hash>::size <=
    sizeof(nhash_t), "");

template<class T>
nhash_t
make_hash(nhash_t h);

template<>
inline
nhash_t
make_hash<uint48_t>(nhash_t h)
{
    return(h>>16)&0xffffffffffff;
}

// Returns the hash of a key given the salt.
// Note: The hash is expressed in f_hash units
//
template<class Hasher>
inline
nhash_t
hash(void const* key, nsize_t key_size, std::uint64_t salt)
{
    Hasher h{salt};
    return make_hash<f_hash>(h(key, key_size));
}

template<class Hasher>
inline
nhash_t
hash(void const* key, nsize_t key_size, Hasher const& h)
{
    return make_hash<f_hash>(h(key, key_size));
}

// Computes pepper from salt
//
template<class Hasher>
std::uint64_t
pepper(std::uint64_t salt)
{
    auto const v = to_little_endian(salt);
    Hasher h{salt};
    return h(&v, sizeof(v));
}

// Returns the actual size of a bucket.
// This can be smaller than the block size.
//
template<class = void>
nsize_t
bucket_size(nkey_t capacity)
{
    // Bucket Record
    return
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size +         // Spill
        capacity * (
            field<uint48_t>::size +     // Offset
            field<uint48_t>::size +     // Size
            field<f_hash>::size);       // Hash
}

// Returns the number of entries that fit in a bucket
//
template<class = void>
nkey_t
bucket_capacity(nsize_t block_size)
{
    // Bucket Record
    auto const size =
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size;          // Spill
    auto const entry_size =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        field<f_hash>::size;            // Hash
    if(block_size < key_file_header::size ||
            block_size < size)
        return 0;
    auto const n =
        (block_size - size) / entry_size;
    BOOST_ASSERT(n <= std::numeric_limits<nkey_t>::max());
    return static_cast<nkey_t>(std::min<std::size_t>(
        std::numeric_limits<nkey_t>::max(), n));
}

// Returns the number of bytes occupied by a value record
// VFALCO TODO Fix this
inline
std::size_t
value_size(std::size_t size,
    std::size_t key_size)
{
    // Data Record
    return
        field<uint48_t>::size + // Size
        key_size +              // Key
        size;                   // Data
}

// Returns the closest power of 2 not less than x
template<class T>
T
ceil_pow2(T x)
{
    static const unsigned long long t[6] = {
        0xFFFFFFFF00000000ull,
        0x00000000FFFF0000ull,
        0x000000000000FF00ull,
        0x00000000000000F0ull,
        0x000000000000000Cull,
        0x0000000000000002ull
    };

    int y =(((x &(x - 1)) == 0) ? 0 : 1);
    int j = 32;
    int i;

    for(i = 0; i < 6; i++) {
        int k =(((x & t[i]) == 0) ? 0 : j);
        y += k;
        x >>= k;
        j >>= 1;
    }

    return T{1}<<y;
}

//------------------------------------------------------------------------------

// Read data file header from stream
template<class = void>
void
read(istream& is, dat_file_header& dh)
{
    read(is, dh.type, sizeof(dh.type));
    read<std::uint16_t>(is, dh.version);
    read<std::uint64_t>(is, dh.uid);
    read<std::uint64_t>(is, dh.appnum);
    read<std::uint16_t>(is, dh.key_size);
    std::array<std::uint8_t, 64> reserved;
    read(is, reserved.data(), reserved.size());
}

// Read data file header from file
template<class File>
void
read(File& f, dat_file_header& dh, error_code& ec)
{
    std::array<std::uint8_t, dat_file_header::size> buf;
    f.read(0, buf.data(), buf.size(), ec);
    if(ec)
        return;
    istream is(buf);
    read(is, dh);
}

// Write data file header to stream
template<class = void>
void
write(ostream& os, dat_file_header const& dh)
{
    write(os, "nudb.dat", 8);
    write<std::uint16_t>(os, dh.version);
    write<std::uint64_t>(os, dh.uid);
    write<std::uint64_t>(os, dh.appnum);
    write<std::uint16_t>(os, dh.key_size);
    std::array<std::uint8_t, 64> reserved;
    reserved.fill(0);
    write(os, reserved.data(), reserved.size());
}

// Write data file header to file
template<class File>
void
write(File& f, dat_file_header const& dh, error_code& ec)
{
    std::array<std::uint8_t, dat_file_header::size> buf;
    ostream os(buf);
    write(os, dh);
    f.write(0, buf.data(), buf.size(), ec);
}

// Read key file header from stream
template<class = void>
void
read(istream& is, noff_t file_size, key_file_header& kh)
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
    std::array<std::uint8_t, 56> reserved;
    read(is, reserved.data(), reserved.size());

    // VFALCO These need to be checked to handle
    //        when the file size is too small
    kh.capacity = bucket_capacity(kh.block_size);
    if(file_size > kh.block_size)
    {
        if(kh.block_size > 0)
            kh.buckets = static_cast<nbuck_t>(
                (file_size - kh.block_size) / kh.block_size);
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
template<class File>
void
read(File& f, key_file_header& kh, error_code& ec)
{
    std::array<std::uint8_t, key_file_header::size> buf;
    f.read(0, buf.data(), buf.size(), ec);
    if(ec)
        return;
    istream is{buf};
    auto const size = f.size(ec);
    if(ec)
        return;
    read(is, size, kh);
}

// Write key file header to stream
template<class = void>
void
write(ostream& os, key_file_header const& kh)
{
    write(os, "nudb.key", 8);
    write<std::uint16_t>(os, kh.version);
    write<std::uint64_t>(os, kh.uid);
    write<std::uint64_t>(os, kh.appnum);
    write<std::uint16_t>(os, kh.key_size);
    write<std::uint64_t>(os, kh.salt);
    write<std::uint64_t>(os, kh.pepper);
    write<std::uint16_t>(os, kh.block_size);
    write<std::uint16_t>(os, kh.load_factor);
    std::array<std::uint8_t, 56> reserved;
    reserved.fill(0);
    write(os, reserved.data(), reserved.size());
}

// Write key file header to file
template<class File>
void
write(File& f, key_file_header const& kh, error_code& ec)
{
    buffer buf;
    buf.reserve(kh.block_size);
    if(kh.block_size < key_file_header::size)
    {
        ec = error::invalid_block_size;
        return;
    }
    std::fill(buf.get(), buf.get() + buf.size(), 0);
    ostream os{buf.get(), buf.size()};
    write(os, kh);
    f.write(0, buf.get(), buf.size(), ec);
}

// Read log file header from stream
template<class = void>
void
read(istream& is, log_file_header& lh)
{
    read(is, lh.type, sizeof(lh.type));
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
template<class File>
void
read(File& f, log_file_header& lh, error_code& ec)
{
    std::array<std::uint8_t, log_file_header::size> buf;
    f.read(0, buf.data(), buf.size(), ec);
    if(ec)
        return;
    istream is{buf};
    read(is, lh);
}

// Write log file header to stream
template<class = void>
void
write(ostream& os, log_file_header const& lh)
{
    write(os, "nudb.log", 8);
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
template<class File>
void
write(File& f, log_file_header const& lh, error_code& ec)
{
    std::array<std::uint8_t, log_file_header::size> buf;
    ostream os{buf};
    write(os, lh);
    f.write(0, buf.data(), buf.size(), ec);
}

// Verify contents of data file header
template<class = void>
void
verify(dat_file_header const& dh, error_code& ec)
{
    std::string const type{dh.type, 8};
    if(type != "nudb.dat")
    {
        ec = error::not_data_file;
        return;
    }
    if(dh.version != currentVersion)
    {
        ec = error::different_version;
        return;
    }
    if(dh.key_size < 1)
    {
        ec = error::invalid_key_size;
        return;
    }
}

// Verify contents of key file header
template<class Hasher>
void
verify(key_file_header const& kh, error_code& ec)
{
    std::string const type{kh.type, 8};
    if(type != "nudb.key")
    {
        ec = error::not_key_file;
        return;
    }
    if(kh.version != currentVersion)
    {
        ec = error::different_version;
        return;
    }
    if(kh.key_size < 1)
    {
        ec = error::invalid_key_size;
        return;
    }
    if(kh.pepper != pepper<Hasher>(kh.salt))
    {
        ec = error::hash_mismatch;
        return;
    }
    if(kh.load_factor < 1)
    {
        ec = error::invalid_load_factor;
        return;
    }
    if(kh.capacity < 1)
    {
        ec = error::invalid_capacity;
        return;
    }
    if(kh.buckets < 1)
    {
        ec = error::invalid_bucket_count;
        return;
    }
}

// Verify contents of log file header
template<class Hasher>
void
verify(log_file_header const& lh, error_code& ec)
{
    std::string const type{lh.type, 8};
    if(type != "nudb.log")
    {
        ec = error::not_log_file;
        return;
    }
    if(lh.version != currentVersion)
    {
        ec = error::different_version;
        return;
    }
    if(lh.pepper != pepper<Hasher>(lh.salt))
    {
        ec = error::hash_mismatch;
        return;
    }
    if(lh.key_size < 1)
    {
        ec = error::invalid_key_size;
        return;
    }
}

// Make sure key file and value file headers match
template<class Hasher>
void
verify(dat_file_header const& dh,
    key_file_header const& kh, error_code& ec)
{
    verify<Hasher>(kh, ec);
    if(ec)
        return;
    if(kh.uid != dh.uid)
    {
        ec = error::uid_mismatch;
        return;
    }
    if(kh.appnum != dh.appnum)
    {
        ec = error::appnum_mismatch;
        return;
    }
    if(kh.key_size != dh.key_size)
    {
        ec = error::key_size_mismatch;
        return;
    }
}

// Make sure key file and log file headers match
template<class Hasher>
void
verify(key_file_header const& kh,
    log_file_header const& lh, error_code& ec)
{
    verify<Hasher>(lh, ec);
    if(ec)
        return;
    if(kh.uid != lh.uid)
    {
        ec = error::uid_mismatch;
        return;
    }
    if(kh.appnum != lh.appnum)
    {
        ec = error::appnum_mismatch;
        return;
    }
    if(kh.key_size != lh.key_size)
    {
        ec = error::key_size_mismatch;
        return;
    }
    if(kh.salt != lh.salt)
    {
        ec = error::salt_mismatch;
        return;
    }
    if(kh.pepper != lh.pepper)
    {
        ec = error::pepper_mismatch;
        return;
    }
    if(kh.block_size != lh.block_size)
    {
        ec = error::block_size_mismatch;
        return;
    }
}

} // detail
} // nudb

#endif
