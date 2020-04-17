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

#ifndef BEAST_CRYPTO_RIPEMD_CONTEXT_H_INCLUDED
#define BEAST_CRYPTO_RIPEMD_CONTEXT_H_INCLUDED

#include <array>
#include <cstdint>
#include <cstring>

namespace beast {
namespace detail {

// Based on
// https://code.google.com/p/blockchain/source/browse/trunk/RIPEMD160.cpp
/*
    Copyright (c) Katholieke Universiteit Leuven
    1996 All Rights Reserved

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.
*/
// This implementation has been modified from the
// original. It has been updated for C++11.

struct ripemd160_context
{
    explicit ripemd160_context() = default;

    static unsigned int const block_size = 64;
    static unsigned int const digest_size = 20;

    unsigned int tot_len;
    unsigned int len;
    unsigned char block[256];
    std::uint32_t h[5];
};

// ROL(x, n) cyclically rotates x over n bits to the left
// x must be of an unsigned 32 bits type and 0 <= n < 32.
#define BEAST_RIPEMD_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

// the five basic functions F(), G() and H()
#define BEAST_RIPEMD_F(x, y, z) ((x) ^ (y) ^ (z))
#define BEAST_RIPEMD_G(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define BEAST_RIPEMD_H(x, y, z) (((x) | ~(y)) ^ (z))
#define BEAST_RIPEMD_I(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define BEAST_RIPEMD_J(x, y, z) ((x) ^ ((y) | ~(z)))

// the ten basic operations FF() through III()
#define BEAST_RIPEMD_FF(a, b, c, d, e, x, s)        \
    {                                               \
        (a) += BEAST_RIPEMD_F((b), (c), (d)) + (x); \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);     \
        (c) = BEAST_RIPEMD_ROL((c), 10);            \
    }
#define BEAST_RIPEMD_GG(a, b, c, d, e, x, s)                       \
    {                                                              \
        (a) += BEAST_RIPEMD_G((b), (c), (d)) + (x) + 0x5a827999UL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }
#define BEAST_RIPEMD_HH(a, b, c, d, e, x, s)                       \
    {                                                              \
        (a) += BEAST_RIPEMD_H((b), (c), (d)) + (x) + 0x6ed9eba1UL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }
#define BEAST_RIPEMD_II(a, b, c, d, e, x, s)                       \
    {                                                              \
        (a) += BEAST_RIPEMD_I((b), (c), (d)) + (x) + 0x8f1bbcdcUL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }
#define BEAST_RIPEMD_JJ(a, b, c, d, e, x, s)                       \
    {                                                              \
        (a) += BEAST_RIPEMD_J((b), (c), (d)) + (x) + 0xa953fd4eUL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }
#define BEAST_RIPEMD_FFF(a, b, c, d, e, x, s)       \
    {                                               \
        (a) += BEAST_RIPEMD_F((b), (c), (d)) + (x); \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);     \
        (c) = BEAST_RIPEMD_ROL((c), 10);            \
    }
#define BEAST_RIPEMD_GGG(a, b, c, d, e, x, s)                      \
    {                                                              \
        (a) += BEAST_RIPEMD_G((b), (c), (d)) + (x) + 0x7a6d76e9UL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }
#define BEAST_RIPEMD_HHH(a, b, c, d, e, x, s)                      \
    {                                                              \
        (a) += BEAST_RIPEMD_H((b), (c), (d)) + (x) + 0x6d703ef3UL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }
#define BEAST_RIPEMD_III(a, b, c, d, e, x, s)                      \
    {                                                              \
        (a) += BEAST_RIPEMD_I((b), (c), (d)) + (x) + 0x5c4dd124UL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }
#define BEAST_RIPEMD_JJJ(a, b, c, d, e, x, s)                      \
    {                                                              \
        (a) += BEAST_RIPEMD_J((b), (c), (d)) + (x) + 0x50a28be6UL; \
        (a) = BEAST_RIPEMD_ROL((a), (s)) + (e);                    \
        (c) = BEAST_RIPEMD_ROL((c), 10);                           \
    }

template <class = void>
void
ripemd_load(std::array<std::uint32_t, 16>& X, unsigned char const* p)
{
    for (int i = 0; i < 16; ++i)
    {
        X[i] = ((std::uint32_t) * ((p) + 3) << 24) |
            ((std::uint32_t) * ((p) + 2) << 16) |
            ((std::uint32_t) * ((p) + 1) << 8) | ((std::uint32_t) * (p));
        p += 4;
    }
}

