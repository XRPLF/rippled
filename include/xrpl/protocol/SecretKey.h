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

#ifndef RIPPLE_PROTOCOL_SECRETKEY_H_INCLUDED
#define RIPPLE_PROTOCOL_SECRETKEY_H_INCLUDED

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/tokens.h>
#include <array>
#include <cstring>
#include <string>

namespace ripple {

/** A secret key. */
class SecretKey
{
private:
    std::vector<std::uint8_t> buf_; // Dynamically sized buffer for the key
    std::size_t keySize_;           // Actual size of the key (32 or 2528 bytes)

public:
    using const_iterator = std::uint8_t const*;

    SecretKey() = delete; // Default constructor is deleted
    SecretKey(Slice const& slice); // Constructor from Slice
    SecretKey(KeyType type, Slice const& slice); // Constructor with KeyType
    SecretKey(SecretKey const&) = default; // Copy constructor
    SecretKey& operator=(SecretKey const&) = default; // Copy assignment operator
    SecretKey(std::vector<std::uint8_t> const& data); // Constructor from std::vector
    ~SecretKey() = default; // Default destructor

    std::uint8_t const* data() const { return buf_.data(); }
    std::size_t size() const { return keySize_; }

    std::string to_string() const;

    const_iterator begin() const noexcept { return buf_.data(); }
    const_iterator cbegin() const noexcept { return buf_.data(); }
    const_iterator end() const noexcept { return buf_.data() + keySize_; }
    const_iterator cend() const noexcept { return buf_.data() + keySize_; }
};

inline bool
operator==(SecretKey const& lhs, SecretKey const& rhs)
{
    return lhs.size() == rhs.size() &&
        std::memcmp(lhs.data(), rhs.data(), rhs.size()) == 0;
}

inline bool
operator!=(SecretKey const& lhs, SecretKey const& rhs)
{
    return !(lhs == rhs);
}

//------------------------------------------------------------------------------

/** Parse a secret key */
template <>
std::optional<SecretKey>
parseBase58(TokenType type, std::string const& s);

inline std::string
toBase58(TokenType type, SecretKey const& sk)
{
    return encodeBase58Token(type, sk.data(), sk.size());
}

/** Create a secret key using secure random numbers. 
I have created two seperate functions for the different keytypes so both algorithms can coexist*/
// SecretKey
// randomSecretKey();

SecretKey randomSecp256k1SecretKey();

SecretKey randomDilithiumSecretKey();

/** Generate a new secret key deterministically. */
SecretKey
generateSecretKey(KeyType type, Seed const& seed);

/** Derive the public key from a secret key. */
PublicKey derivePublicKey(KeyType type, SecretKey const& sk);

/** Derive the public key from a secret key and a seed. */
PublicKey derivePublicKey(KeyType type, SecretKey const& sk, Seed const& seed);

/** Generate a key pair deterministically.

    This algorithm is specific to Ripple:

    For secp256k1 key pairs, the seed is converted
    to a Generator and used to compute the key pair
    corresponding to ordinal 0 for the generator.
*/
std::pair<PublicKey, SecretKey>
generateKeyPair(KeyType type, Seed const& seed);

/** Create a key pair using secure random numbers. */
std::pair<PublicKey, SecretKey>
randomKeyPair(KeyType type);

/** Generate a signature for a message digest.
    This can only be used with secp256k1 since Ed25519's
    security properties come, in part, from how the message
    is hashed.
*/
/** @{ */
Buffer
signDigest(PublicKey const& pk, SecretKey const& sk, uint256 const& digest);

inline Buffer
signDigest(KeyType type, SecretKey const& sk, uint256 const& digest)
{
    return signDigest(derivePublicKey(type, sk), sk, digest);
}
/** @} */

/** Generate a signature for a message.
    With secp256k1 signatures, the data is first hashed with
    SHA512-Half, and the resulting digest is signed.
*/
/** @{ */
Buffer
sign(PublicKey const& pk, SecretKey const& sk, Slice const& message);

inline Buffer
sign(KeyType type, SecretKey const& sk, Slice const& message)
{
    return sign(derivePublicKey(type, sk), sk, message);
}
/** @} */

}  // namespace ripple

#endif
