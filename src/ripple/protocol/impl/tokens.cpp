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

#include <ripple/protocol/tokens.h>
#include <ripple/protocol/digest.h>

#include <boost/container/small_vector.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <cassert>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ripple {

// Tokens are encoded as:
// <1-byte TokenType><Optional 2-byte Encoding type for ripple-lib><Data><4-byte checksum>
constexpr size_t checksumBytes = 4;
constexpr size_t familySeedBytes = 16;
// Ripple lib encoded seeds start with a three-byte prefix of:
// <TokenType::None><0xE1><0x4B> rather than the usual one-byte prefix of:
// <TokenType::FamilySeed>
constexpr std::array<std::uint8_t, 2> rippleLibEncodedSeedPrefix{0xE1, 0x4B};

static char rippleAlphabet[] =
    "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

static char bitcoinAlphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

//------------------------------------------------------------------------------

template <class Hasher>
static typename Hasher::result_type
digest(Slice prefix, Slice message) noexcept
{
    Hasher h;
    h(prefix.data(), prefix.size());
    h(message.data(), message.size());
    return static_cast<typename Hasher::result_type>(h);
}

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

/*  Calculate a 4-byte checksum of the data

    The checksum is calculated as the first 4 bytes
    of the SHA256 digest of the message. This is added
    to the base58 encoding of identifiers to detect
    user error in data entry.

    @note This checksum algorithm is part of the client API
*/
static
void
checksum(void* out, void const* message, std::size_t size)
{
    auto const h = digest2<sha256_hasher>(message, size);
    std::memcpy(out, h.data(), checksumBytes);
}

