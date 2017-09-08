//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/Der.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>

#include <array>
#include <cstdint>

namespace ripple {
namespace cryptoconditions {

/** Fulfillment for an Ed25519 cryptocondition.

    An Ed25519 condition specifies an Ed25519 public key. The fulfillment
    contains a signature of cryptocondition message.
 */
class Ed25519 final : public Fulfillment
{
    static std::size_t constexpr signature_size_ = 64;
    static std::size_t constexpr pubkey_size_ = 32;

    std::array<std::uint8_t, pubkey_size_> publicKey_;
    std::array<std::uint8_t, signature_size_> signature_;

    void
    encodeFingerprint(der::Encoder& encoder) const override;

    bool
    checkEqualForTesting(Fulfillment const& rhs) const override;

    bool
    validationDependsOnMessage() const override;

public:
    Ed25519(der::Constructor const&) noexcept {};

    Ed25519() = delete;

    explicit
    Ed25519(
        std::array<std::uint8_t, pubkey_size_> const& publicKey,
        std::array<std::uint8_t, signature_size_> const& signature)
        : publicKey_(publicKey), signature_(signature)
    {
    }

    template <class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache)
    {
        f(std::tie(publicKey_, signature_));
    }

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache) const
    {
        f(std::tie(publicKey_, signature_));
    }

    Type
    type() const override
    {
        return Type::ed25519Sha256;
    }

    std::array<std::uint8_t, 32>
    fingerprint(std::error_code& ec) const override
    {
        return Fulfillment::fingerprint(ec);
    }

    bool
    validate(Slice data) const override;

    std::uint32_t
    cost() const override
    {
        // see crypto-conditions spec:
        // https://tools.ietf.org/html/draft-thomas-crypto-conditions-02#page-27
        return 131072;
    }

    std::bitset<5>
    subtypes() const override
    {
        return std::bitset<5>{};
    }

    void
    encode(der::Encoder& encoder) const override;

    void
    decode(der::Decoder& decoder) override;

    std::uint64_t
    derEncodedLength(
        boost::optional<der::GroupType> const& parentGroupType,
        der::TagMode encoderTagMode,
        der::TraitsCache& traitsCache) const override;

    int
    compare(Fulfillment const& rhs, der::TraitsCache& traitsCache) const override;
};
}
}

#endif
