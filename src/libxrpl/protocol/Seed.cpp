/** @file
 *  Implements the Seed class and the factory/parsing functions that feed
 *  XRPL's deterministic key-derivation pipeline.
 *
 *  A seed is the 128-bit root secret from which both secp256k1 and ed25519
 *  key pairs are derived via @c generateSecretKey / @c generateKeyPair.
 *  Every XRPL account or validator node traces back to one of these 16-byte
 *  values, making this the most security-sensitive path in the key-management
 *  subsystem.  Secure erasure is applied to every transient buffer that
 *  touches raw seed material.
 */
#include <xrpl/protocol/Seed.h>

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
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/tokens.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>

namespace xrpl {

/** Securely wipe the seed buffer on destruction.
 *
 *  Uses @c secureErase (backed by @c OPENSSL_cleanse) rather than a plain
 *  @c memset or zeroing loop.  The OpenSSL variant uses strategies specifically
 *  designed to resist dead-store elimination by optimizing compilers, ensuring
 *  the key material is actually overwritten before the storage is reclaimed.
 */
Seed::~Seed()
{
    secureErase(buf_.data(), buf_.size());
}

/** Construct from a raw byte span.
 *
 *  @param slice A non-owning view of exactly 16 bytes of seed material.
 *  @throw LogicError if @p slice is not exactly 16 bytes.  A size mismatch
 *      here is a programmer error — callers must validate size before
 *      constructing a Seed from an already-typed value.
 */
Seed::Seed(Slice const& slice)
{
    if (slice.size() != buf_.size())
        logicError("Seed::Seed: invalid size");
    std::memcpy(buf_.data(), slice.data(), buf_.size());
}

/** Construct from a 128-bit integer.
 *
 *  @param seed A @c uint128 whose raw bytes are copied into the seed buffer.
 *  @throw LogicError if the size of @p seed is not exactly 16 bytes.
 */
Seed::Seed(uint128 const& seed)
{
    if (seed.size() != buf_.size())
        logicError("Seed::Seed: invalid size");
    std::memcpy(buf_.data(), seed.data(), buf_.size());
}

//------------------------------------------------------------------------------

/** Generate a seed from 16 bytes of cryptographically secure random data.
 *
 *  The raw entropy is written into a stack-allocated buffer, used to
 *  construct the @c Seed, and then immediately wiped with @c secureErase
 *  before the function returns, preventing the raw bytes from lingering in
 *  the stack frame after the call.
 *
 *  @return A freshly generated, cryptographically random @c Seed.
 */
Seed
randomSeed()
{
    std::array<std::uint8_t, 16> buffer{};
    beast::rngfill(buffer.data(), buffer.size(), cryptoPrng());
    Seed const seed(makeSlice(buffer));
    secureErase(buffer.data(), buffer.size());
    return seed;
}

/** Derive a seed deterministically from a passphrase.
 *
 *  Computes the SHA-512 half (first 256 bits of SHA-512) of @p passPhrase
 *  and uses the first 16 bytes of the resulting digest as the seed.  The
 *  passphrase bytes are hashed without normalization, null terminator, or
 *  length prefix.
 *
 *  @param passPhrase The passphrase to hash.  Any non-empty string is
 *      accepted; no format detection is performed.
 *  @return The deterministically derived @c Seed.
 *
 *  @note Uses @c sha512_half_hasher_s (the secure variant) so that the
 *      internal SHA-512 state is zeroed in the hasher's destructor,
 *      preventing the passphrase's hash state from lingering in stack memory.
 *      The passphrase @c "masterpassphrase" always produces the seed encoded
 *      as @c "snoPBrXtMeMyMHUVTgbuqAfg1SUTb".
 */
Seed
generateSeed(std::string const& passPhrase)
{
    sha512_half_hasher_s h;
    h(passPhrase.data(), passPhrase.size());
    auto const digest = sha512_half_hasher::result_type(h);
    return Seed({digest.data(), 16});
}

/** Decode a Base58Check-encoded seed string tagged with @c TokenType::FamilySeed.
 *
 *  @param s A Base58Check string in XRPL's @c sXXXX family-seed format.
 *  @return The decoded @c Seed on success, or @c std::nullopt if @p s fails
 *      Base58Check decoding, carries the wrong token type, or decodes to a
 *      payload that is not exactly 16 bytes.  A size mismatch here is treated
 *      as an input error (returning @c std::nullopt) rather than a logic error,
 *      because the string originates from external input.
 */
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

/** Attempt to parse a string as a seed using cascading format detection.
 *
 *  Tries each representation in order and returns the first match:
 *  1. **Rejection guard** — returns @c std::nullopt immediately if @p str
 *     decodes as an @c AccountID, node/account public key, or node/account
 *     secret key.  This prevents key-type confusion: without this guard, a
 *     valid public-key string could be silently reparsed as a seed via the
 *     passphrase fallback below.
 *  2. **Hex** — attempts to parse as a 128-bit hexadecimal value.
 *  3. **Base58** — attempts @c parseBase58<Seed> for the standard @c sXXXX
 *     family-seed format.
 *  4. **RFC1751** (when @p rfc1751 is @c true) — decodes a mnemonic English
 *     word sequence; the decoded bytes are reversed to match the reversal
 *     applied by @c seedAs1751 during encoding.
 *  5. **Passphrase fallback** — any non-empty string that passes none of the
 *     above is hashed via @c generateSeed.
 *
 *  @param str The string to parse.
 *  @param rfc1751 When @c true, RFC1751 mnemonic decoding is attempted.
 *      Defaults to @c true but is deprecated; prefer @c parseBase58<Seed>
 *      for strict parsing.
 *  @return The parsed @c Seed, or @c std::nullopt if @p str is empty or
 *      decodes as a recognized key type other than a seed.
 *
 *  @note The passphrase fallback means this function never returns
 *      @c std::nullopt for a non-empty string that is not another key type.
 *      Callers requiring strict validation should use @c parseBase58<Seed>.
 */
std::optional<Seed>
parseGenericSeed(std::string const& str, bool rfc1751)
{
    if (str.empty())
        return std::nullopt;

    if (parseBase58<AccountID>(str) || parseBase58<PublicKey>(TokenType::NodePublic, str) ||
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
            return Seed{uint128::fromRaw(blob)};
        }
    }

    return generateSeed(str);
}

/** Encode a seed as an RFC1751 mnemonic word sequence.
 *
 *  The 16 seed bytes are reversed before being passed to
 *  @c RFC1751::getEnglishFromKey.  This reversal is symmetric with the
 *  reversal applied by @c parseGenericSeed when decoding an RFC1751 string,
 *  and reflects a historical endianness convention in XRPL's adoption of the
 *  RFC.
 *
 *  @param seed The seed to encode.
 *  @return The RFC1751 mnemonic string for @p seed.
 */
std::string
seedAs1751(Seed const& seed)
{
    std::string key;

    std::reverse_copy(seed.data(), seed.data() + 16, std::back_inserter(key));

    std::string encodedKey;
    RFC1751::getEnglishFromKey(encodedKey, key);
    return encodedKey;
}

}  // namespace xrpl
