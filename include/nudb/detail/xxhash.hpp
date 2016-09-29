//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//
// This is a derivative work based on xxHash 0.6.2, copyright below:
/*
   xxHash - Extremely Fast Hash algorithm
   Header File
   Copyright (C) 2012-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - xxHash source repository : https://github.com/Cyan4973/xxHash
*/

#ifndef NUDB_DETAIL_XXHASH_HPP
#define NUDB_DETAIL_XXHASH_HPP

#include <nudb/detail/endian.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace nudb {
namespace detail {

#define NUDB_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

// minGW _rotl gives poor performance
#if defined(_MSC_VER)
# define NUDB_XXH_rotl64(x,r) _rotl64(x,r)
#else
# define NUDB_XXH_rotl64(x,r) ((x << r) | (x >> (64 - r)))
#endif

#if defined(_MSC_VER)
# define NUDB_XXH_swap32 _byteswap_ulong
#elif NUDB_GCC_VERSION >= 403
# define NUDB_XXH_swap32 __builtin_bswap32
#endif

#if defined(_MSC_VER)
# define NUDB_XXH_swap64 _byteswap_uint64
#elif NUDB_GCC_VERSION >= 403
# define NUDB_XXH_swap64 __builtin_bswap64
#endif

#ifndef NUDB_XXH_swap32
inline
std::uint32_t
NUDB_XXH_swap32(std::uint32_t x)
{
    return  ((x << 24) & 0xff000000 ) |
            ((x <<  8) & 0x00ff0000 ) |
            ((x >>  8) & 0x0000ff00 ) |
            ((x >> 24) & 0x000000ff );
}
#endif

#ifndef NUDB_XXH_swap64
inline
std::uint64_t
NUDB_XXH_swap64(std::uint64_t x)
{
    return  ((x << 56) & 0xff00000000000000ULL) |
            ((x << 40) & 0x00ff000000000000ULL) |
            ((x << 24) & 0x0000ff0000000000ULL) |
            ((x << 8)  & 0x000000ff00000000ULL) |
            ((x >> 8)  & 0x00000000ff000000ULL) |
            ((x >> 24) & 0x0000000000ff0000ULL) |
            ((x >> 40) & 0x000000000000ff00ULL) |
            ((x >> 56) & 0x00000000000000ffULL);
}
#endif

static std::uint64_t constexpr prime64_1 = 11400714785074694791ULL;
static std::uint64_t constexpr prime64_2 = 14029467366897019727ULL;
static std::uint64_t constexpr prime64_3 =  1609587929392839161ULL;
static std::uint64_t constexpr prime64_4 =  9650029242287828579ULL;
static std::uint64_t constexpr prime64_5 =  2870177450012600261ULL;

// Portable and safe solution. Generally efficient.
// see : http://stackoverflow.com/a/32095106/646947

inline
std::uint32_t
XXH_read32(void const* p)
{
    std::uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

inline
std::uint64_t
XXH_read64(void const* p)
{
    std::uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

// little endian, aligned
inline
std::uint32_t
XXH_readLE32_align(void const* p, std::true_type, std::true_type)
{
    return *reinterpret_cast<std::uint32_t const*>(p);
}

// little endian, unaligned
inline
std::uint32_t
XXH_readLE32_align(void const* p, std::true_type, std::false_type)
{
    return XXH_read32(p);
}

// big endian, aligned
inline
std::uint32_t
XXH_readLE32_align(void const* p, std::false_type, std::true_type)
{
    return NUDB_XXH_swap32(
        *reinterpret_cast<std::uint32_t const*>(p));
}

// big endian, unaligned
inline
std::uint32_t
XXH_readLE32_align(void const* p, std::false_type, std::false_type)
{
    return NUDB_XXH_swap32(XXH_read32(p));
}

// little endian, aligned
inline
std::uint64_t
XXH_readLE64_align(void const* p, std::true_type, std::true_type)
{
    return *reinterpret_cast<std::uint64_t const*>(p);
}

// little endian, unaligned
inline
std::uint64_t
XXH_readLE64_align(void const* p, std::true_type, std::false_type)
{
    return XXH_read64(p);
}

// big endian, aligned
inline
std::uint64_t
XXH_readLE64_align(void const* p, std::false_type, std::true_type)
{
    return NUDB_XXH_swap64(
        *reinterpret_cast<std::uint64_t const*>(p));
}

// big endian, unaligned
inline
std::uint64_t
XXH_readLE64_align(void const* p, std::false_type, std::false_type)
{
    return NUDB_XXH_swap64(XXH_read64(p));
}

inline
std::uint64_t
XXH64_round(std::uint64_t acc, std::uint64_t input)
{
    acc += input * prime64_2;
    acc  = NUDB_XXH_rotl64(acc, 31);
    acc *= prime64_1;
    return acc;
}

inline
std::uint64_t
XXH64_mergeRound(std::uint64_t acc, std::uint64_t val)
{
    val  = XXH64_round(0, val);
    acc ^= val;
    acc  = acc * prime64_1 + prime64_4;
    return acc;
}

template<bool LittleEndian, bool Aligned>
std::uint64_t
XXH64_endian_align(
    void const* input, std::size_t len, std::uint64_t seed,
        std::integral_constant<bool, LittleEndian> endian,
            std::integral_constant<bool, Aligned> align)
{
    const std::uint8_t* p = (const std::uint8_t*)input;
    const std::uint8_t* const bEnd = p + len;
    std::uint64_t h64;
    auto const XXH_get32bits =
        [](void const* p)
        {
            return XXH_readLE32_align(p,
                decltype(endian){}, decltype(align){});
        };
    auto const XXH_get64bits =
        [](void const* p)
        {
            return XXH_readLE64_align(p,
                decltype(endian){}, decltype(align){});
        };
    if(len>=32)
    {
        const std::uint8_t* const limit = bEnd - 32;
        std::uint64_t v1 = seed + prime64_1 + prime64_2;
        std::uint64_t v2 = seed + prime64_2;
        std::uint64_t v3 = seed + 0;
        std::uint64_t v4 = seed - prime64_1;

        do
        {
            v1 = XXH64_round(v1, XXH_get64bits(p)); p+=8;
            v2 = XXH64_round(v2, XXH_get64bits(p)); p+=8;
            v3 = XXH64_round(v3, XXH_get64bits(p)); p+=8;
            v4 = XXH64_round(v4, XXH_get64bits(p)); p+=8;
        }
        while(p<=limit);

        h64 = NUDB_XXH_rotl64(v1, 1) +
              NUDB_XXH_rotl64(v2, 7) +
              NUDB_XXH_rotl64(v3, 12) +
              NUDB_XXH_rotl64(v4, 18);
        h64 = XXH64_mergeRound(h64, v1);
        h64 = XXH64_mergeRound(h64, v2);
        h64 = XXH64_mergeRound(h64, v3);
        h64 = XXH64_mergeRound(h64, v4);
    }
    else
    {
        h64  = seed + prime64_5;
    }
    h64 += len;
    while(p + 8 <= bEnd)
    {
        std::uint64_t const k1 = XXH64_round(0, XXH_get64bits(p));
        h64 ^= k1;
        h64  = NUDB_XXH_rotl64(h64,27) * prime64_1 + prime64_4;
        p+=8;
    }
    if(p+4<=bEnd)
    {
        h64 ^= (std::uint64_t)(XXH_get32bits(p)) * prime64_1;
        h64 = NUDB_XXH_rotl64(h64, 23) * prime64_2 + prime64_3;
        p+=4;
    }
    while(p<bEnd)
    {
        h64 ^= (*p) * prime64_5;
        h64 = NUDB_XXH_rotl64(h64, 11) * prime64_1;
        p++;
    }
    h64 ^= h64 >> 33;
    h64 *= prime64_2;
    h64 ^= h64 >> 29;
    h64 *= prime64_3;
    h64 ^= h64 >> 32;
    return h64;
}

/*  Calculate the 64-bit hash of a block of memory.

    @param data A pointer to the buffer to compute the hash on.
    The buffer may be unaligned.

    @note This function runs faster on 64-bits systems, but slower
    on 32-bits systems (see benchmark).

    @param bytes The size of the buffer in bytes.

    @param seed A value which may be used to permute the output.
    Using a different seed with the same input will produce a
    different value.

    @return The 64-bit hash of the input data.
*/
template<class = void>
std::uint64_t
XXH64(void const* data, size_t bytes, std::uint64_t seed)
{
    // Use faster algorithm if aligned
    if((reinterpret_cast<std::uintptr_t>(data) & 7) == 0)
        return XXH64_endian_align(data, bytes, seed,
            is_little_endian{}, std::false_type{});
    return XXH64_endian_align(data, bytes, seed,
        is_little_endian{}, std::true_type{});
}

} // detail
} // nudb

#endif

