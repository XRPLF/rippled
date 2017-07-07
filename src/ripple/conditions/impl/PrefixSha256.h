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

#ifndef RIPPLE_CONDITIONS_PREFIX_SHA256_H
#define RIPPLE_CONDITIONS_PREFIX_SHA256_H

#include <ripple/basics/Buffer.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/Der.h>

#include <boost/container/small_vector.hpp>

#include <memory>

namespace ripple {
namespace cryptoconditions {

/** Fulfillment for a prefix cryptocondition A prefix adds a specified prefix to
    the cryptocondition's message, and sends that new message to the specified
    sub-fulfillment.
 */
class PrefixSha256 final : public Fulfillment
{
    /// prefix to add to the subcondition's message
    boost::container::small_vector<std::uint8_t, 32> prefix_;
    std::uint64_t maxMessageLength_ = 0;
    /// subfulfillment used to verify the newly created message
    std::unique_ptr<Fulfillment> subfulfillment_;

    void
    encodeFingerprint(der::Encoder& encoder) const override;

    bool
    checkEqualForTesting(Fulfillment const& rhs) const override;

    bool
    validationDependsOnMessage() const override;

public:
    PrefixSha256(der::Constructor const&) noexcept {};

    PrefixSha256() = delete;

    PrefixSha256(
        Slice prefix,
        std::uint64_t maxLength,
        std::unique_ptr<Fulfillment> subfulfillment);

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache)
    {
        f(std::tie(prefix_, maxMessageLength_, subfulfillment_));
    }

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache) const
    {
        const_cast<PrefixSha256*>(this)->withTuple(std::forward<F>(f), traitsCache);
    }

    Type
    type() const override
    {
        return Type::prefixSha256;
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
