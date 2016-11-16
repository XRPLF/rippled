//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/Ed25519.h>
#include <ripple/conditions/impl/base64.h>
#include <ripple/basics/contract.h>
#include <ed25519-donna/ed25519.h>

namespace ripple {
namespace cryptoconditions {

Ed25519::Ed25519 (
    SecretKey const& secretKey,
    PublicKey const& publicKey,
    Slice message)
{
    if (publicKeyType (publicKey) != KeyType::ed25519)
        LogicError ("An Ed25519 public key is required.");

    // When PublicKey wraps an Ed25519 key it prefixes
    // the key itself with a 0xED byte. We carefully
    // skip that byte.
    std::memcpy (
        payload_.data(),
        publicKey.data() + 1,
        publicKey.size() - 1);

    // Now sign:
    ed25519_sign (
        message.data(),
        message.size(),
        secretKey.data(),
        payload_.data(),
        payload_.data() + pubkey_size_);
}

/** Create a fulfillment given a secret key and the message */
Ed25519::Ed25519 (
    SecretKey const& secretKey,
    Slice message)
{
    // First derive the public key, and place it in the
    // payload:
    ed25519_publickey (
        secretKey.data(),
        payload_.data());

    ed25519_sign (
        message.data(),
        message.size(),
        secretKey.data(),
        payload_.data(),
        payload_.data() + pubkey_size_);
}

Condition
Ed25519::condition() const
{
    Condition cc;
    cc.type = type();
    cc.featureBitmask = features();
    cc.maxFulfillmentLength = payloadSize();

    std::memcpy (
        cc.fingerprint.data(),
        payload_.data(),
        pubkey_size_);

    return cc;
}

bool
Ed25519::validate (Slice data) const
{
    return ed25519_sign_open (
        data.data(),
        data.size(),
        payload_.data(),
        payload_.data() + pubkey_size_) == 0;
}

bool
Ed25519::parsePayload (Slice s)
{
    // The payload consists of 96 consecutive bytes:
    // The public key is the first 32 and the
    // remaining 64 bytes are the signature.
    if (s.size() != sizeof(payload_))
        return false;

    std::memcpy (payload_.data(), s.data(), s.size());
    return true;
}

}

}
