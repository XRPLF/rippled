//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_ASSET_H_INCLUDED
#define RIPPLE_PROTOCOL_ASSET_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>

namespace ripple {

template <typename TIss>
concept ValidIssueType =
    std::is_same_v<TIss, Issue> || std::is_same_v<TIss, MPTIssue>;

class Asset
{
private:
    using value_type = std::variant<Issue, MPTIssue>;
    value_type issue_;

public:
    Asset() = default;

    Asset(Issue const& issue);

    Asset(MPTIssue const& mpt);

    Asset(MPTID const& mpt);

    explicit operator Issue() const;

    explicit operator MPTIssue() const;

    AccountID const&
    getIssuer() const;

    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

    template <ValidIssueType TIss>
    TIss&
    get();

    template <ValidIssueType TIss>
    constexpr bool
    holds() const;

    std::string
    getText() const;

    constexpr value_type const&
    value() const;

    void
    setJson(Json::Value& jv) const;

    friend constexpr bool
    operator==(Asset const& lhs, Asset const& rhs);

    friend constexpr bool
    operator!=(Asset const& lhs, Asset const& rhs);
};

template <ValidIssueType TIss>
constexpr bool
Asset::holds() const
{
    return std::holds_alternative<TIss>(issue_);
}

template <ValidIssueType TIss>
constexpr TIss const&
Asset::get() const
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Asset is not a requested issue");
    return std::get<TIss>(issue_);
}

template <ValidIssueType TIss>
TIss&
Asset::get()
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Asset is not a requested issue");
    return std::get<TIss>(issue_);
}

constexpr Asset::value_type const&
Asset::value() const
{
    return issue_;
}

constexpr bool
operator==(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(
            TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
                return issLhs == issRhs;
            else
                return false;
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr bool
operator!=(Asset const& lhs, Asset const& rhs)
{
    return !(lhs == rhs);
}

inline bool
isXRP(Asset const& asset)
{
    return asset.holds<Issue>() && isXRP(asset.get<Issue>());
}

std::string
to_string(Asset const& asset);

bool
validJSONAsset(Json::Value const& jv);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ASSET_H_INCLUDED
