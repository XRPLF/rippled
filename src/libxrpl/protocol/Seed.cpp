#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/RFC1751.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/crypto/secure_erase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>

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
