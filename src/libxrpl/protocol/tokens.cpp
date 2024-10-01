//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
//
/* The base58 encoding & decoding routines in the b58_ref namespace are taken
 * from Bitcoin but have been modified from the original.
 *
 * Copyright (c) 2014 The Bitcoin Core developers
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 */

#include <xrpl/protocol/tokens.h>

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/detail/b58_utils.h>
#include <xrpl/protocol/digest.h>

#include <boost/container/small_vector.hpp>
#include <boost/endian.hpp>
#include <boost/endian/conversion.hpp>

#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

/*
Converting between bases is straight forward. First, some background:

Given the coefficients C[m], ... ,C[0] and base B, those coefficients represent
the number C[m]*B^m + ... + C[0]*B^0; The following pseudo-code converts the
coefficients to the (infinite precision) integer N:

```
N = 0;
i = m ;; N.B. m is the index of the largest coefficient
while (i>=0)
    N = N + C[i]*B^i
    i = i - 1
```

For example, in base 10, the number 437 represents the integer 4*10^2 + 3*10^1 +
7*10^0. In base 16, 437 is the same as 4*16^2 + 3*16^1 + 7*16^0.

To find the coefficients that represent the integer N in base B, we start by
computing the lowest order coefficients and work up to the highest order
coefficients. The following pseudo-code converts the (infinite precision)
integer N to the correct coefficients:

```
i = 0
while(N)
    C[i] = N mod B
    N = floor(N/B)
    i = i + 1
```

For example, to find the coefficients of the integer 437 in base 10:

C[0] is 437 mod 10; C[0] = 7;
N is floor(437/10); N = 43;
C[1] is 43 mod 10; C[1] = 3;
N is floor(43/10); N = 4;
C[2] is 4 mod 10; C[2] = 4;
N is floor(4/10); N = 0;
Since N is 0, the algorithm stops.


To convert between a number represented with coefficients from base B1 to that
same number represented with coefficients from base B2, we can use the algorithm
that converts coefficients from base B1 to an integer, and then use the
algorithm that converts a number to coefficients from base B2.

There is a useful shortcut that can be used if one of the bases is a power of
the other base. If B1 == B2^G, then each coefficient from base B1 can be
converted to base B2 independently to create a group of "G" B2 coefficient.
These coefficients can be simply concatenated together. Since 16 == 2^4, this
property is what makes base 16 useful when dealing with binary numbers. For
example consider converting the base 16 number "93" to binary. The base 16
coefficient 9 is represented in base 2 with the coefficients 1,0,0,1. The base
16 coefficient 3 is represented in base 2 with the coefficients 0,0,1,1. To get
the final answer, just concatenate those two independent conversions together.
The base 16 number "93" is the binary number "10010011".

The original (now reference) algorithm to convert from base 58 to a binary
number used the

```
N = 0;
for i in m to 0 inclusive
    N = N + C[i]*B^i
```

algorithm.

However, the algorithm above is pseudo-code. In particular, the variable "N" is
an infinite precision integer in that pseudo-code. Real computers do
computations on registers, and these registers have limited length. Modern
computers use 64-bit general purpose registers, and can multiply two 64 bit
numbers and obtain a 128 bit result (in two registers).

The original algorithm in essence converted from base 58 to base 256 (base
2^8). The new, faster algorithm converts from base 58 to base 58^10 (this is
fast using the shortcut described above), then from base 58^10 to base 2^64
(this is slow, and requires multi-precision arithmetic), and then from base 2^64
to base 2^8 (this is fast, using the shortcut described above). Base 58^10 is
chosen because it is the largest power of 58 that will fit into a 64-bit
register.

While it may seem counter-intuitive that converting from base 58 -> base 58^10
-> base 2^64 -> base 2^8 is faster than directly converting from base 58 -> base
2^8, it is actually 10x-15x faster. The reason for the speed increase is two of
the conversions are trivial (converting between bases where one base is a power
of another base), and doing the multi-precision computations with larger
coefficients sizes greatly speeds up the multi-precision computations.
*/