template <class = void>
void
ripemd_compress(
    ripemd160_context& ctx,
    std::array<std::uint32_t, 16>& X) noexcept
{
    std::uint32_t aa = ctx.h[0];
    std::uint32_t bb = ctx.h[1];
    std::uint32_t cc = ctx.h[2];
    std::uint32_t dd = ctx.h[3];
    std::uint32_t ee = ctx.h[4];
    std::uint32_t aaa = ctx.h[0];
    std::uint32_t bbb = ctx.h[1];
    std::uint32_t ccc = ctx.h[2];
    std::uint32_t ddd = ctx.h[3];
    std::uint32_t eee = ctx.h[4];

    // round 1
    BEAST_RIPEMD_FF(aa, bb, cc, dd, ee, X[0], 11);
    BEAST_RIPEMD_FF(ee, aa, bb, cc, dd, X[1], 14);
    BEAST_RIPEMD_FF(dd, ee, aa, bb, cc, X[2], 15);
    BEAST_RIPEMD_FF(cc, dd, ee, aa, bb, X[3], 12);
    BEAST_RIPEMD_FF(bb, cc, dd, ee, aa, X[4], 5);
    BEAST_RIPEMD_FF(aa, bb, cc, dd, ee, X[5], 8);
    BEAST_RIPEMD_FF(ee, aa, bb, cc, dd, X[6], 7);
    BEAST_RIPEMD_FF(dd, ee, aa, bb, cc, X[7], 9);
    BEAST_RIPEMD_FF(cc, dd, ee, aa, bb, X[8], 11);
    BEAST_RIPEMD_FF(bb, cc, dd, ee, aa, X[9], 13);
    BEAST_RIPEMD_FF(aa, bb, cc, dd, ee, X[10], 14);
    BEAST_RIPEMD_FF(ee, aa, bb, cc, dd, X[11], 15);
    BEAST_RIPEMD_FF(dd, ee, aa, bb, cc, X[12], 6);
    BEAST_RIPEMD_FF(cc, dd, ee, aa, bb, X[13], 7);
    BEAST_RIPEMD_FF(bb, cc, dd, ee, aa, X[14], 9);
    BEAST_RIPEMD_FF(aa, bb, cc, dd, ee, X[15], 8);

    // round 2
    BEAST_RIPEMD_GG(ee, aa, bb, cc, dd, X[7], 7);
    BEAST_RIPEMD_GG(dd, ee, aa, bb, cc, X[4], 6);
    BEAST_RIPEMD_GG(cc, dd, ee, aa, bb, X[13], 8);
    BEAST_RIPEMD_GG(bb, cc, dd, ee, aa, X[1], 13);
    BEAST_RIPEMD_GG(aa, bb, cc, dd, ee, X[10], 11);
    BEAST_RIPEMD_GG(ee, aa, bb, cc, dd, X[6], 9);
    BEAST_RIPEMD_GG(dd, ee, aa, bb, cc, X[15], 7);
    BEAST_RIPEMD_GG(cc, dd, ee, aa, bb, X[3], 15);
    BEAST_RIPEMD_GG(bb, cc, dd, ee, aa, X[12], 7);
    BEAST_RIPEMD_GG(aa, bb, cc, dd, ee, X[0], 12);
    BEAST_RIPEMD_GG(ee, aa, bb, cc, dd, X[9], 15);
    BEAST_RIPEMD_GG(dd, ee, aa, bb, cc, X[5], 9);
    BEAST_RIPEMD_GG(cc, dd, ee, aa, bb, X[2], 11);
    BEAST_RIPEMD_GG(bb, cc, dd, ee, aa, X[14], 7);
    BEAST_RIPEMD_GG(aa, bb, cc, dd, ee, X[11], 13);
    BEAST_RIPEMD_GG(ee, aa, bb, cc, dd, X[8], 12);

    // round 3
    BEAST_RIPEMD_HH(dd, ee, aa, bb, cc, X[3], 11);
    BEAST_RIPEMD_HH(cc, dd, ee, aa, bb, X[10], 13);
    BEAST_RIPEMD_HH(bb, cc, dd, ee, aa, X[14], 6);
    BEAST_RIPEMD_HH(aa, bb, cc, dd, ee, X[4], 7);
    BEAST_RIPEMD_HH(ee, aa, bb, cc, dd, X[9], 14);
    BEAST_RIPEMD_HH(dd, ee, aa, bb, cc, X[15], 9);
    BEAST_RIPEMD_HH(cc, dd, ee, aa, bb, X[8], 13);
    BEAST_RIPEMD_HH(bb, cc, dd, ee, aa, X[1], 15);
    BEAST_RIPEMD_HH(aa, bb, cc, dd, ee, X[2], 14);
    BEAST_RIPEMD_HH(ee, aa, bb, cc, dd, X[7], 8);
    BEAST_RIPEMD_HH(dd, ee, aa, bb, cc, X[0], 13);
    BEAST_RIPEMD_HH(cc, dd, ee, aa, bb, X[6], 6);
    BEAST_RIPEMD_HH(bb, cc, dd, ee, aa, X[13], 5);
    BEAST_RIPEMD_HH(aa, bb, cc, dd, ee, X[11], 12);
    BEAST_RIPEMD_HH(ee, aa, bb, cc, dd, X[5], 7);
    BEAST_RIPEMD_HH(dd, ee, aa, bb, cc, X[12], 5);

    // round 4
    BEAST_RIPEMD_II(cc, dd, ee, aa, bb, X[1], 11);
    BEAST_RIPEMD_II(bb, cc, dd, ee, aa, X[9], 12);
    BEAST_RIPEMD_II(aa, bb, cc, dd, ee, X[11], 14);
    BEAST_RIPEMD_II(ee, aa, bb, cc, dd, X[10], 15);
    BEAST_RIPEMD_II(dd, ee, aa, bb, cc, X[0], 14);
    BEAST_RIPEMD_II(cc, dd, ee, aa, bb, X[8], 15);
    BEAST_RIPEMD_II(bb, cc, dd, ee, aa, X[12], 9);
    BEAST_RIPEMD_II(aa, bb, cc, dd, ee, X[4], 8);
    BEAST_RIPEMD_II(ee, aa, bb, cc, dd, X[13], 9);
    BEAST_RIPEMD_II(dd, ee, aa, bb, cc, X[3], 14);
    BEAST_RIPEMD_II(cc, dd, ee, aa, bb, X[7], 5);
    BEAST_RIPEMD_II(bb, cc, dd, ee, aa, X[15], 6);
    BEAST_RIPEMD_II(aa, bb, cc, dd, ee, X[14], 8);
    BEAST_RIPEMD_II(ee, aa, bb, cc, dd, X[5], 6);
    BEAST_RIPEMD_II(dd, ee, aa, bb, cc, X[6], 5);
    BEAST_RIPEMD_II(cc, dd, ee, aa, bb, X[2], 12);

    // round 5
    BEAST_RIPEMD_JJ(bb, cc, dd, ee, aa, X[4], 9);
    BEAST_RIPEMD_JJ(aa, bb, cc, dd, ee, X[0], 15);
    BEAST_RIPEMD_JJ(ee, aa, bb, cc, dd, X[5], 5);
    BEAST_RIPEMD_JJ(dd, ee, aa, bb, cc, X[9], 11);
    BEAST_RIPEMD_JJ(cc, dd, ee, aa, bb, X[7], 6);
    BEAST_RIPEMD_JJ(bb, cc, dd, ee, aa, X[12], 8);
    BEAST_RIPEMD_JJ(aa, bb, cc, dd, ee, X[2], 13);
    BEAST_RIPEMD_JJ(ee, aa, bb, cc, dd, X[10], 12);
    BEAST_RIPEMD_JJ(dd, ee, aa, bb, cc, X[14], 5);
    BEAST_RIPEMD_JJ(cc, dd, ee, aa, bb, X[1], 12);
    BEAST_RIPEMD_JJ(bb, cc, dd, ee, aa, X[3], 13);
    BEAST_RIPEMD_JJ(aa, bb, cc, dd, ee, X[8], 14);
    BEAST_RIPEMD_JJ(ee, aa, bb, cc, dd, X[11], 11);
    BEAST_RIPEMD_JJ(dd, ee, aa, bb, cc, X[6], 8);
    BEAST_RIPEMD_JJ(cc, dd, ee, aa, bb, X[15], 5);
    BEAST_RIPEMD_JJ(bb, cc, dd, ee, aa, X[13], 6);

    // parallel round 1
    BEAST_RIPEMD_JJJ(aaa, bbb, ccc, ddd, eee, X[5], 8);
    BEAST_RIPEMD_JJJ(eee, aaa, bbb, ccc, ddd, X[14], 9);
    BEAST_RIPEMD_JJJ(ddd, eee, aaa, bbb, ccc, X[7], 9);
    BEAST_RIPEMD_JJJ(ccc, ddd, eee, aaa, bbb, X[0], 11);
    BEAST_RIPEMD_JJJ(bbb, ccc, ddd, eee, aaa, X[9], 13);
    BEAST_RIPEMD_JJJ(aaa, bbb, ccc, ddd, eee, X[2], 15);
    BEAST_RIPEMD_JJJ(eee, aaa, bbb, ccc, ddd, X[11], 15);
    BEAST_RIPEMD_JJJ(ddd, eee, aaa, bbb, ccc, X[4], 5);
    BEAST_RIPEMD_JJJ(ccc, ddd, eee, aaa, bbb, X[13], 7);
    BEAST_RIPEMD_JJJ(bbb, ccc, ddd, eee, aaa, X[6], 7);
    BEAST_RIPEMD_JJJ(aaa, bbb, ccc, ddd, eee, X[15], 8);
    BEAST_RIPEMD_JJJ(eee, aaa, bbb, ccc, ddd, X[8], 11);
    BEAST_RIPEMD_JJJ(ddd, eee, aaa, bbb, ccc, X[1], 14);
    BEAST_RIPEMD_JJJ(ccc, ddd, eee, aaa, bbb, X[10], 14);
    BEAST_RIPEMD_JJJ(bbb, ccc, ddd, eee, aaa, X[3], 12);
    BEAST_RIPEMD_JJJ(aaa, bbb, ccc, ddd, eee, X[12], 6);

    // parallel round 2
    BEAST_RIPEMD_III(eee, aaa, bbb, ccc, ddd, X[6], 9);
    BEAST_RIPEMD_III(ddd, eee, aaa, bbb, ccc, X[11], 13);
    BEAST_RIPEMD_III(ccc, ddd, eee, aaa, bbb, X[3], 15);
    BEAST_RIPEMD_III(bbb, ccc, ddd, eee, aaa, X[7], 7);
    BEAST_RIPEMD_III(aaa, bbb, ccc, ddd, eee, X[0], 12);
    BEAST_RIPEMD_III(eee, aaa, bbb, ccc, ddd, X[13], 8);
    BEAST_RIPEMD_III(ddd, eee, aaa, bbb, ccc, X[5], 9);
    BEAST_RIPEMD_III(ccc, ddd, eee, aaa, bbb, X[10], 11);
    BEAST_RIPEMD_III(bbb, ccc, ddd, eee, aaa, X[14], 7);
    BEAST_RIPEMD_III(aaa, bbb, ccc, ddd, eee, X[15], 7);
    BEAST_RIPEMD_III(eee, aaa, bbb, ccc, ddd, X[8], 12);
    BEAST_RIPEMD_III(ddd, eee, aaa, bbb, ccc, X[12], 7);
    BEAST_RIPEMD_III(ccc, ddd, eee, aaa, bbb, X[4], 6);
    BEAST_RIPEMD_III(bbb, ccc, ddd, eee, aaa, X[9], 15);
    BEAST_RIPEMD_III(aaa, bbb, ccc, ddd, eee, X[1], 13);
    BEAST_RIPEMD_III(eee, aaa, bbb, ccc, ddd, X[2], 11);

    // parallel round 3
    BEAST_RIPEMD_HHH(ddd, eee, aaa, bbb, ccc, X[15], 9);
    BEAST_RIPEMD_HHH(ccc, ddd, eee, aaa, bbb, X[5], 7);
    BEAST_RIPEMD_HHH(bbb, ccc, ddd, eee, aaa, X[1], 15);
    BEAST_RIPEMD_HHH(aaa, bbb, ccc, ddd, eee, X[3], 11);
    BEAST_RIPEMD_HHH(eee, aaa, bbb, ccc, ddd, X[7], 8);
    BEAST_RIPEMD_HHH(ddd, eee, aaa, bbb, ccc, X[14], 6);
    BEAST_RIPEMD_HHH(ccc, ddd, eee, aaa, bbb, X[6], 6);
    BEAST_RIPEMD_HHH(bbb, ccc, ddd, eee, aaa, X[9], 14);
    BEAST_RIPEMD_HHH(aaa, bbb, ccc, ddd, eee, X[11], 12);
    BEAST_RIPEMD_HHH(eee, aaa, bbb, ccc, ddd, X[8], 13);
    BEAST_RIPEMD_HHH(ddd, eee, aaa, bbb, ccc, X[12], 5);
    BEAST_RIPEMD_HHH(ccc, ddd, eee, aaa, bbb, X[2], 14);
    BEAST_RIPEMD_HHH(bbb, ccc, ddd, eee, aaa, X[10], 13);
    BEAST_RIPEMD_HHH(aaa, bbb, ccc, ddd, eee, X[0], 13);
    BEAST_RIPEMD_HHH(eee, aaa, bbb, ccc, ddd, X[4], 7);
    BEAST_RIPEMD_HHH(ddd, eee, aaa, bbb, ccc, X[13], 5);

    // parallel round 4
    BEAST_RIPEMD_GGG(ccc, ddd, eee, aaa, bbb, X[8], 15);
    BEAST_RIPEMD_GGG(bbb, ccc, ddd, eee, aaa, X[6], 5);
    BEAST_RIPEMD_GGG(aaa, bbb, ccc, ddd, eee, X[4], 8);
    BEAST_RIPEMD_GGG(eee, aaa, bbb, ccc, ddd, X[1], 11);
    BEAST_RIPEMD_GGG(ddd, eee, aaa, bbb, ccc, X[3], 14);
    BEAST_RIPEMD_GGG(ccc, ddd, eee, aaa, bbb, X[11], 14);
    BEAST_RIPEMD_GGG(bbb, ccc, ddd, eee, aaa, X[15], 6);
    BEAST_RIPEMD_GGG(aaa, bbb, ccc, ddd, eee, X[0], 14);
    BEAST_RIPEMD_GGG(eee, aaa, bbb, ccc, ddd, X[5], 6);
    BEAST_RIPEMD_GGG(ddd, eee, aaa, bbb, ccc, X[12], 9);
    BEAST_RIPEMD_GGG(ccc, ddd, eee, aaa, bbb, X[2], 12);
    BEAST_RIPEMD_GGG(bbb, ccc, ddd, eee, aaa, X[13], 9);
    BEAST_RIPEMD_GGG(aaa, bbb, ccc, ddd, eee, X[9], 12);
    BEAST_RIPEMD_GGG(eee, aaa, bbb, ccc, ddd, X[7], 5);
    BEAST_RIPEMD_GGG(ddd, eee, aaa, bbb, ccc, X[10], 15);
    BEAST_RIPEMD_GGG(ccc, ddd, eee, aaa, bbb, X[14], 8);

    // parallel round 5
    BEAST_RIPEMD_FFF(bbb, ccc, ddd, eee, aaa, X[12], 8);
    BEAST_RIPEMD_FFF(aaa, bbb, ccc, ddd, eee, X[15], 5);
    BEAST_RIPEMD_FFF(eee, aaa, bbb, ccc, ddd, X[10], 12);
    BEAST_RIPEMD_FFF(ddd, eee, aaa, bbb, ccc, X[4], 9);
    BEAST_RIPEMD_FFF(ccc, ddd, eee, aaa, bbb, X[1], 12);
    BEAST_RIPEMD_FFF(bbb, ccc, ddd, eee, aaa, X[5], 5);
    BEAST_RIPEMD_FFF(aaa, bbb, ccc, ddd, eee, X[8], 14);
    BEAST_RIPEMD_FFF(eee, aaa, bbb, ccc, ddd, X[7], 6);
    BEAST_RIPEMD_FFF(ddd, eee, aaa, bbb, ccc, X[6], 8);
    BEAST_RIPEMD_FFF(ccc, ddd, eee, aaa, bbb, X[2], 13);
    BEAST_RIPEMD_FFF(bbb, ccc, ddd, eee, aaa, X[13], 6);
    BEAST_RIPEMD_FFF(aaa, bbb, ccc, ddd, eee, X[14], 5);
    BEAST_RIPEMD_FFF(eee, aaa, bbb, ccc, ddd, X[0], 15);
    BEAST_RIPEMD_FFF(ddd, eee, aaa, bbb, ccc, X[3], 13);
    BEAST_RIPEMD_FFF(ccc, ddd, eee, aaa, bbb, X[9], 11);
    BEAST_RIPEMD_FFF(bbb, ccc, ddd, eee, aaa, X[11], 11);

    // combine results
    ddd += cc + ctx.h[1];  // final result for h[0]
    ctx.h[1] = ctx.h[2] + dd + eee;
    ctx.h[2] = ctx.h[3] + ee + aaa;
    ctx.h[3] = ctx.h[4] + aa + bbb;
    ctx.h[4] = ctx.h[0] + bb + ccc;
    ctx.h[0] = ddd;
}

