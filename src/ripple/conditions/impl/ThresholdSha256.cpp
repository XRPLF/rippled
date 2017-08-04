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

#include <ripple/conditions/impl/Der.h>
#include <ripple/conditions/impl/ThresholdSha256.h>

#include <boost/dynamic_bitset.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ripple {
namespace cryptoconditions {

ThresholdSha256::ThresholdSha256(
    std::vector<std::unique_ptr<Fulfillment>> subfulfillments,
    std::vector<Condition> subconditions)
{
    subfulfillments_.reserve(subfulfillments.size());
    subconditions_.reserve(subconditions.size());
    for(auto&& f : subfulfillments)
        subfulfillments_.push_back(std::move(f));
    for(auto&& c : subconditions)
        subconditions_.push_back(std::move(c));
}

std::array<std::uint8_t, 32>
ThresholdSha256::fingerprint(std::error_code& ec) const
{
    return Fulfillment::fingerprint(ec);
}

void
ThresholdSha256::encodeFingerprint(der::Encoder& encoder) const
{
    std::uint16_t const threshold =
        static_cast<std::uint16_t>(subfulfillments_.size());
    if (!cachedAllConditions)
    {
        cachedAllConditions.emplace();
        cachedAllConditions->reserve(
            subfulfillments_.size() + subconditions_.size());
        for (auto const& f : subfulfillments_)
        {
            cachedAllConditions->push_back(f->condition(encoder.ec_));
            if (encoder.ec_)
                return;
        }
        for (auto const& c : subconditions_)
            cachedAllConditions->push_back(c);
    }
    auto conditionsSet =
        der::make_set(*cachedAllConditions, encoder.traitsCache_);
    encoder << std::tie(threshold, conditionsSet);
}

bool
ThresholdSha256::validate(Slice data) const
{
    for (auto const& f : subfulfillments_)
        if (!f || !f->validate(data))
            return false;
    return true;
}

std::uint32_t
ThresholdSha256::cost() const
{
    boost::container::small_vector<std::uint32_t, 4> subcosts;
    subcosts.reserve(subconditions_.size() + subfulfillments_.size());
    for (auto const& c : subconditions_)
        subcosts.push_back(c.cost);
    for (auto const& f : subfulfillments_)
        subcosts.push_back(f->cost());
    size_t const threshold = subfulfillments_.size();
    std::nth_element(
        subcosts.begin(), subcosts.end() - threshold, subcosts.end());
    std::uint64_t const result =
        std::accumulate(subcosts.end() - threshold, subcosts.end(), 0ull) +
        1024 * (subfulfillments_.size() + subconditions_.size());
    if (result > std::numeric_limits<std::uint32_t>::max())
        return std::numeric_limits<std::uint32_t>::max();
    return static_cast<uint32_t>(result);
}

std::bitset<5>
ThresholdSha256::subtypes() const
{
    std::bitset<5> result;
    for (auto const& s : subconditions_)
        result |= s.selfAndSubtypes();
    for (auto const& s : subfulfillments_)
        result |= s->selfAndSubtypes();
    result[static_cast<int>(type())] = 0;
    return result;
}

std::uint64_t
ThresholdSha256::derEncodedLength(
    boost::optional<der::GroupType> const& parentGroupType,
    der::TagMode encoderTagMode,
    der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleEncodedLengthHelper(
        *this, parentGroupType, encoderTagMode, traitsCache);
}

void
ThresholdSha256::encode(cryptoconditions::der::Encoder& encoder) const
{
    cryptoconditions::der::withTupleEncodeHelper(*this, encoder);
}

void
ThresholdSha256::decode(cryptoconditions::der::Decoder& decoder)
{
    cryptoconditions::der::withTupleDecodeHelper(*this, decoder);
}

bool
ThresholdSha256::checkEqualForTesting(Fulfillment const& rhs) const
{
    auto c = dynamic_cast<ThresholdSha256 const*>(&rhs);
    if (!c)
        return false;
    if (c->subfulfillments_.size() != subfulfillments_.size() ||
        c->subconditions_.size() != subconditions_.size())
        return false;

    {
        boost::dynamic_bitset<> foundEqual(subfulfillments_.size());
        for (size_t i = 0; i < subfulfillments_.size(); ++i)
        {
            for (size_t j = 0; j < subfulfillments_.size(); ++j)
            {
                if (foundEqual[j])
                    continue;

                if (c->subfulfillments_[i]->checkEqualForTesting(*subfulfillments_[j]))
                {
                    foundEqual.set(j);
                    break;
                }
            }
        }
        if (!foundEqual.all())
            return false;
    }

    {
        boost::dynamic_bitset<> foundEqual(subconditions_.size());
        for (size_t i = 0; i < subconditions_.size(); ++i)
        {
            for (size_t j = 0; j < subconditions_.size(); ++j)
            {
                if (foundEqual[j])
                    continue;

                if (c->subconditions_[i] == subconditions_[j])
                {
                    foundEqual.set(j);
                    break;
                }
            }
        }
        if (!foundEqual.all())
            return false;
    }

    return true;
}

int
ThresholdSha256::compare(Fulfillment const& rhs, der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleCompareHelper(*this, rhs, traitsCache);
}

bool
ThresholdSha256::validationDependsOnMessage() const
{
    for(auto const& f : subfulfillments_)
        if (f->validationDependsOnMessage())
            return true;

    return false;
}

}
}