namespace ripple {

static constexpr char const* alphabetForward =
    "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

static constexpr std::array<int, 256> const alphabetReverse = []() {
    std::array<int, 256> map{};
    for (auto& m : map)
        m = -1;
    for (int i = 0, j = 0; alphabetForward[i] != 0; ++i)
        map[static_cast<unsigned char>(alphabetForward[i])] = j++;
    return map;
}();

template <class Hasher>
static typename Hasher::result_type
digest(void const* data, std::size_t size) noexcept
{
    Hasher h;
    h(data, size);
    return static_cast<typename Hasher::result_type>(h);
}

template <
    class Hasher,
    class T,
    std::size_t N,
    class = std::enable_if_t<sizeof(T) == 1>>
static typename Hasher::result_type
digest(std::array<T, N> const& v)
{
    return digest<Hasher>(v.data(), v.size());
}

// Computes a double digest (e.g. digest of the digest)
template <class Hasher, class... Args>
static typename Hasher::result_type
digest2(Args const&... args)
{
    return digest<Hasher>(digest<Hasher>(args...));
}

/** Calculate a 4-byte checksum of the data

    The checksum is calculated as the first 4 bytes
    of the SHA256 digest of the message. This is added
    to the base58 encoding of identifiers to detect
    user error in data entry.

    @note This checksum algorithm is part of the client API
*/
static void
checksum(void* out, void const* message, std::size_t size)
{
    auto const h = digest2<sha256_hasher>(message, size);
    std::memcpy(out, h.data(), 4);
}

[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size)
{
#ifndef _MSC_VER
    return b58_fast::encodeBase58Token(type, token, size);
#else
    return b58_ref::encodeBase58Token(type, token, size);
#endif
}

[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type)
{
#ifndef _MSC_VER
    return b58_fast::decodeBase58Token(s, type);
#else
    return b58_ref::decodeBase58Token(s, type);
#endif
}

namespace b58_ref {

namespace detail {

std::string
encodeBase58(
    void const* message,
    std::size_t size,
    void* temp,
    std::size_t temp_size)
{
    auto pbegin = reinterpret_cast<unsigned char const*>(message);
    auto const pend = pbegin + size;

    // Skip & count leading zeroes.
    int zeroes = 0;
    while (pbegin != pend && *pbegin == 0)
    {
        pbegin++;
        zeroes++;
    }

    auto const b58begin = reinterpret_cast<unsigned char*>(temp);
    auto const b58end = b58begin + temp_size;

    std::fill(b58begin, b58end, 0);

    while (pbegin != pend)
    {
        int carry = *pbegin;
        // Apply "b58 = b58 * 256 + ch".
        for (auto iter = b58end; iter != b58begin; --iter)
        {
            carry += 256 * (iter[-1]);
            iter[-1] = carry % 58;
            carry /= 58;
        }
        ASSERT(
            carry == 0, "ripple::b58_ref::detail::encodeBase58 : zero carry");
        pbegin++;
    }

    // Skip leading zeroes in base58 result.
    auto iter = b58begin;
    while (iter != b58end && *iter == 0)
        ++iter;

    // Translate the result into a string.
    std::string str;
    str.reserve(zeroes + (b58end - iter));
    str.assign(zeroes, alphabetForward[0]);
    while (iter != b58end)
        str += alphabetForward[*(iter++)];
    return str;
}

std::string
decodeBase58(std::string const& s)
{
    auto psz = reinterpret_cast<unsigned char const*>(s.c_str());
    auto remain = s.size();
    // Skip and count leading zeroes
    int zeroes = 0;
    while (remain > 0 && alphabetReverse[*psz] == 0)
    {
        ++zeroes;
        ++psz;
        --remain;
    }

    if (remain > 64)
        return {};

    // Allocate enough space in big-endian base256 representation.
    // log(58) / log(256), rounded up.
    std::vector<unsigned char> b256(remain * 733 / 1000 + 1);
    while (remain > 0)
    {
        auto carry = alphabetReverse[*psz];
        if (carry == -1)
            return {};
        // Apply "b256 = b256 * 58 + carry".
        for (auto iter = b256.rbegin(); iter != b256.rend(); ++iter)
        {
            carry += 58 * *iter;
            *iter = carry % 256;
            carry /= 256;
        }
        ASSERT(
            carry == 0, "ripple::b58_ref::detail::decodeBase58 : zero carry");
        ++psz;
        --remain;
    }
    // Skip leading zeroes in b256.
    auto iter = std::find_if(
        b256.begin(), b256.end(), [](unsigned char c) { return c != 0; });
    std::string result;
    result.reserve(zeroes + (b256.end() - iter));
    result.assign(zeroes, 0x00);
    while (iter != b256.end())
        result.push_back(*(iter++));
    return result;
}

}  // namespace detail

std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size)
{
    // expanded token includes type + 4 byte checksum
    auto const expanded = 1 + size + 4;

    // We need expanded + expanded * (log(256) / log(58)) which is
    // bounded by expanded + expanded * (138 / 100 + 1) which works
    // out to expanded * 3:
    auto const bufsize = expanded * 3;

    boost::container::small_vector<std::uint8_t, 1024> buf(bufsize);

    // Lay the data out as
    //      <type><token><checksum>
    buf[0] = safe_cast<std::underlying_type_t<TokenType>>(type);
    if (size)
        std::memcpy(buf.data() + 1, token, size);
    checksum(buf.data() + 1 + size, buf.data(), 1 + size);

    return detail::encodeBase58(
        buf.data(), expanded, buf.data() + expanded, bufsize - expanded);
}

std::string
decodeBase58Token(std::string const& s, TokenType type)
{
    std::string const ret = detail::decodeBase58(s);

    // Reject zero length tokens
    if (ret.size() < 6)
        return {};

    // The type must match.
    if (type != safe_cast<TokenType>(static_cast<std::uint8_t>(ret[0])))
        return {};

    // And the checksum must as well.
    std::array<char, 4> guard;
    checksum(guard.data(), ret.data(), ret.size() - guard.size());
    if (!std::equal(guard.rbegin(), guard.rend(), ret.rbegin()))
        return {};

    // Skip the leading type byte and the trailing checksum.
    return ret.substr(1, ret.size() - 1 - guard.size());
}
}  // namespace b58_ref

