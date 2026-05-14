// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/*
   Portions from http://www.adp-gmbh.ch/cpp/common/base64.html
   Copyright notice:

   base64.cpp and base64.h

   Copyright (C) 2004-2008 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

/** @file
 *  RFC 4648 Base64 codec for the XRPL ledger library.
 *
 *  Derived from René Nyffenegger's public-domain implementation (2004–2008).
 *  Two API layers are provided: the inner `xrpl::base64` namespace exposes
 *  buffer-oriented primitives that avoid heap allocation, while the outer
 *  `xrpl` namespace exposes `base64Encode` / `base64Decode` which manage
 *  `std::string` memory automatically.  All functions are fully re-entrant;
 *  all mutable state is function-local and the lookup tables are `constexpr`.
 */

#include <xrpl/basics/base64.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl {

namespace base64 {

/** Return a pointer to the 64-character Base64 alphabet (A–Z, a–z, 0–9, +, /).
 *
 *  The array is stored as a function-local `static constexpr` and its
 *  lifetime is that of the program, so the returned pointer is always valid.
 */
inline char const*
getAlphabet()
{
    static char constexpr kTAB[] = {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
    return &kTAB[0];
}

/** Return a pointer to the 256-entry inverse-alphabet lookup table.
 *
 *  For each byte value `b`, `getInverse()[b]` is the 6-bit Base64 value of
 *  the character (0–63), or -1 if `b` is not a valid Base64 character.
 *  Using a flat 256-element array makes validation and value extraction a
 *  single array index — O(1) with no per-character branching.
 */
inline signed char const*
getInverse()
{
    static signed char constexpr kTAB[] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  //   0-15
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  //  16-31
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,  //  32-47
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,  //  48-63
        -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,  //  64-79
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,  //  80-95
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  //  96-111
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,  // 112-127
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 128-143
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 144-159
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 160-175
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 176-191
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 192-207
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 208-223
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 224-239
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1   // 240-255
    };
    return &kTAB[0];
}

/** Compute the exact number of Base64 characters produced by encoding `n` bytes.
 *
 *  The result is `4 * ⌈n / 3⌉`, always a multiple of four due to `=` padding.
 *
 *  @param n Number of raw input bytes.
 *  @return Exact output size in characters (no null terminator included).
 */
std::size_t constexpr encodedSize(std::size_t n)
{
    return 4 * ((n + 2) / 3);
}

/** Compute an upper-bound buffer size sufficient to hold the decoded output of `n` Base64 characters.
 *
 *  Returns `(n / 4) * 3 + 2`, which is deliberately conservative: the `+2`
 *  guarantees the caller's pre-allocated buffer is always large enough
 *  regardless of `=` padding or a partial trailing group.  The actual number
 *  of bytes written is returned by `decode()`, not by this function.
 *
 *  @param n Number of Base64 input characters.
 *  @return Upper-bound byte count for the decoded output buffer.
 */
std::size_t constexpr decodedSize(std::size_t n)
{
    return ((n / 4) * 3) + 2;
}

/** Encode raw bytes as a padded Base64 string into a caller-supplied buffer.
 *
 *  Processes input three bytes at a time, emitting four Base64 characters per
 *  group.  A one- or two-byte tail is handled with `=` padding so the output
 *  length is always a multiple of four.  The output is not null-terminated.
 *
 *  @param dest Destination buffer; must be at least `encodedSize(len)` bytes.
 *  @param src  Source buffer containing the raw bytes to encode.
 *  @param len  Number of bytes to read from `src`.
 *  @return Number of Base64 characters written to `dest` (no null terminator).
 */
std::size_t
encode(void* dest, void const* src, std::size_t len)
{
    char* out = static_cast<char*>(dest);  // NOLINT(misc-const-correctness)
    char const* in = static_cast<char const*>(src);
    auto const tab = base64::getAlphabet();

    for (auto n = len / 3; n > 0; --n)
    {
        *out++ = tab[(in[0] & 0xfc) >> 2];
        *out++ = tab[((in[0] & 0x03) << 4) + ((in[1] & 0xf0) >> 4)];
        *out++ = tab[((in[2] & 0xc0) >> 6) + ((in[1] & 0x0f) << 2)];
        *out++ = tab[in[2] & 0x3f];
        in += 3;
    }

    // NOLINTNEXTLINE(bugprone-switch-missing-default-case)
    switch (len % 3)
    {
        case 2:
            *out++ = tab[(in[0] & 0xfc) >> 2];
            *out++ = tab[((in[0] & 0x03) << 4) + ((in[1] & 0xf0) >> 4)];
            *out++ = tab[(in[1] & 0x0f) << 2];
            *out++ = '=';
            break;

        case 1:
            *out++ = tab[(in[0] & 0xfc) >> 2];
            *out++ = tab[((in[0] & 0x03) << 4)];
            *out++ = '=';
            *out++ = '=';
            break;

        case 0:
            break;
    }

    return out - static_cast<char*>(dest);
}

/** Decode a Base64 string into raw bytes in a caller-supplied buffer.
 *
 *  Reads four Base64 characters at a time and reconstructs three output bytes
 *  per group.  Decoding stops at the first `=` padding character, the first
 *  character that maps to -1 in the inverse table (i.e. not in the Base64
 *  alphabet), or when `len` input characters have been consumed — whichever
 *  comes first.  Any partial group of 1–3 valid characters accumulated before
 *  stopping produces `i - 1` additional output bytes.
 *
 *  @note There is no error return: invalid input silently terminates decoding.
 *      For example, `decode("not_base64!!")` yields the same output as
 *      `decode("not")` because `_` is not a valid Base64 character.  Callers
 *      that need to detect partial decodes must compare the returned byte count
 *      against the expected output size themselves.
 *
 *  @param dest Destination buffer; must be at least `decodedSize(len)` bytes.
 *  @param src  Pointer to the Base64-encoded input characters.
 *  @param len  Maximum number of input characters to consume.
 *  @return A pair `{bytesWritten, charsConsumed}`: the number of raw bytes
 *      written to `dest` and the number of input characters read from `src`.
 */
std::pair<std::size_t, std::size_t>
decode(void* dest, char const* src, std::size_t len)
{
    char* out = static_cast<char*>(dest);  // NOLINT(misc-const-correctness)
    auto in = reinterpret_cast<unsigned char const*>(src);
    unsigned char c3[3]{}, c4[4]{};
    int i = 0;
    int j = 0;

    auto const inverse = base64::getInverse();

    while (((len--) != 0u) && *in != '=')
    {
        auto const v = inverse[*in];
        if (v == -1)
            break;
        ++in;
        c4[i] = v;
        if (++i; i == 4)
        {
            c3[0] = (c4[0] << 2) + ((c4[1] & 0x30) >> 4);
            c3[1] = ((c4[1] & 0xf) << 4) + ((c4[2] & 0x3c) >> 2);
            c3[2] = ((c4[2] & 0x3) << 6) + c4[3];

            for (i = 0; i < 3; i++)
                *out++ = c3[i];
            i = 0;
        }
    }

    if (i != 0)
    {
        c3[0] = (c4[0] << 2) + ((c4[1] & 0x30) >> 4);
        c3[1] = ((c4[1] & 0xf) << 4) + ((c4[2] & 0x3c) >> 2);
        c3[2] = ((c4[2] & 0x3) << 6) + c4[3];

        for (j = 0; j < i - 1; j++)
            *out++ = c3[j];
    }

    return {out - static_cast<char*>(dest), in - reinterpret_cast<unsigned char const*>(src)};
}

}  // namespace base64

