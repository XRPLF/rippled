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

#ifndef RIPPLE_PROTOCOL_ANYSECRETKEY_H_INCLUDED
#define RIPPLE_PROTOCOL_ANYSECRETKEY_H_INCLUDED

#include <ripple/basics/Buffer.h>
#include <ripple/protocol/AnyPublicKey.h>
#include <ripple/protocol/HashPrefix.h>
#include <boost/utility/base_from_member.hpp>
#include <cstdint>
#include <memory>
#include <utility>

namespace ripple {

/** Variant container for secret key, with ownership. */
class AnySecretKey
{
private:
    Buffer p_;
    KeyType type_;

public:
    AnySecretKey() = delete;
    AnySecretKey (AnySecretKey const&) = delete;
    AnySecretKey& operator= (AnySecretKey const&) = delete;

    /** Destroy the key.
        The memory area is secure erased.
    */
    ~AnySecretKey();

    AnySecretKey (AnySecretKey&& other);

    AnySecretKey& operator= (AnySecretKey&& other);

    AnySecretKey (KeyType type,
        void const* data, std::size_t size);

    /** Returns the type of secret key. */
    KeyType
    type() const noexcept
    {
        return type_;
    }

    /** Returns the corresponding public key. */
    AnyPublicKey
    publicKey() const;

    /** Create a signature for the given message. */
    Buffer
    sign (void const* msg, std::size_t msg_len) const;

    /** Securely generate a new ed25519 secret key. */
    static
    AnySecretKey
    make_ed25519();

    /** Securely generate a new secp256k1 key pair. */
    static
    std::pair<AnySecretKey, AnyPublicKey>
    make_secp256k1_pair();
};

} // ripple

#endif
