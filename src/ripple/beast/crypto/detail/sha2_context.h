//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CRYPTO_SHA512_CONTEXT_H_INCLUDED
#define BEAST_CRYPTO_SHA512_CONTEXT_H_INCLUDED

#include <cstdint>
#include <cstring>

namespace beast {
namespace detail {

// Based on https://github.com/ogay/sha2
// This implementation has been modified from the
// original. It has been updated for C++11.

/*
 * Updated to C++, zedwood.com 2012
 * Based on Olivier Gay's version
 * See Modified BSD License below:
 *
 * FIPS 180-2 SHA-224/256/384/512 implementation
 * Issue date:  04/30/2005
 * http://www.ouah.org/ogay/sha2/
 *
 * Copyright (C) 2005, 2007 Olivier Gay <olivier.gay@a3.epfl.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

struct sha256_context
{
    explicit sha256_context() = default;

    static unsigned int const block_size = 64;
    static unsigned int const digest_size = 32;

    unsigned int tot_len;
    unsigned int len;
    unsigned char block[2 * block_size];
    std::uint32_t h[8];
};

struct sha512_context
{
    explicit sha512_context() = default;

    static unsigned int const block_size = 128;
    static unsigned int const digest_size = 64;

    unsigned int tot_len;
    unsigned int len;
    unsigned char block[2 * block_size];
    std::uint64_t h[8];
};

#define BEAST_SHA2_SHFR(x, n) (x >> n)
#define BEAST_SHA2_ROTR(x, n) ((x >> n) | (x << ((sizeof(x) << 3) - n)))
#define BEAST_SHA2_ROTL(x, n) ((x << n) | (x >> ((sizeof(x) << 3) - n)))
#define BEAST_SHA2_CH(x, y, z) ((x & y) ^ (~x & z))
#define BEAST_SHA2_MAJ(x, y, z) ((x & y) ^ (x & z) ^ (y & z))
#define BEAST_SHA256_F1(x) \
    (BEAST_SHA2_ROTR(x, 2) ^ BEAST_SHA2_ROTR(x, 13) ^ BEAST_SHA2_ROTR(x, 22))
#define BEAST_SHA256_F2(x) \
    (BEAST_SHA2_ROTR(x, 6) ^ BEAST_SHA2_ROTR(x, 11) ^ BEAST_SHA2_ROTR(x, 25))
#define BEAST_SHA256_F3(x) \
    (BEAST_SHA2_ROTR(x, 7) ^ BEAST_SHA2_ROTR(x, 18) ^ BEAST_SHA2_SHFR(x, 3))
#define BEAST_SHA256_F4(x) \
    (BEAST_SHA2_ROTR(x, 17) ^ BEAST_SHA2_ROTR(x, 19) ^ BEAST_SHA2_SHFR(x, 10))
#define BEAST_SHA512_F1(x) \
    (BEAST_SHA2_ROTR(x, 28) ^ BEAST_SHA2_ROTR(x, 34) ^ BEAST_SHA2_ROTR(x, 39))
#define BEAST_SHA512_F2(x) \
    (BEAST_SHA2_ROTR(x, 14) ^ BEAST_SHA2_ROTR(x, 18) ^ BEAST_SHA2_ROTR(x, 41))
#define BEAST_SHA512_F3(x) \
    (BEAST_SHA2_ROTR(x, 1) ^ BEAST_SHA2_ROTR(x, 8) ^ BEAST_SHA2_SHFR(x, 7))
#define BEAST_SHA512_F4(x) \
    (BEAST_SHA2_ROTR(x, 19) ^ BEAST_SHA2_ROTR(x, 61) ^ BEAST_SHA2_SHFR(x, 6))
#define BEAST_SHA2_PACK32(str, x)                   \
    {                                               \
        *(x) = ((std::uint32_t) * ((str) + 3)) |    \
            ((std::uint32_t) * ((str) + 2) << 8) |  \
            ((std::uint32_t) * ((str) + 1) << 16) | \
            ((std::uint32_t) * ((str) + 0) << 24);  \
    }
#define BEAST_SHA2_UNPACK32(x, str)               \
    {                                             \
        *((str) + 3) = (std::uint8_t)((x));       \
        *((str) + 2) = (std::uint8_t)((x) >> 8);  \
        *((str) + 1) = (std::uint8_t)((x) >> 16); \
        *((str) + 0) = (std::uint8_t)((x) >> 24); \
    }
#define BEAST_SHA2_PACK64(str, x)                   \
    {                                               \
        *(x) = ((std::uint64_t) * ((str) + 7)) |    \
            ((std::uint64_t) * ((str) + 6) << 8) |  \
            ((std::uint64_t) * ((str) + 5) << 16) | \
            ((std::uint64_t) * ((str) + 4) << 24) | \
            ((std::uint64_t) * ((str) + 3) << 32) | \
            ((std::uint64_t) * ((str) + 2) << 40) | \
            ((std::uint64_t) * ((str) + 1) << 48) | \
            ((std::uint64_t) * ((str) + 0) << 56);  \
    }
#define BEAST_SHA2_UNPACK64(x, str)               \
    {                                             \
        *((str) + 7) = (std::uint8_t)((x));       \
        *((str) + 6) = (std::uint8_t)((x) >> 8);  \
        *((str) + 5) = (std::uint8_t)((x) >> 16); \
        *((str) + 4) = (std::uint8_t)((x) >> 24); \
        *((str) + 3) = (std::uint8_t)((x) >> 32); \
        *((str) + 2) = (std::uint8_t)((x) >> 40); \
        *((str) + 1) = (std::uint8_t)((x) >> 48); \
        *((str) + 0) = (std::uint8_t)((x) >> 56); \
    }

//------------------------------------------------------------------------------

// SHA256

template <class = void>
void
sha256_transform(
    sha256_context& ctx,
    unsigned char const* message,
    unsigned int block_nb) noexcept
{
    static unsigned long long const K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t w[64];
    std::uint32_t wv[8];
    std::uint32_t t1, t2;
    unsigned char const* sub_block;
    int i, j;
    for (i = 0; i < (int)block_nb; i++)
    {
        sub_block = message + (i << 6);
        for (j = 0; j < 16; j++)
            BEAST_SHA2_PACK32(&sub_block[j << 2], &w[j]);
        for (j = 16; j < 64; j++)
            w[j] = BEAST_SHA256_F4(w[j - 2]) + w[j - 7] +
                BEAST_SHA256_F3(w[j - 15]) + w[j - 16];
        for (j = 0; j < 8; j++)
            wv[j] = ctx.h[j];
        for (j = 0; j < 64; j++)
        {
            t1 = wv[7] + BEAST_SHA256_F2(wv[4]) +
                BEAST_SHA2_CH(wv[4], wv[5], wv[6]) + K[j] + w[j];
            t2 = BEAST_SHA256_F1(wv[0]) + BEAST_SHA2_MAJ(wv[0], wv[1], wv[2]);
            wv[7] = wv[6];
            wv[6] = wv[5];
            wv[5] = wv[4];
            wv[4] = wv[3] + t1;
            wv[3] = wv[2];
            wv[2] = wv[1];
            wv[1] = wv[0];
            wv[0] = t1 + t2;
        }
        for (j = 0; j < 8; j++)
            ctx.h[j] += wv[j];
    }
}

template <class = void>
void
init(sha256_context& ctx) noexcept
{
    ctx.len = 0;
    ctx.tot_len = 0;
    ctx.h[0] = 0x6a09e667;
    ctx.h[1] = 0xbb67ae85;
    ctx.h[2] = 0x3c6ef372;
    ctx.h[3] = 0xa54ff53a;
    ctx.h[4] = 0x510e527f;
    ctx.h[5] = 0x9b05688c;
    ctx.h[6] = 0x1f83d9ab;
    ctx.h[7] = 0x5be0cd19;
}

template <class = void>
void
update(sha256_context& ctx, void const* message, std::size_t size) noexcept
{
    auto const pm = reinterpret_cast<unsigned char const*>(message);
    unsigned int block_nb;
    unsigned int new_len, rem_len, tmp_len;
    const unsigned char* shifted_message;
    tmp_len = sha256_context::block_size - ctx.len;
    rem_len = size < tmp_len ? size : tmp_len;
    std::memcpy(&ctx.block[ctx.len], pm, rem_len);
    if (ctx.len + size < sha256_context::block_size)
    {
        ctx.len += size;
        return;
    }
    new_len = size - rem_len;
    block_nb = new_len / sha256_context::block_size;
    shifted_message = pm + rem_len;
    sha256_transform(ctx, ctx.block, 1);
    sha256_transform(ctx, shifted_message, block_nb);
    rem_len = new_len % sha256_context::block_size;
    std::memcpy(ctx.block, &shifted_message[block_nb << 6], rem_len);
    ctx.len = rem_len;
    ctx.tot_len += (block_nb + 1) << 6;
}

template <class = void>
void
finish(sha256_context& ctx, void* digest) noexcept
{
    auto const pd = reinterpret_cast<unsigned char*>(digest);
    unsigned int block_nb;
    unsigned int pm_len;
    unsigned int len_b;
    int i;
    block_nb =
        (1 +
         ((sha256_context::block_size - 9) <
          (ctx.len % sha256_context::block_size)));
    len_b = (ctx.tot_len + ctx.len) << 3;
    pm_len = block_nb << 6;
    std::memset(ctx.block + ctx.len, 0, pm_len - ctx.len);
    ctx.block[ctx.len] = 0x80;
    BEAST_SHA2_UNPACK32(len_b, ctx.block + pm_len - 4);
    sha256_transform(ctx, ctx.block, block_nb);
    for (i = 0; i < 8; i++)
        BEAST_SHA2_UNPACK32(ctx.h[i], &pd[i << 2]);
}

//------------------------------------------------------------------------------

// SHA512

template <class = void>
void
sha512_transform(
    sha512_context& ctx,
    unsigned char const* message,
    unsigned int block_nb) noexcept
{
    static unsigned long long const K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
        0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
        0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
        0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
        0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
        0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
        0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
        0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
        0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
        0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
        0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

    std::uint64_t w[80];
    std::uint64_t wv[8];
    std::uint64_t t1, t2;
    unsigned char const* sub_block;
    int i, j;
    for (i = 0; i < (int)block_nb; i++)
    {
        sub_block = message + (i << 7);
        for (j = 0; j < 16; j++)
            BEAST_SHA2_PACK64(&sub_block[j << 3], &w[j]);
        for (j = 16; j < 80; j++)
            w[j] = BEAST_SHA512_F4(w[j - 2]) + w[j - 7] +
                BEAST_SHA512_F3(w[j - 15]) + w[j - 16];
        for (j = 0; j < 8; j++)
            wv[j] = ctx.h[j];
        for (j = 0; j < 80; j++)
        {
            t1 = wv[7] + BEAST_SHA512_F2(wv[4]) +
                BEAST_SHA2_CH(wv[4], wv[5], wv[6]) + K[j] + w[j];
            t2 = BEAST_SHA512_F1(wv[0]) + BEAST_SHA2_MAJ(wv[0], wv[1], wv[2]);
            wv[7] = wv[6];
            wv[6] = wv[5];
            wv[5] = wv[4];
            wv[4] = wv[3] + t1;
            wv[3] = wv[2];
            wv[2] = wv[1];
            wv[1] = wv[0];
            wv[0] = t1 + t2;
        }
        for (j = 0; j < 8; j++)
            ctx.h[j] += wv[j];
    }
}

template <class = void>
void
init(sha512_context& ctx) noexcept
{
    ctx.len = 0;
    ctx.tot_len = 0;
    ctx.h[0] = 0x6a09e667f3bcc908ULL;
    ctx.h[1] = 0xbb67ae8584caa73bULL;
    ctx.h[2] = 0x3c6ef372fe94f82bULL;
    ctx.h[3] = 0xa54ff53a5f1d36f1ULL;
    ctx.h[4] = 0x510e527fade682d1ULL;
    ctx.h[5] = 0x9b05688c2b3e6c1fULL;
    ctx.h[6] = 0x1f83d9abfb41bd6bULL;
    ctx.h[7] = 0x5be0cd19137e2179ULL;
}

template <class = void>
void
update(sha512_context& ctx, void const* message, std::size_t size) noexcept
{
    auto const pm = reinterpret_cast<unsigned char const*>(message);
    unsigned int block_nb;
    unsigned int new_len, rem_len, tmp_len;
    const unsigned char* shifted_message;
    tmp_len = sha512_context::block_size - ctx.len;
    rem_len = size < tmp_len ? size : tmp_len;
    std::memcpy(&ctx.block[ctx.len], pm, rem_len);
    if (ctx.len + size < sha512_context::block_size)
    {
        ctx.len += size;
        return;
    }
    new_len = size - rem_len;
    block_nb = new_len / sha512_context::block_size;
    shifted_message = pm + rem_len;
    sha512_transform(ctx, ctx.block, 1);
    sha512_transform(ctx, shifted_message, block_nb);
    rem_len = new_len % sha512_context::block_size;
    std::memcpy(ctx.block, &shifted_message[block_nb << 7], rem_len);
    ctx.len = rem_len;
    ctx.tot_len += (block_nb + 1) << 7;
}

template <class = void>
void
finish(sha512_context& ctx, void* digest) noexcept
{
    auto const pd = reinterpret_cast<unsigned char*>(digest);
    unsigned int block_nb;
    unsigned int pm_len;
    unsigned int len_b;
    int i;
    block_nb = 1 +
        ((sha512_context::block_size - 17) <
         (ctx.len % sha512_context::block_size));
    len_b = (ctx.tot_len + ctx.len) << 3;
    pm_len = block_nb << 7;
    std::memset(ctx.block + ctx.len, 0, pm_len - ctx.len);
    ctx.block[ctx.len] = 0x80;
    BEAST_SHA2_UNPACK32(len_b, ctx.block + pm_len - 4);
    sha512_transform(ctx, ctx.block, block_nb);
    for (i = 0; i < 8; i++)
        BEAST_SHA2_UNPACK64(ctx.h[i], &pd[i << 3]);
}

}  // namespace detail
}  // namespace beast

#endif
