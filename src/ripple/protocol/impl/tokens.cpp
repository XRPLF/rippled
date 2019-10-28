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

#include <ripple/basics/safe_cast.h>
#include <ripple/protocol/tokens.h>
#include <ripple/protocol/digest.h>
#include <boost/container/small_vector.hpp>
#include <cassert>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ripple {

static char rippleAlphabet[] =
    "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

static char bitcoinAlphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

//------------------------------------------------------------------------------

template <class Hasher>
static
typename Hasher::result_type
digest (void const* data, std::size_t size) noexcept
{
    Hasher h;
    h(data, size);
    return static_cast<
        typename Hasher::result_type>(h);
}

template <class Hasher, class T, std::size_t N,
    class = std::enable_if_t<sizeof(T) == 1>>
static
typename Hasher::result_type
digest (std::array<T, N> const& v)
{
    return digest<Hasher>(v.data(), v.size());
}

// Computes a double digest (e.g. digest of the digest)
template <class Hasher, class... Args>
static
typename Hasher::result_type
digest2 (Args const&... args)
{
    return digest<Hasher>(
        digest<Hasher>(args...));
}

/*  Calculate a 4-byte checksum of the data

    The checksum is calculated as the first 4 bytes
    of the SHA256 digest of the message. This is added
    to the base58 encoding of identifiers to detect
    user error in data entry.

    @note This checksum algorithm is part of the client API
*/
void
checksum (void* out,
    void const* message,
        std::size_t size)
{
    auto const h =
        digest2<sha256_hasher>(message, size);
    std::memcpy(out, h.data(), 4);
}

//------------------------------------------------------------------------------

// Code from Bitcoin: https://github.com/bitcoin/bitcoin
// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Modified from the original
//
// WARNING Do not call this directly, use
//         encodeBase58Token instead since it
//         calculates the size of buffer needed.
static
std::string
encodeBase58(
    void const* message, std::size_t size,
        void *temp, std::size_t temp_size,
            char const* const alphabet)
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
        assert(carry == 0);
        pbegin++;
    }

    // Skip leading zeroes in base58 result.
    auto iter = b58begin;
    while (iter != b58end && *iter == 0)
        ++iter;

    // Translate the result into a string.
    std::string str;
    str.reserve(zeroes + (b58end - iter));
    str.assign(zeroes, alphabet[0]);
    while (iter != b58end)
        str += alphabet[*(iter++)];
    return str;
}

static
std::string
encodeToken (TokenType type,
    void const* token, std::size_t size, char const* const alphabet)
{
    // expanded token includes type + 4 byte checksum
    auto const expanded = 1 + size + 4;

    // We need expanded + expanded * (log(256) / log(58)) which is
    // bounded by expanded + expanded * (138 / 100 + 1) which works
    // out to expanded * 3:
    auto const bufsize = expanded * 3;

    boost::container::small_vector<std::uint8_t, 1024> buf (bufsize);

    // Lay the data out as
    //      <type><token><checksum>
    buf[0] = safe_cast<std::underlying_type_t <TokenType>>(type);
    if (size)
        std::memcpy(buf.data() + 1, token, size);
    checksum(buf.data() + 1 + size, buf.data(), 1 + size);

    return encodeBase58(
        buf.data(), expanded,
        buf.data() + expanded, bufsize - expanded,
        alphabet);
}

std::string
base58EncodeToken (TokenType type,
    void const* token, std::size_t size)
{
    return encodeToken(type, token, size, rippleAlphabet);
}

std::string
base58EncodeTokenBitcoin (TokenType type,
    void const* token, std::size_t size)
{
    return encodeToken(type, token, size, bitcoinAlphabet);
}

//------------------------------------------------------------------------------

// Code from Bitcoin: https://github.com/bitcoin/bitcoin
// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Modified from the original
template <class InverseArray>
static
std::string
decodeBase58 (std::string const& s,
    InverseArray const& inv)
{
    auto psz = s.c_str();
    auto remain = s.size();
    // Skip and count leading zeroes
    int zeroes = 0;
    while (remain > 0 && inv[*psz] == 0)
    {
        ++zeroes;
        ++psz;
        --remain;
    }

    if (remain > 64)
        return {};

    // Allocate enough space in big-endian base256 representation.
    // log(58) / log(256), rounded up.
    std::vector<unsigned char> b256(
        remain * 733 / 1000 + 1);
    while (remain > 0)
    {
        auto carry = inv[*psz];
        if (carry == -1)
            return {};
        // Apply "b256 = b256 * 58 + carry".
        for (auto iter = b256.rbegin();
            iter != b256.rend(); ++iter)
        {
            carry += 58 * *iter;
            *iter = carry % 256;
            carry /= 256;
        }
        assert(carry == 0);
        ++psz;
        --remain;
    }
    // Skip leading zeroes in b256.
    auto iter = std::find_if(
        b256.begin(), b256.end(),[](unsigned char c)
            { return c != 0; });
    std::string result;
    result.reserve (zeroes + (b256.end() - iter));
    result.assign (zeroes, 0x00);
    while (iter != b256.end())
        result.push_back(*(iter++));
    return result;
}

/*  Base58 decode a Ripple token

    The type and checksum are are checked
    and removed from the returned result.
*/
template <class InverseArray>
static
std::string
decodeBase58Token (std::string const& s,
    TokenType type, InverseArray const& inv)
{
    std::string const ret = decodeBase58(s, inv);

    // Reject zero length tokens
    if (ret.size() < 6)
        return {};

    // The type must match.
    if (type != safe_cast<TokenType>(static_cast<std::uint8_t>(ret[0])))
        return {};

    // And the checksum must as well.
    std::array<char, 4> guard;
    checksum(guard.data(), ret.data(), ret.size() - guard.size());
    if (!std::equal (guard.rbegin(), guard.rend(), ret.rbegin()))
        return {};

    // Skip the leading type byte and the trailing checksum.
    return ret.substr(1, ret.size() - 1 - guard.size());
}

//------------------------------------------------------------------------------

// Maps characters to their base58 digit
class InverseAlphabet
{
private:
    std::array<int, 256> map_;

public:
    explicit
    InverseAlphabet(std::string const& digits)
    {
        map_.fill(-1);
        int i = 0;
        for(auto const c : digits)
            map_[static_cast<
                unsigned char>(c)] = i++;
    }

    int
    operator[](char c) const
    {
        return map_[static_cast<
            unsigned char>(c)];
    }
};

static InverseAlphabet rippleInverse(rippleAlphabet);

static InverseAlphabet bitcoinInverse(bitcoinAlphabet);

std::string
decodeBase58Token(
    std::string const& s, TokenType type)
{
    return decodeBase58Token(s, type, rippleInverse);
}

std::string
decodeBase58TokenBitcoin(
    std::string const& s, TokenType type)
{
    return decodeBase58Token(s, type, bitcoinInverse);
}

} // ripple
