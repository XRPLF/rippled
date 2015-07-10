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

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <ripple/crypto/KeyType.h> // move to protocol/
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/tokens.h>
#include <array>

namespace ripple {

/** Seeds are used to generate deterministic secret keys. */
class Seed
{
private:
    std::array<uint8_t, 16> buf_;

public:
    Seed() = default;
    Seed (Seed const&) = default;
    Seed& operator= (Seed const&) = default;

    /** Destroy the seed.

        The buffer will first be securely erased.
    */
    ~Seed();

    Seed (Slice const& slice);

    std::uint8_t const*
    data() const
    {
        return buf_.data();
    }

    std::size_t
    size() const
    {
        return buf_.size();
    }
};

//------------------------------------------------------------------------------

/** A secret key. */
class SecretKey
{
private:
    std::uint8_t buf_[32];

public:
    SecretKey() = default;
    SecretKey (SecretKey const&) = default;
    SecretKey& operator= (SecretKey const&) = default;

    ~SecretKey();

    SecretKey (Slice const& slice);

    std::uint8_t const*
    data() const
    {
        return buf_;
    }

    std::size_t
    size() const
    {
        return sizeof(buf_);
    }
};

//------------------------------------------------------------------------------

/** Create a seed using secure random numbers. */
Seed
randomSeed();

/** Generate a seed deterministically.

    The algorithm is specific to Ripple:

        The seed is calculated as the first 128 bits
        of the SHA512-Half of the string text excluding
        any terminating null.

    @note Unlike createSeedGeneric, this does not
          attempt to interpret the string as hex
          or other formats.
*/
Seed
generateSeed (std::string const& passPhrase);

/** Create a secret key using secure random numbers. */
SecretKey
randomSecretKey();

/** Generate a new secret key deterministically. */
SecretKey
generateSecretKey (Seed const& seed);

/** Derive the public key from a secret key. */
PublicKey
derivePublicKey (KeyType type, SecretKey const& sk);

/** Generate a key pair deterministically.

    This algorithm is specific to Ripple:

    For secp256k1 key pairs, the seed is converted
    to a Generator and used to compute the key pair
    corresponding to ordinal 0 for the generator.
*/
std::pair<PublicKey, SecretKey>
generateKeyPair (KeyType type, Seed const& seed);

/** Create a key pair using secure random numbers. */
std::pair<PublicKey, SecretKey>
randomKeyPair (KeyType type);

/** Generate a signature for a message.

    The algorithm is specific to Ripple:
        secp256k1 signatures are computed
        on the SHA512-Half of the message.
*/
/** @{ */
Buffer
sign (PublicKey const& pk,
    SecretKey const& sk, Slice const& message);

inline
Buffer
sign (KeyType type, SecretKey const& sk,
    Slice const& message)
{
    return sign (derivePublicKey(type, sk),
        sk, message);
}
/** @} */

} // ripple

#endif
