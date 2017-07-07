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

#ifndef RIPPLE_CONDITIONS_RSA_SHA256_H
#define RIPPLE_CONDITIONS_RSA_SHA256_H

#include <ripple/basics/Buffer.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/Der.h>

namespace ripple {
namespace cryptoconditions {

/** Fulfillment for a RsaSha256 cryptocondition.

    A RsaSha256 condition specifies an RsaSha256 public key (the modulus). The
    fulfillment contains a signature of cryptocondition message.
*/
class RsaSha256 final : public Fulfillment
{
    boost::container::small_vector<std::uint8_t, 256> modulus_;
    boost::container::small_vector<std::uint8_t, 256> signature_;

    void
    encodeFingerprint(der::Encoder& encoder) const override;

    bool
    checkEqualForTesting(Fulfillment const& rhs) const override;

    bool
    validationDependsOnMessage() const override;

public:
    RsaSha256(der::Constructor const&) noexcept {};

    RsaSha256() = delete;

    template <std::size_t N, std::size_t M>
    RsaSha256(
        boost::container::small_vector<std::uint8_t, N> m,
        boost::container::small_vector<std::uint8_t, M> s)
        : modulus_{std::move(m)}, signature_{std::move(s)}
    {
    }

    RsaSha256(Slice m, Slice s);
    RsaSha256(Buffer const& m, Buffer const& s)
        : RsaSha256{Slice(m), Slice(s)}
    {
    }

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache)
    {
        f(std::tie(modulus_, signature_));
    }

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache) const
    {
        const_cast<RsaSha256*>(this)->withTuple(
            std::forward<F>(f), traitsCache);
    }

    Type
    type() const override
    {
        return Type::rsaSha256;
    }

    std::array<std::uint8_t, 32>
    fingerprint(std::error_code& ec) const override;

    bool
    validate(Slice data) const override;

    std::uint32_t
    cost() const override;

    std::bitset<5>
    subtypes() const override;

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
