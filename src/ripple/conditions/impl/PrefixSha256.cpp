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
#include <ripple/conditions/impl/Der.h>
#include <ripple/conditions/impl/PrefixSha256.h>

#include <limits>
#include <vector>

namespace ripple {
namespace cryptoconditions {

PrefixSha256::PrefixSha256(
    Slice prefix,
    std::uint64_t maxLength,
    std::unique_ptr<Fulfillment> subfulfillment)
    : maxMessageLength_(maxLength)
    , subfulfillment_(std::move(subfulfillment))
{
    prefix_.resize(prefix.size());
    std::copy(prefix.data(), prefix.data() + prefix.size(), prefix_.data());
}

std::array<std::uint8_t, 32>
PrefixSha256::fingerprint(std::error_code& ec) const
{
    if (!subfulfillment_)
    {
        assert(0);
        ec = der::Error::logicError;
        return {};
    };

    return Fulfillment::fingerprint(ec);
}

void
PrefixSha256::encodeFingerprint(der::Encoder& encoder) const
{
    if (!subfulfillment_)
    {
        assert(0);
        encoder.ec_ = der::Error::logicError;
        return;
    };

    auto const cond = subfulfillment_->condition(encoder.ec_);
    if (encoder.ec_)
        return;
    encoder << std::tie(prefix_, maxMessageLength_, cond);
}

bool
PrefixSha256::validate(Slice data) const
{
    if (data.size() > maxMessageLength_)
        return false;

    if (!subfulfillment_)
    {
        assert(0);
        return false;
    }
    boost::container::small_vector<std::uint8_t, 32> appendedData(prefix_.size() + data.size());
    std::copy(
        prefix_.data(), prefix_.data() + prefix_.size(), appendedData.data());
    std::copy(
        data.data(),
        data.data() + data.size(),
        appendedData.data() + prefix_.size());
    return subfulfillment_->validate(makeSlice(appendedData));
}

std::uint32_t
PrefixSha256::cost() const
{
    if (!subfulfillment_)
    {
        assert(0);
        return std::numeric_limits<std::uint32_t>::max();
    };

    return prefix_.size() + maxMessageLength_ + subfulfillment_->cost() + 1024;
}

std::bitset<5>
PrefixSha256::subtypes() const
{
    if (subfulfillment_)
    {
        auto result = subfulfillment_->selfAndSubtypes();
        result[static_cast<int>(type())] = 0;
        return result;
    }
    return std::bitset<5>{};
}

std::uint64_t
PrefixSha256::derEncodedLength(
    boost::optional<der::GroupType> const& parentGroupType,
    der::TagMode encoderTagMode,
    der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleEncodedLengthHelper(
        *this, parentGroupType, encoderTagMode, traitsCache);
}

void
PrefixSha256::encode(cryptoconditions::der::Encoder& encoder) const
{
    cryptoconditions::der::withTupleEncodeHelper(*this, encoder);
}

void
PrefixSha256::decode(cryptoconditions::der::Decoder& decoder)
{
    cryptoconditions::der::withTupleDecodeHelper(*this, decoder);
}

bool
PrefixSha256::checkEqualForTesting(Fulfillment const& rhs) const
{
    auto c = dynamic_cast<PrefixSha256 const*>(&rhs);
    if (!c)
        return false;
    if (!(c->prefix_ == prefix_ && c->maxMessageLength_ == maxMessageLength_ &&
          bool(c->subfulfillment_) == bool(subfulfillment_)))
        return false;
    if (subfulfillment_ && !c->subfulfillment_->checkEqualForTesting(*subfulfillment_))
        return false;
    return true;
}

int
PrefixSha256::compare(Fulfillment const& rhs, der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleCompareHelper(*this, rhs, traitsCache);
}

bool
PrefixSha256::validationDependsOnMessage() const
{
    if (!subfulfillment_)
        return false;
    // Note: this isn't quite true: Since the maxMessageLength_ is enforced,
    // PrefixSha256 always depends on the message
    return subfulfillment_->validationDependsOnMessage();
}

}
}
