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

#ifndef RIPPLE_CONDITIONS_THRESHOLD_SHA256_H
#define RIPPLE_CONDITIONS_THRESHOLD_SHA256_H

#include <ripple/basics/Buffer.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/Der.h>

#include <boost/optional.hpp>

#include <cstdint>

namespace ripple {
namespace cryptoconditions {

/** Fulfillment for an m-of-n collection of fulfillments

    The fulfillment contains a collection of subfulfillments. This is the
    threshold (the m in the m-of-n). It also contains a collection of
    subconditions. These are the additional conditions that will not be
    verified (but of course, are part of the condition).

    @note The number of sub-fulfillments is the m in the m-of-n. The number of
    sub-fulfillments plus the number of sub-conditions is the n in the m-of-n.
 */
class ThresholdSha256 final : public Fulfillment
{
    /** Subfulfillments to be verified. The number of subfulfillments is the
        threshold (the m in the m-of-n).
     */
    boost::container::small_vector<std::unique_ptr<Fulfillment>, 4>
        subfulfillments_;
    /** Subconditions that will not be verified (but are part of this object's
        condition).
     */
    boost::container::small_vector<Condition, 4> subconditions_;
    /** A cache of all the subconditions in this fulfillment.

       @note: This includes the conditions that will be verified (from the
       subfulfillments_ collection) plus the conditions that will not be verified
       (from the subconditions_ collection).
     */
    mutable boost::optional<boost::container::small_vector<Condition, 4>>
        cachedAllConditions;

    void
    encodeFingerprint(der::Encoder& encoder) const override;

    bool
    checkEqualForTesting(Fulfillment const& rhs) const override;

    bool
    validationDependsOnMessage() const override;
public:
    ThresholdSha256(der::Constructor const&) noexcept {};

    ThresholdSha256() = delete;
    explicit
    ThresholdSha256(
        std::vector<std::unique_ptr<Fulfillment>> subfulfillments,
        std::vector<Condition> subconditions);

    template <size_t S1, size_t S2>
    explicit ThresholdSha256(
        boost::container::small_vector<std::unique_ptr<Fulfillment>, S1>
            subfulfillments,
        boost::container::small_vector<Condition, S2> subconditions)
        : subfulfillments_(std::move(subfulfillments))
        , subconditions_(std::move(subconditions))
    {
    }

    template <class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache)
    {
        auto fulfillmentsSet = der::make_set(subfulfillments_, traitsCache);
        auto conditionsSet = der::make_set(subconditions_, traitsCache);
        f(std::tie(fulfillmentsSet, conditionsSet));
    }

    template<class F>
    void
    withTuple(F&& f, der::TraitsCache& traitsCache) const
    {
        const_cast<ThresholdSha256*>(this)->withTuple(
            std::forward<F>(f), traitsCache);
    }

    Type
    type() const override
    {
        return Type::thresholdSha256;
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