template <class = void>
void
init(ripemd160_context& ctx) noexcept
{
    ctx.len = 0;
    ctx.tot_len = 0;
    ctx.h[0] = 0x67452301UL;
    ctx.h[1] = 0xefcdab89UL;
    ctx.h[2] = 0x98badcfeUL;
    ctx.h[3] = 0x10325476UL;
    ctx.h[4] = 0xc3d2e1f0UL;
}

template <class = void>
void
update(ripemd160_context& ctx, void const* message, std::size_t size) noexcept
{
    auto const pm = reinterpret_cast<unsigned char const*>(message);
    unsigned int block_nb;
    unsigned int new_len, rem_len, tmp_len;
    const unsigned char* shifted_message;
    tmp_len = ripemd160_context::block_size - ctx.len;
    rem_len = size < tmp_len ? size : tmp_len;
    std::memcpy(&ctx.block[ctx.len], pm, rem_len);
    if (ctx.len + size < ripemd160_context::block_size)
    {
        ctx.len += size;
        return;
    }
    new_len = size - rem_len;
    block_nb = new_len / ripemd160_context::block_size;
    shifted_message = pm + rem_len;
    std::array<std::uint32_t, 16> X;
    ripemd_load(X, ctx.block);
    ripemd_compress(ctx, X);
    for (int i = 0; i < block_nb; ++i)
    {
        ripemd_load(X, shifted_message + i * ripemd160_context::block_size);
        ripemd_compress(ctx, X);
    }
    rem_len = new_len % ripemd160_context::block_size;
    std::memcpy(
        ctx.block,
        &shifted_message[block_nb * ripemd160_context::block_size],
        rem_len);
    ctx.len = rem_len;
    ctx.tot_len += (block_nb + 1) * ripemd160_context::block_size;
}

