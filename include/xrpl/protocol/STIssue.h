//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_STISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_STISSUE_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

namespace ripple {

class STIssue final : public STBase, CountedObject<STIssue>
{
private:
    Asset asset_{xrpIssue()};

public:
    using value_type = Asset;

    STIssue() = default;

    explicit STIssue(SerialIter& sit, SField const& name);

    template <AssetType A>
    explicit STIssue(SField const& name, A const& issue);

    explicit STIssue(SField const& name);

    STIssue&
    operator=(STIssue const& rhs) = default;
    STIssue&
    operator=(Asset const& rhs)
    {
        asset_ = rhs;
        return *this;
    }

    template <ValidIssueType TIss>
    TIss const&
    get() const;

    template <ValidIssueType TIss>
    bool
    holds() const;

    value_type const&
    value() const noexcept;

    void
    setIssue(Asset const& issue);

    SerializedTypeID
    getSType() const override;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

template <AssetType A>
STIssue::STIssue(SField const& name, A const& asset)
    : STBase{name}, asset_{asset}
{
    if (holds<Issue>() && !isConsistent(asset_.get<Issue>()))
        Throw<std::runtime_error>(
            "Invalid asset: currency and account native mismatch");
}

STIssue
issueFromJson(SField const& name, Json::Value const& v);

template <ValidIssueType TIss>
bool
STIssue::holds() const
{
    return std::holds_alternative<TIss>(asset_.value());
}

template <ValidIssueType TIss>
TIss const&
STIssue::get() const
{
    if (!holds<TIss>(asset_))
        Throw<std::runtime_error>("Asset doesn't hold the requested issue");
    return std::get<TIss>(asset_);
}

inline STIssue::value_type const&
STIssue::value() const noexcept
{
    return asset_;
}

inline void
STIssue::setIssue(Asset const& asset)
{
    if (holds<Issue>() && !isConsistent(asset_.get<Issue>()))
        Throw<std::runtime_error>(
            "Invalid asset: currency and account native mismatch");

    asset_ = asset;
}

inline bool
operator==(STIssue const& lhs, STIssue const& rhs)
{
    return lhs.value() == rhs.value();
}

inline bool
operator!=(STIssue const& lhs, STIssue const& rhs)
{
    return !operator==(lhs, rhs);
}

inline bool
operator<(STIssue const& lhs, STIssue const& rhs)
{
    return lhs.value() < rhs.value();
}

inline bool
operator==(STIssue const& lhs, Asset const& rhs)
{
    return lhs.value() == rhs;
}

inline bool
operator<(STIssue const& lhs, Asset const& rhs)
{
    return lhs.value() < rhs;
}

}  // namespace ripple

#endif
