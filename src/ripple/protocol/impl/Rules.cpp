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

#include <ripple/basics/LocalValue.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rules.h>

#include <optional>

namespace ripple {

namespace {
// Use a static inside a function to help prevent order-of-initialization issues
LocalValue<std::optional<Rules>>&
getCurrentTransactionRulesRef()
{
    static LocalValue<std::optional<Rules>> r;
    return r;
}
}  // namespace

std::optional<Rules> const&
getCurrentTransactionRules()
{
    return *getCurrentTransactionRulesRef();
}

void
setCurrentTransactionRules(std::optional<Rules> r)
{
    *getCurrentTransactionRulesRef() = std::move(r);
}

class Rules::Impl
{
private:
    std::unordered_set<uint256, hardened_hash<>> set_;
    std::optional<uint256> digest_;
    std::unordered_set<uint256, beast::uhash<>> const& presets_;

public:
    explicit Impl(std::unordered_set<uint256, beast::uhash<>> const& presets)
        : presets_(presets)
    {
    }

    Impl(
        std::unordered_set<uint256, beast::uhash<>> const& presets,
        std::optional<uint256> const& digest,
        STVector256 const& amendments)
        : digest_(digest), presets_(presets)
    {
        set_.reserve(amendments.size());
        set_.insert(amendments.begin(), amendments.end());
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
        if (!digest_ && !other.digest_)
            return true;
        if (!digest_ || !other.digest_)
            return false;
        assert(presets_ == other.presets_);
        return *digest_ == *other.digest_;
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
    if (impl_.get() == other.impl_.get())
        return true;
    return *impl_ == *other.impl_;
}

bool
Rules::operator!=(Rules const& other) const
{
    return !(*this == other);
}
}  // namespace ripple