/** Encode raw bytes as a Base64 string.
 *
 *  Pre-allocates the output string to the exact encoded size, fills it via
 *  the buffer-oriented `base64::encode`, then returns it without any extra
 *  copy or reallocation.
 *
 *  @param data Pointer to the raw bytes to encode.
 *  @param len  Number of bytes to encode.
 *  @return Base64-encoded string with `=` padding; length is always a
 *      multiple of four.
 */
std::string
base64Encode(std::uint8_t const* data, std::size_t len)
{
    std::string dest;
    dest.resize(base64::encodedSize(len));
    dest.resize(base64::encode(&dest[0], data, len));
    return dest;
}

/** Decode a Base64 string, returning the raw bytes.
 *
 *  Pre-allocates to the conservative upper-bound size from
 *  `base64::decodedSize`, invokes `base64::decode`, then shrinks the string
 *  to the actual byte count before returning.  Decoding stops silently at
 *  the first invalid or padding character; no exception is thrown and no
 *  error status is returned.
 *
 *  @param data Base64-encoded input; need not be null-terminated.
 *  @return Decoded byte string.  If `data` contains invalid Base64 characters,
 *      only the bytes decoded before the first invalid character are included.
 */
std::string
base64Decode(std::string_view data)
{
    std::string dest;
    dest.resize(base64::decodedSize(data.size()));
    auto const result = base64::decode(&dest[0], data.data(), data.size());
    dest.resize(result.first);
    return dest;
}

}  // namespace xrpl
