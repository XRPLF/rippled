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

#ifndef RIPPLE_CONDITIONS_ED25519_H
#define RIPPLE_CONDITIONS_ED25519_H

#include <ripple/conditions/condition.h>
#include <ripple/conditions/fulfillment.h>
#include <ripple/conditions/impl/base64.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ed25519-donna/ed25519.h>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <iterator>

namespace ripple {
namespace cryptoconditions {

struct ed25519_t
    : public fulfillment_t
{
    static std::size_t constexpr signature_size_ = 64;
    static std::size_t constexpr pubkey_size_ = 32;

    std::array<std::uint8_t,
        pubkey_size_ + signature_size_> payload_;

public:
    ed25519_t () = default;

    /** Create a fulfillment */
    ed25519_t (std::vector<std::uint8_t> const& payload)
    {
        if (payload.size() != payload_.size())
            throw std::length_error ("Invalid Ed25519 fulfillment length");

        std::copy_n (
            payload.data(),
            payload.size(),
            payload_.data());
    }

    /** Create a fulfillment given a keypair and the message */
    ed25519_t (
        SecretKey const& secretKey,
        PublicKey const& publicKey,
        Slice const& message)
    {
        if (publicKeyType (publicKey) != KeyType::ed25519)
            LogicError ("An Ed25519 public key is required.");

        // When PublicKey wraps an Ed25519 key it prefixes
        // the key itself with a 0xED byte. We carefully
        // skip that byte.
        std::copy_n (
            publicKey.data() + 1,
            publicKey.size() - 1,
            payload_.data());

        // Now sign:
        ed25519_sign (
            message.data(),
            message.size(),
            secretKey.data(),
            payload_.data(),
            payload_.data() + pubkey_size_);
    }

    /** Create a fulfillment given a secret key and the message */
    ed25519_t (
        SecretKey const& secretKey,
        Slice const& message)
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

    condition_t
    condition() const override
    {
        condition_t cc;
        cc.type = type();
        cc.requires = requires();
        cc.fulfillment_length = payload().size();

        std::copy_n (
            payload_.data(),
            pubkey_size_,
            cc.fingerprint.data());

        return cc;
    }

    std::uint16_t
    type () const override
    {
        return condition_ed25519;
    }

    std::uint32_t
    requires () const override
    {
        return feature_ed25519;
    }

    Buffer
    payload() const override
    {
        return { payload_.data(), payload_.size() };
    }

    bool
    validate (Slice const& data) const override
    {
        return ed25519_sign_open (
            data.data(),
            data.size(),
            payload_.data(),
            payload_.data() + pubkey_size_) == 0;
    }
};


}

}

#endif
