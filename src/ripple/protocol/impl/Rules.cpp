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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rules.h>

namespace ripple {

class Rules::Impl
{
private:
    std::unordered_set<uint256, beast::uhash<>> const& presets_;
    std::unordered_set<uint256, hardened_hash<>> set_;
    std::optional<uint256> digest_;
    std::optional<std::uint32_t> sequence_;

public:
    explicit Impl(std::unordered_set<uint256, beast::uhash<>> const& presets)
        : presets_(presets)
    {
    }

    Impl(
        std::unordered_set<uint256, beast::uhash<>> const& presets,
        std::optional<uint256> const& digest,
        STVector256 const& amendments)
        : presets_(presets)
        , set_(amendments.begin(), amendments.end())
        , digest_(digest)
    {
    }

    Impl(
        std::unordered_set<uint256, beast::uhash<>> const& presets,
        std::uint32_t sequence,
        STVector256 const& amendments)
        : presets_(presets)
        , set_(amendments.begin(), amendments.end())
        , sequence_(sequence)
    {
    }

    std::unordered_set<uint256, beast::uhash<>> const&
    presets() const
    {
        return presets_;
    }

    bool
    enabled(uint256 const& feature) const
    {
        if (presets_.count(feature) > 0)
            return true;
        return set_.count(feature) > 0;
    }

    bool
    operator==(Impl const& other) const
    {
        auto ret = [this, &other]() {
            if (sequence_.has_value())
                return sequence_ == other.sequence_;

            return (digest_ == other.digest_);
        }();

        // Extra check to be sure the comparason is right
        assert(!ret || ((set_ == other.set_) && presets_ == other.presets_));

        return ret;
    }
};

Rules::Rules(std::unordered_set<uint256, beast::uhash<>> const& presets)
    : impl_(std::make_shared<Impl>(presets))
{
}

Rules::Rules(
    std::unordered_set<uint256, beast::uhash<>> const& presets,
    std::optional<uint256> const& digest,
    STVector256 const& amendments)
    : impl_(std::make_shared<Impl>(presets, digest, amendments))
{
}

Rules::Rules(
    std::unordered_set<uint256, beast::uhash<>> const& presets,
    std::uint32_t sequence,
    STVector256 const& amendments)
    : impl_(std::make_shared<Impl>(presets, sequence, amendments))
{
}

std::unordered_set<uint256, beast::uhash<>> const&
Rules::presets() const
{
    return impl_->presets();
}

bool
Rules::enabled(uint256 const& feature) const
{
    assert(impl_);

    // The functionality of the "NonFungibleTokensV1_1" amendment is
    // precisely the functionality of the following three amendments
    // so if their status is ever queried individually, we inject an
    // extra check here to simplify the checking elsewhere.
    if (feature == featureNonFungibleTokensV1 ||
        feature == fixNFTokenNegOffer || feature == fixNFTokenDirV1)
    {
        if (impl_->enabled(featureNonFungibleTokensV1_1))
            return true;
    }

    return impl_->enabled(feature);
}

bool
Rules::operator==(Rules const& other) const
{
    assert(impl_ && other.impl_);
    if (impl_ == other.impl_)
        return true;
    return *impl_ == *other.impl_;
}

bool
Rules::operator!=(Rules const& other) const
{
    return !(*this == other);
}
}  // namespace ripple