template <class = void>
void
finish(ripemd160_context& ctx, void* digest) noexcept
{
    std::array<std::uint32_t, 16> X;
    X.fill(0);
    // put leftovers into X
    auto p = &ctx.block[0];
    // uint8_t i goes into word X[i div 4] at pos.  8*(i mod 4)
    for (int i = 0; i < ctx.len; ++i)
        X[i >> 2] ^= (std::uint32_t)*p++ << (8 * (i & 3));
    ctx.tot_len += ctx.len;
    // append the bit m_n == 1
    X[(ctx.tot_len >> 2) & 15] ^= (uint32_t)1 << (8 * (ctx.tot_len & 3) + 7);
    // length goes to next block?
    if ((ctx.tot_len & 63) > 55)
    {
        ripemd_compress(ctx, X);
        X.fill(0);
    }
    // append length in bits*/
    X[14] = ctx.tot_len << 3;
    X[15] = (ctx.tot_len >> 29) | (0 << 3);
    ripemd_compress(ctx, X);

    std::uint8_t* pd = reinterpret_cast<std::uint8_t*>(digest);
    for (std::uint32_t i = 0; i < 20; i += 4)
    {
        pd[i] = (std::uint8_t)(ctx.h[i >> 2]);  // implicit cast to uint8_t
        pd[i + 1] = (std::uint8_t)(ctx.h[i >> 2] >> 8);  // extracts the 8 least
        pd[i + 2] = (std::uint8_t)(ctx.h[i >> 2] >> 16);  // significant bits.
        pd[i + 3] = (std::uint8_t)(ctx.h[i >> 2] >> 24);
    }
}

}  // namespace detail
}  // namespace beast

#endif