static
void
checksum(void* out, Slice prefix, Slice message)
{
    auto const h = digest2<sha256_hasher>(prefix, message);
    std::memcpy(out, h.data(), checksumBytes);
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
static std::string
encodeBase58(
    void const* message,
    std::size_t size,
    void* temp,
    std::size_t temp_size,
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

static std::string
encodeToken(
    TokenType type,
    void const* token,
    std::size_t size,
    char const* const alphabet)
{
    // expanded token includes type + 4 byte checksum
    auto const expanded = 1 + size + checksumBytes;

    // We need expanded + expanded * (log(256) / log(58)) which is
    // bounded by expanded + expanded * (138 / 100 + 1) which works
    // out to expanded * 3:
    auto const bufsize = expanded * 3;

    boost::container::small_vector<std::uint8_t, 1024> buf(bufsize);

    // Lay the data out as
    //      <type><token><checksum>
    buf[0] = static_cast<std::underlying_type_t<TokenType>>(type);
    if (size)
        std::memcpy(buf.data() + 1, token, size);
    checksum(buf.data() + 1 + size, buf.data(), 1 + size);

    return encodeBase58(
        buf.data(),
        expanded,
        buf.data() + expanded,
        bufsize - expanded,
        alphabet);
}

std::string
base58EncodeToken(TokenType type, void const* token, std::size_t size)
{
    return encodeToken(type, token, size, rippleAlphabet);
}

std::string
base58EncodeTokenBitcoin(TokenType type, void const* token, std::size_t size)
{
    return encodeToken(type, token, size, bitcoinAlphabet);
}

//------------------------------------------------------------------------------

namespace DecodeBase58Detail {
constexpr std::size_t const maybeSecret=0;
constexpr std::size_t const maybeRippleLibEncoded=1;
constexpr std::size_t const allowResize=2; // Result may be smaller than the result slice
using bitset = std::bitset<3>;

// Multi-precision unsigned integer for doing computations with at most NumBits bits.
// An exception is throw if an overflow occurs. May be optionally securely erased on
// destruction.
template <size_t NumBits>
class mpUint
{
    using T = typename boost::multiprecision::number<
        boost::multiprecision::cpp_int_backend<
            NumBits,
            NumBits,
            boost::multiprecision::unsigned_magnitude,
            boost::multiprecision::checked,
            void>>;

    T* num_{nullptr};
    alignas(alignof(T)) std::array<std::uint8_t, sizeof(T)> storage_;
    bool secureErase_{false};

public:
    mpUint(bool secureErase) : secureErase_(secureErase){};
    mpUint(mpUint const& other) = delete;
    mpUint&
    operator=(mpUint const& other) = delete;

    ~mpUint()
    {
        if (num_)
            num_->~T();
        if (secureErase_)
            beast::secure_erase(storage_.data(), storage_.size());
    }
    template <class TT>
    void
    emplace(TT&& n)
    {
        if (num_)
            num_->~T();
        num_ = new (storage_.data()) T(std::forward<TT>(n));
    }

    T& operator*()
    {
        assert(num_);
        return *num_;
    }
    T const& operator*() const
    {
        assert(num_);
        return *num_;
    }
};

// Stack buffer appropriate for decoding into. May be optionally securely erased
// on destruction.
template <class T, size_t N>
struct TempBuf : std::array<T, N>
{
private:
    bool secureErase_;

public:
    using std::array<T, N>::array;
    std::size_t numToErase = N;
    TempBuf(bool secureErase) : secureErase_{secureErase}
    {
    }
    TempBuf(TempBuf const& rhs) = default;
    ~TempBuf()
    {
        if (secureErase_)
            beast::secure_erase(this->data(), sizeof(T) * numToErase);
    }
};

}  // namespace DecodeBase58Detail

/**
   Decode a base58 number.

   This algorithm first encoding the base 58 number as a base 58^10 number. In
   this representation, all the coefficients will fit into a 64 bit number. This
   base 58^10 is then decoded into a boost multi precision integer.
*/
template <class InverseArray>
static boost::optional<std::pair<Slice, DecodeMetadata>>
decodeBase58(
    Slice in,
    MutableSlice out,
    InverseArray const& inv,
    DecodeBase58Detail::bitset const& flags)
{
    using namespace boost::multiprecision;

    // Given an input of size N, the output size is not known exactly. If the largest token
    // size is MaxDecodedTokenBytes, this function needs to be able to decode MaxDecodedTokenBytes+1
    // bytes
    constexpr size_t maxOutBytes = MaxDecodedTokenBytes+1;
    constexpr size_t maxOutBits = maxOutBytes*8;

    // Skip and count leading zeroes
    int zeroes = 0;
    while (in.size() > 0 && inv[in[0]] == 0)
    {
        ++zeroes;
        in+=1;
    }

    {
        std::size_t const maxSize = in.size() * 733 / 1000 + 1;
        if (out.size() + 1 + rippleLibEncodedSeedPrefix.size() + checksumBytes <
            maxSize - 1)  // -1 because maxSize may overestimate
            return {};
        // tighter constraint for non-ripplelib encoded data
        // ripple-lib encoded will be <1 token><2 encoding><16 data><4 checksum>==23
        if (!flags.test(DecodeBase58Detail::maybeRippleLibEncoded) &&
            out.size() + 1 + checksumBytes < maxSize - 1) // -1 because maxSize may overestimate
        {
            return {};
        }
        if (maxSize + zeroes < checksumBytes || maxSize + zeroes > maxOutBytes)
            return {};
    }

    if (out.size() > maxOutBytes)
    {
        return {};
    }

    // coefficients for the base 58^10 decoding
    std::array<std::uint64_t, 6> b5810{};
    // Durring loop: index of the last next coefficients to populate
    // After loop: number of coefficients in the b5810 array.
    int b5810i = 0;
    {
        // convert from base58 to base 58^10; All values will fit in a 64-bit
        // uint without overflow

        // pow58[i] == 58^i
        constexpr std::array<std::uint64_t, 10> pow58{1,
                                                      58,
                                                      3364,
                                                      195112,
                                                      11316496,
                                                      656356768,
                                                      38068692544,
                                                      2207984167552,
                                                      128063081718016,
                                                      7427658739644928};

        int const n = in.size();
        int i = 0;
        while (1)
        {
            auto const count = std::min(10, n - i);
            std::uint64_t s = 0;
            for (int j = 0; j < count; ++j, ++i)
            {
                auto const val = inv[in[n - i - 1]];
                if (val == -1)
                    return {};
                s += pow58[j] * val;
            }
            assert(b5810i < b5810.size());
            b5810[b5810i] = s;
            ++b5810i;
            if (i >= n)
            {
                assert(i == n);
                break;
            }
        }
    };

    try
    {
        // The following static constants of the form c58_X == 58^X
        static checked_uint128_t const c58_10{"430804206899405824"};  // 58^10
        // Only c58_50 needs to be 512, the others can be 256; However, since there are static
        // the extra space doesn't matter (and there is no extra computation)
        static checked_uint512_t const c58_20{
            "185592264682226060569122324245118976"};  // 58^20
        static checked_uint512_t const c58_30{
            "79953928393091004080532279173738402825689466587316224"};  // 58^30
        static checked_uint512_t const c58_40{
            "34444488709877454747081479018851205589883534409595458615323760395288576"};  // 58^40
        static checked_uint512_t const c58_50{
            "14838830640714294999983396511843381888381117770762056659674968992599706869653743415066624"};  // 58^50
        using mpUint128 = DecodeBase58Detail::mpUint<128>;
        using mpUintMaxOut = DecodeBase58Detail::mpUint<maxOutBits>;
        bool const secureErase = flags.test(DecodeBase58Detail::maybeSecret);
        // low result accounts for the first two base 58^10 coefficients. This is at most 2^64+2^62*58^10, or 123 bits.
        mpUint128 low_result{secureErase};
        low_result.emplace(b5810[0] + c58_10 * b5810[1]);
        // high result accounts all but the first two base 58^10 coefficients.
        mpUintMaxOut high_result{secureErase};
        switch (b5810i)
        {
            case 3:
                high_result.emplace(b5810[2] * c58_20);
                break;
            case 4:
                high_result.emplace(b5810[2] * c58_20 + b5810[3] * c58_30);
                break;
            case 5:
                high_result.emplace(b5810[2] * c58_20 + b5810[3] * c58_30 + b5810[4] * c58_40);
                break;
            case 6:
                high_result.emplace(b5810[2] * c58_20 + b5810[3] * c58_30 +
                                    b5810[4] * c58_40 + b5810[5] * c58_50);
                break;
            default:
                high_result.emplace(0);
                break;
        }
        mpUintMaxOut result{secureErase};
        result.emplace(*low_result + *high_result);
        DecodeBase58Detail::TempBuf<std::uint8_t, maxOutBytes> tmp{secureErase};
        memset(tmp.data(), 0, zeroes);
        // Account for leading zeroes by starting the write at an offset
        auto endWritten =
            boost::multiprecision::export_bits(*result, tmp.data() + zeroes, 8);
        if (zeroes && endWritten == tmp.data() + zeroes + 1 && tmp[zeroes] == 0)
        {
            // The last zero is redundant and should be removed
            --endWritten;
        }
        std::size_t const numWritten = std::distance(tmp.data(), endWritten);
        assert(numWritten <= maxOutBytes);
        tmp.numToErase = numWritten;
        if (numWritten <= checksumBytes)
            return {};
        if (numWritten > MaxDecodedTokenBytes)
            return {};

        if (out.size())
            memset(out.data(), 0, out.size());
        DecodeMetadata metadata;
        memset(metadata.encodingType.data(), 0, metadata.encodingType.size());
        memcpy(metadata.checksum.data(), tmp.data() + numWritten - checksumBytes, checksumBytes);
        metadata.tokenType = tmp[0];
        std::uint8_t const* dataStart = &tmp[1];
        std::uint8_t const* dataEnd = &tmp[0] + numWritten - checksumBytes;
        if (flags.test(DecodeBase58Detail::maybeRippleLibEncoded) &&
            static_cast<TokenType>(metadata.tokenType) == TokenType::None &&
            (dataEnd - dataStart == rippleLibEncodedSeedPrefix.size() + familySeedBytes) &&
            dataStart[0] == rippleLibEncodedSeedPrefix[0] &&
            dataStart[1] == rippleLibEncodedSeedPrefix[1])
        {
            memcpy(metadata.encodingType.data(), dataStart, metadata.encodingType.size());
            dataStart+=metadata.encodingType.size();
        }
        assert(dataStart<=dataEnd);
        size_t const dataSize = dataEnd - dataStart;
        if (dataSize > out.size())
            return {};

        if (!flags.test(DecodeBase58Detail::allowResize) &&
            dataSize != out.size())
            return {};
        memcpy(out.data(), dataStart, dataSize);
        return std::make_pair(Slice{out.data(), dataSize}, metadata);
    }
    catch (std::overflow_error const&)
    {
        return {};
    }
}

/*  Base58 decode a Ripple token

    The type and checksum are are checked
    and removed from the returned result.
*/
template <class InverseArray>
static
boost::optional<std::pair<Slice, ExtraB58Encoding>>
decodeBase58Token(
    Slice s,
    TokenType type,
    MutableSlice result,
    InverseArray const& inv,
    DecodeBase58Detail::bitset flags)
{
    // additional flags based on token type
    switch (type)
    {
        case TokenType::NodePrivate:
        case TokenType::AccountSecret:
        case TokenType::FamilyGenerator:
        case TokenType::FamilySeed:
            flags.set(DecodeBase58Detail::maybeSecret);
            break;
        default:
            break;
    }
    auto r = decodeBase58(s, result, inv, flags);
    if (!r)
        return {};
    Slice decoded;
    DecodeMetadata metadata;
    std::tie(decoded, metadata) = *r;
    ExtraB58Encoding extraB58Encoding = ExtraB58Encoding::None;

    if (type == TokenType::FamilySeed && metadata.isRippleLibEncoded())
    {
        // ripple lib encoded seed
        // ripple-lib encodes seed used to generate an Ed25519 wallet in a
        // non-standard way. While rippled never encode seeds that way, we
        // try to detect such keys to avoid user confusion.
        if (TokenType::None != static_cast<TokenType>(metadata.tokenType))
            return {};
        extraB58Encoding = ExtraB58Encoding::RippleLib;
    }
    else
    {
        if (type != static_cast<TokenType>(metadata.tokenType) ||
            metadata.encodingType[0] != 0 || metadata.encodingType[1] != 0)
            return {};
    }

    std::array<std::uint8_t, checksumBytes> const guard = [&] {
        std::array<std::uint8_t, checksumBytes> g;
        if (metadata.encodingType[0] == 0 && metadata.encodingType[1] == 0)
        {
            Slice prefix(&metadata.tokenType, sizeof(metadata.tokenType));
            checksum(g.data(), prefix, decoded);
            return g;
        }
        else
        {
            // ripple lib encoded seed
            assert(extraB58Encoding == ExtraB58Encoding::RippleLib);
            std::array<std::uint8_t, 3> prefix{metadata.tokenType,
                                               metadata.encodingType[0],
                                               metadata.encodingType[1]};
            checksum(g.data(), Slice(prefix.data(), prefix.size()), decoded);
            return g;
        }
    }();
    if (!std::equal(guard.begin(), guard.end(), metadata.checksum.begin()))
        return {};
    return std::make_pair(decoded, extraB58Encoding);
}

//------------------------------------------------------------------------------

// Maps characters to their base58 digit
class InverseAlphabet
{
private:
    std::array<int, 256> map_;

public:
    explicit InverseAlphabet(std::string const& digits)
    {
        map_.fill(-1);
        int i = 0;
        for (auto const c : digits)
            map_[static_cast<unsigned char>(c)] = i++;
    }

    int operator[](char c) const
    {
        return map_[static_cast<unsigned char>(c)];
    }
};

static InverseAlphabet rippleInverse(rippleAlphabet);

static InverseAlphabet bitcoinInverse(bitcoinAlphabet);

bool
decodeBase58Token(
    Slice s,
    TokenType type,
    MutableSlice result)
{
    DecodeBase58Detail::bitset flags;
    if (auto r = decodeBase58Token(s, type, result, rippleInverse, flags))
        return true;
    return false;
}

boost::optional<ExtraB58Encoding>
decodeBase58FamilySeed(Slice s, MutableSlice result)
{
    DecodeBase58Detail::bitset flags;
    flags.set(DecodeBase58Detail::maybeRippleLibEncoded);
    flags.set(DecodeBase58Detail::maybeSecret);
    if (auto const r = decodeBase58Token(
            s, TokenType::FamilySeed, result, rippleInverse, flags))
    {
        return r->second;
    }
    return {};
}

bool
decodeBase58TokenBitcoin(Slice s, TokenType type, MutableSlice result)
{
    DecodeBase58Detail::bitset flags;
    if (auto r = decodeBase58Token(s, type, result, bitcoinInverse, flags))
        return true;
    return false;
}

boost::optional<std::pair<Slice, DecodeMetadata>>
decodeBase58Resizable(Slice s, MutableSlice result)
{
    DecodeBase58Detail::bitset flags;
    flags.set(DecodeBase58Detail::maybeSecret);
    flags.set(DecodeBase58Detail::maybeRippleLibEncoded);
    flags.set(DecodeBase58Detail::allowResize);
    return decodeBase58(s, result, rippleInverse, flags);
}

bool DecodeMetadata::isRippleLibEncoded() const
{
    return encodingType == rippleLibEncodedSeedPrefix;
}

}  // namespace ripple