#ifndef _MSC_VER
// The algorithms use gcc's int128 (fast MS version will have to wait, in the
// meantime MS falls back to the slower reference implementation)
namespace b58_fast {
namespace detail {
// Note: both the input and output will be BIG ENDIAN
B58Result<std::span<std::uint8_t>>
b256_to_b58_be(std::span<std::uint8_t const> input, std::span<std::uint8_t> out)
{
    // Max valid input is 38 bytes:
    // (33 bytes for nodepublic + 1 byte token + 4 bytes checksum)
    if (input.size() > 38)
    {
        return Unexpected(TokenCodecErrc::inputTooLarge);
    };

    auto count_leading_zeros =
        [](std::span<std::uint8_t const> const& col) -> std::size_t {
        std::size_t count = 0;
        for (auto const& c : col)
        {
            if (c != 0)
            {
                return count;
            }
            count += 1;
        }
        return count;
    };

    auto const input_zeros = count_leading_zeros(input);
    input = input.subspan(input_zeros);

    // Allocate enough base 2^64 coeff for encoding 38 bytes
    // log(2^(38*8),2^64)) ~= 4.75. So 5 coeff are enough
    std::array<std::uint64_t, 5> base_2_64_coeff_buf{};
    std::span<std::uint64_t> const base_2_64_coeff =
        [&]() -> std::span<std::uint64_t> {
        // convert input from big endian to native u64, lowest coeff first
        std::size_t num_coeff = 0;
        for (int i = 0; i < base_2_64_coeff_buf.size(); ++i)
        {
            if (i * 8 >= input.size())
            {
                break;
            }
            auto const src_i_end = input.size() - i * 8;
            if (src_i_end >= 8)
            {
                std::memcpy(
                    &base_2_64_coeff_buf[num_coeff], &input[src_i_end - 8], 8);
                boost::endian::big_to_native_inplace(
                    base_2_64_coeff_buf[num_coeff]);
            }
            else
            {
                std::uint64_t be = 0;
                for (int bi = 0; bi < src_i_end; ++bi)
                {
                    be <<= 8;
                    be |= input[bi];
                }
                base_2_64_coeff_buf[num_coeff] = be;
            };
            num_coeff += 1;
        }
        return std::span(base_2_64_coeff_buf.data(), num_coeff);
    }();

    // Allocate enough base 58^10 coeff for encoding 38 bytes
    // log(2^(38*8),58^10)) ~= 5.18. So 6 coeff are enough
    std::array<std::uint64_t, 6> base_58_10_coeff{};
    constexpr std::uint64_t B_58_10 = 430804206899405824;  // 58^10;
    std::size_t num_58_10_coeffs = 0;
    std::size_t cur_2_64_end = base_2_64_coeff.size();
    // compute the base 58^10 coeffs
    while (cur_2_64_end > 0)
    {
        base_58_10_coeff[num_58_10_coeffs] =
            ripple::b58_fast::detail::inplace_bigint_div_rem(
                base_2_64_coeff.subspan(0, cur_2_64_end), B_58_10);
        num_58_10_coeffs += 1;
        if (base_2_64_coeff[cur_2_64_end - 1] == 0)
        {
            cur_2_64_end -= 1;
        }
    }

    // Translate the result into the alphabet
    // Put all the zeros at the beginning, then all the values from the output
    std::fill(
        out.begin(), out.begin() + input_zeros, ::ripple::alphabetForward[0]);

    // iterate through the base 58^10 coeff
    // convert to base 58 big endian then
    // convert to alphabet big endian
    bool skip_zeros = true;
    auto out_index = input_zeros;
    for (int i = num_58_10_coeffs - 1; i >= 0; --i)
    {
        if (skip_zeros && base_58_10_coeff[i] == 0)
        {
            continue;
        }
        static constexpr std::uint64_t B_58_10 = 430804206899405824;  // 58^10;
        if (base_58_10_coeff[i] >= B_58_10)
        {
            return Unexpected(TokenCodecErrc::inputTooLarge);
        }
        std::array<std::uint8_t, 10> const b58_be =
            ripple::b58_fast::detail::b58_10_to_b58_be(base_58_10_coeff[i]);
        std::size_t to_skip = 0;
        std::span<std::uint8_t const> b58_be_s{b58_be.data(), b58_be.size()};
        if (skip_zeros)
        {
            to_skip = count_leading_zeros(b58_be_s);
            skip_zeros = false;
            if (out.size() < (i + 1) * 10 - to_skip)
            {
                return Unexpected(TokenCodecErrc::outputTooSmall);
            }
        }
        for (auto b58_coeff : b58_be_s.subspan(to_skip))
        {
            out[out_index] = ::ripple::alphabetForward[b58_coeff];
            out_index += 1;
        }
    }

    return out.subspan(0, out_index);
}

// Note the input is BIG ENDIAN (some fn in this module use little endian)
B58Result<std::span<std::uint8_t>>
b58_to_b256_be(std::string_view input, std::span<std::uint8_t> out)
{
    // Convert from b58 to b 58^10

    // Max encoded value is 38 bytes
    // log(2^(38*8),58) ~= 51.9
    if (input.size() > 52)
    {
        return Unexpected(TokenCodecErrc::inputTooLarge);
    };
    if (out.size() < 8)
    {
        return Unexpected(TokenCodecErrc::outputTooSmall);
    }

    auto count_leading_zeros = [&](auto const& col) -> std::size_t {
        std::size_t count = 0;
        for (auto const& c : col)
        {
            if (c != ::ripple::alphabetForward[0])
            {
                return count;
            }
            count += 1;
        }
        return count;
    };

    auto const input_zeros = count_leading_zeros(input);

    // Allocate enough base 58^10 coeff for encoding 38 bytes
    // (33 bytes for nodepublic + 1 byte token + 4 bytes checksum)
    // log(2^(38*8),58^10)) ~= 5.18. So 6 coeff are enough
    std::array<std::uint64_t, 6> b_58_10_coeff{};
    auto [num_full_coeffs, partial_coeff_len] =
        ripple::b58_fast::detail::div_rem(input.size(), 10);
    auto const num_partial_coeffs = partial_coeff_len ? 1 : 0;
    auto const num_b_58_10_coeffs = num_full_coeffs + num_partial_coeffs;
    ASSERT(
        num_b_58_10_coeffs <= b_58_10_coeff.size(),
        "ripple::b58_fast::detail::b58_to_b256_be : maximum coeff");
    for (auto c : input.substr(0, partial_coeff_len))
    {
        auto cur_val = ::ripple::alphabetReverse[c];
        if (cur_val < 0)
        {
            return Unexpected(TokenCodecErrc::invalidEncodingChar);
        }
        b_58_10_coeff[0] *= 58;
        b_58_10_coeff[0] += cur_val;
    }
    for (int i = 0; i < 10; ++i)
    {
        for (int j = 0; j < num_full_coeffs; ++j)
        {
            auto c = input[partial_coeff_len + j * 10 + i];
            auto cur_val = ::ripple::alphabetReverse[c];
            if (cur_val < 0)
            {
                return Unexpected(TokenCodecErrc::invalidEncodingChar);
            }
            b_58_10_coeff[num_partial_coeffs + j] *= 58;
            b_58_10_coeff[num_partial_coeffs + j] += cur_val;
        }
    }

    constexpr std::uint64_t B_58_10 = 430804206899405824;  // 58^10;

    // log(2^(38*8),2^64) ~= 4.75)
    std::array<std::uint64_t, 5> result{};
    result[0] = b_58_10_coeff[0];
    std::size_t cur_result_size = 1;
    for (int i = 1; i < num_b_58_10_coeffs; ++i)
    {
        std::uint64_t const c = b_58_10_coeff[i];

        {
            auto code = ripple::b58_fast::detail::inplace_bigint_mul(
                std::span(&result[0], cur_result_size + 1), B_58_10);
            if (code != TokenCodecErrc::success)
            {
                return Unexpected(code);
            }
        }
        {
            auto code = ripple::b58_fast::detail::inplace_bigint_add(
                std::span(&result[0], cur_result_size + 1), c);
            if (code != TokenCodecErrc::success)
            {
                return Unexpected(code);
            }
        }
        if (result[cur_result_size] != 0)
        {
            cur_result_size += 1;
        }
    }
    std::fill(out.begin(), out.begin() + input_zeros, 0);
    auto cur_out_i = input_zeros;
    // Don't write leading zeros to the output for the most significant
    // coeff
    {
        std::uint64_t const c = result[cur_result_size - 1];
        auto skip_zero = true;
        // start and end of output range
        for (int i = 0; i < 8; ++i)
        {
            std::uint8_t const b = (c >> (8 * (7 - i))) & 0xff;
            if (skip_zero)
            {
                if (b == 0)
                {
                    continue;
                }
                skip_zero = false;
            }
            out[cur_out_i] = b;
            cur_out_i += 1;
        }
    }
    if ((cur_out_i + 8 * (cur_result_size - 1)) > out.size())
    {
        return Unexpected(TokenCodecErrc::outputTooSmall);
    }

    for (int i = cur_result_size - 2; i >= 0; --i)
    {
        auto c = result[i];
        boost::endian::native_to_big_inplace(c);
        memcpy(&out[cur_out_i], &c, 8);
        cur_out_i += 8;
    }

    return out.subspan(0, cur_out_i);
}
}  // namespace detail

B58Result<std::span<std::uint8_t>>
encodeBase58Token(
    TokenType token_type,
    std::span<std::uint8_t const> input,
    std::span<std::uint8_t> out)
{
    constexpr std::size_t tmpBufSize = 128;
    std::array<std::uint8_t, tmpBufSize> buf;
    if (input.size() > tmpBufSize - 5)
    {
        return Unexpected(TokenCodecErrc::inputTooLarge);
    }
    if (input.size() == 0)
    {
        return Unexpected(TokenCodecErrc::inputTooSmall);
    }
    // <type (1 byte)><token (input len)><checksum (4 bytes)>
    buf[0] = static_cast<std::uint8_t>(token_type);
    // buf[1..=input.len()] = input;
    memcpy(&buf[1], input.data(), input.size());
    size_t const checksum_i = input.size() + 1;
    // buf[checksum_i..checksum_i + 4] = checksum
    checksum(buf.data() + checksum_i, buf.data(), checksum_i);
    std::span<std::uint8_t const> b58Span(buf.data(), input.size() + 5);
    return detail::b256_to_b58_be(b58Span, out);
}
// Convert from base 58 to base 256, largest coefficients first
// The input is encoded in XPRL format, with the token in the first
// byte and the checksum in the last four bytes.
// The decoded base 256 value does not include the token type or checksum.
// It is an error if the token type or checksum does not match.
B58Result<std::span<std::uint8_t>>
decodeBase58Token(
    TokenType type,
    std::string_view s,
    std::span<std::uint8_t> outBuf)
{
    std::array<std::uint8_t, 64> tmpBuf;
    auto const decodeResult =
        detail::b58_to_b256_be(s, std::span(tmpBuf.data(), tmpBuf.size()));

    if (!decodeResult)
        return decodeResult;

    auto const ret = decodeResult.value();

    // Reject zero length tokens
    if (ret.size() < 6)
        return Unexpected(TokenCodecErrc::inputTooSmall);

    // The type must match.
    if (type != static_cast<TokenType>(static_cast<std::uint8_t>(ret[0])))
        return Unexpected(TokenCodecErrc::mismatchedTokenType);

    // And the checksum must as well.
    std::array<std::uint8_t, 4> guard;
    checksum(guard.data(), ret.data(), ret.size() - guard.size());
    if (!std::equal(guard.rbegin(), guard.rend(), ret.rbegin()))
    {
        return Unexpected(TokenCodecErrc::mismatchedChecksum);
    }

    std::size_t const outSize = ret.size() - 1 - guard.size();
    if (outBuf.size() < outSize)
        return Unexpected(TokenCodecErrc::outputTooSmall);
    // Skip the leading type byte and the trailing checksum.
    std::copy(ret.begin() + 1, ret.begin() + outSize + 1, outBuf.begin());
    return outBuf.subspan(0, outSize);
}

[[nodiscard]] std::string
encodeBase58Token(TokenType type, void const* token, std::size_t size)
{
    std::string sr;
    // The largest object encoded as base58 is 33 bytes; This will be encoded in
    // at most ceil(log(2^256,58)) bytes, or 46 bytes. 128 is plenty (and
    // there's not real benefit making it smaller). Note that 46 bytes may be
    // encoded in more than 46 base58 chars. Since decode uses 64 as the
    // over-allocation, this function uses 128 (again, over-allocation assuming
    // 2 base 58 char per byte)
    sr.resize(128);
    std::span<std::uint8_t> outSp(
        reinterpret_cast<std::uint8_t*>(sr.data()), sr.size());
    std::span<std::uint8_t const> inSp(
        reinterpret_cast<std::uint8_t const*>(token), size);
    auto r = b58_fast::encodeBase58Token(type, inSp, outSp);
    if (!r)
        return {};
    sr.resize(r.value().size());
    return sr;
}

[[nodiscard]] std::string
decodeBase58Token(std::string const& s, TokenType type)
{
    std::string sr;
    // The largest object encoded as base58 is 33 bytes; 64 is plenty (and
    // there's no benefit making it smaller)
    sr.resize(64);
    std::span<std::uint8_t> outSp(
        reinterpret_cast<std::uint8_t*>(sr.data()), sr.size());
    auto r = b58_fast::decodeBase58Token(type, s, outSp);
    if (!r)
        return {};
    sr.resize(r.value().size());
    return sr;
}

}  // namespace b58_fast
#endif  // _MSC_VER
}  // namespace ripple
