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

#include <ripple/protocol/Seed.h>
#include <ripple/basics/Buffer.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/basics/contract.h>
#include <ripple/crypto/KeyType.h>
#include <ripple/crypto/RFC1751.h>
#include <ripple/crypto/csprng.h>
#include <ripple/beast/crypto/secure_erase.h>
#include <ripple/beast/utility/rngfill.h>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace ripple {

Seed::~Seed()
{
    beast::secure_erase(buf_.data(), buf_.size());
}

Seed::Seed(Slice const& slice, boost::optional<KeyType> const& keyType)
    : keyType_{keyType}
{
    if (slice.size() != buf_.size())
        LogicError("Seed::Seed: invalid size");
    std::memcpy(buf_.data(),
        slice.data(), buf_.size());
}

Seed::Seed(uint128 const& seed, boost::optional<KeyType> const& keyType)
    : keyType_{keyType}
{
    if (seed.size() != buf_.size())
        LogicError("Seed::Seed: invalid size");
    std::memcpy(buf_.data(),
        seed.data(), buf_.size());
}

//------------------------------------------------------------------------------

// Stack buffer whose memory is securely zeroed in the destructor
template <class T, size_t N>
struct zero_after_use_buf : std::array<T, N>
{
    using std::array<T, N>::array;
    zero_after_use_buf() = default;
    zero_after_use_buf(zero_after_use_buf const& rhs) = default;
    ~zero_after_use_buf()
    {
        beast::secure_erase(this->data(), sizeof(T) * this->size());
    }
};
struct zero_after_use_uint256 : uint256
{
    using uint256::uint256;
    zero_after_use_uint256() = default;
    zero_after_use_uint256(zero_after_use_uint256 const& other) : uint256{other}
    {
    }
    zero_after_use_uint256(uint256 const& other) : uint256{other}
    {
    }
    ~zero_after_use_uint256()
    {
        beast::secure_erase(this->data(), uint256::bytes);
    }
};

Seed
randomSeed()
{
    zero_after_use_buf <std::uint8_t, 16> buffer;
    beast::rngfill (
        buffer.data(),
        buffer.size(),
        crypto_prng());
    Seed seed (makeSlice (buffer));
    beast::secure_erase(buffer.data(), buffer.size());
    return seed;
}

Seed
generateSeed (std::string const& passPhrase)
{
    sha512_half_hasher_s h;
    h(passPhrase.data(), passPhrase.size());
    zero_after_use_uint256 digest = sha512_half_hasher::result_type(h);
    return Seed({ digest.data(), 16 });
}

template <>
boost::optional<Seed>
parseBase58 (std::string const& s)
{
    zero_after_use_buf<std::uint8_t, 16> result;
    auto resultSlice = makeMutableSlice(result);
    boost::optional<KeyType> keyType;
    if (boost::optional<ExtraB58Encoding> r =
        decodeBase58FamilySeed(makeSlice(s), resultSlice))
    {
        if (*r == ExtraB58Encoding::RippleLib)
            keyType = KeyType::ed25519;
        return Seed(resultSlice, keyType);
    }
    return {};
}

boost::optional<Seed>
parseGenericSeed (std::string const& str)
{
    if (str.empty())
        return boost::none;

    // large enough to hold either a public key, account, secret key, or seed
    zero_after_use_buf<std::uint8_t, 33> buffer;

    boost::optional<std::pair<Slice, DecodeMetadata>> rawDecode = decodeBase58(
        makeSlice(str), makeMutableSlice(buffer), /*allowResize*/ true);

    Slice decodedSlice;
    DecodeMetadata metadata;
    if (rawDecode)
    {
        std::tie(decodedSlice, metadata) = *rawDecode;
        switch (static_cast<TokenType>(metadata.tokenType))
        {
            case TokenType::AccountID:
            case TokenType::NodePublic:
            case TokenType::AccountPublic:
            case TokenType::NodePrivate:
            case TokenType::AccountSecret:
                return boost::none;
            default:
                break;
        }
    }

    {
        uint128 seed;

        if (seed.SetHexExact(str))
            return Seed{Slice{seed.data(), seed.size()}};
    }

    if (rawDecode && decodedSlice.size() == 16)
    {
        if (static_cast<TokenType>(metadata.tokenType) == TokenType::None &&
            metadata.isRippleLibEncoded())
        {
            return Seed{decodedSlice, KeyType::ed25519};
        }
        if (static_cast<TokenType>(metadata.tokenType) == TokenType::FamilySeed &&
            !metadata.isRippleLibEncoded())
        {
            return Seed{decodedSlice};
        }
    }

    {
        std::string key;
        if (RFC1751::getKeyFromEnglish(key, str) == 1)
        {
            Blob const blob(key.rbegin(), key.rend());
            return Seed{uint128{blob}};
        }
    }

    return generateSeed(str);
}

std::string
seedAs1751 (Seed const& seed)
{
    std::string key;

    std::reverse_copy (
        seed.data(),
        seed.data() + 16,
        std::back_inserter(key));

    std::string encodedKey;
    RFC1751::getEnglishFromKey (encodedKey, key);
    return encodedKey;
}

}
