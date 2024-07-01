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

#include <ripple/basics/Buffer.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/utility/rngfill.h>
#include <ripple/crypto/RFC1751.h>
#include <ripple/crypto/csprng.h>
#include <ripple/crypto/secure_erase.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Seed.h>
#include <ripple/protocol/digest.h>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace ripple {

Seed::~Seed()
{
    secure_erase(buf_.data(), buf_.size());
}

Seed::Seed(Slice const& slice)
{
    if (slice.size() != buf_.size())
        LogicError("Seed::Seed: invalid size");
    std::memcpy(buf_.data(), slice.data(), buf_.size());
}

Seed::Seed(uint128 const& seed)
{
    if (seed.size() != buf_.size())
        LogicError("Seed::Seed: invalid size");
    std::memcpy(buf_.data(), seed.data(), buf_.size());
}

//------------------------------------------------------------------------------

Seed
randomSeed()
{
    std::array<std::uint8_t, 16> buffer;
    beast::rngfill(buffer.data(), buffer.size(), crypto_prng());
    Seed seed(makeSlice(buffer));
    secure_erase(buffer.data(), buffer.size());
    return seed;
}

Seed
generateSeed(std::string const& passPhrase)
{
    sha512_half_hasher_s h;
    h(passPhrase.data(), passPhrase.size());
    auto const digest = sha512_half_hasher::result_type(h);
    return Seed({digest.data(), 16});
}

template <>
std::optional<Seed>
parseBase58(std::string const& s)
{
    auto const result = decodeBase58Token(s, TokenType::FamilySeed);
    if (result.empty())
        return std::nullopt;
    if (result.size() != 16)
        return std::nullopt;
    return Seed(makeSlice(result));
}

std::optional<Seed>
parseGenericSeed(std::string const& str, bool rfc1751)
{
    if (str.empty())
        return std::nullopt;

    if (parseBase58<AccountID>(str) ||
        parseBase58<PublicKey>(TokenType::NodePublic, str) ||
        parseBase58<PublicKey>(TokenType::AccountPublic, str) ||
        parseBase58<SecretKey>(TokenType::NodePrivate, str) ||
        parseBase58<SecretKey>(TokenType::AccountSecret, str))
    {
        return std::nullopt;
    }

    {
        uint128 seed;

        if (seed.parseHex(str))
            return Seed{Slice(seed.data(), seed.size())};
    }

    if (auto seed = parseBase58<Seed>(str))
        return seed;

    if (rfc1751)
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
seedAs1751(Seed const& seed)
{
    std::string key;

    std::reverse_copy(seed.data(), seed.data() + 16, std::back_inserter(key));

    std::string encodedKey;
    RFC1751::getEnglishFromKey(encodedKey, key);
    return encodedKey;
}

}  // namespace ripple
