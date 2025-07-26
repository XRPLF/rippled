//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_PATHASSET_H_INCLUDED
#define RIPPLE_APP_PATHASSET_H_INCLUDED

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>

namespace ripple {

/* Represent STPathElement's asset, which can be Currency or MPTID.
 */
class PathAsset
{
private:
    std::variant<Currency, MPTID> easset_;

public:
    PathAsset() = default;
    // Enables comparing Asset and PathAsset
    PathAsset(Asset const& asset);
    PathAsset(Currency const& currency) : easset_(currency)
    {
    }
    PathAsset(MPTID const& mpt) : easset_(mpt)
    {
    }

    template <ValidPathAsset T>
    constexpr bool
    holds() const;

    constexpr bool
    isXRP() const;

    template <ValidPathAsset T>
    T const&
    get() const;

    constexpr std::variant<Currency, MPTID> const&
    value() const;

    friend constexpr bool
    operator==(PathAsset const& lhs, PathAsset const& rhs);
};

inline PathAsset::PathAsset(Asset const& asset)
{
    std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, Issue>)
                easset_ = issue.currency;
            else
                easset_ = issue.getMptID();
        },
        asset.value());
}

template <ValidPathAsset T>
constexpr bool
PathAsset::holds() const
{
    return std::holds_alternative<T>(easset_);
}

template <ValidPathAsset T>
T const&
PathAsset::get() const
{
    if (!holds<T>())
        Throw<std::runtime_error>("PathAsset doesn't hold requested asset.");
    return std::get<T>(easset_);
}

constexpr std::variant<Currency, MPTID> const&
PathAsset::value() const
{
    return easset_;
}

constexpr bool
PathAsset::isXRP() const
{
    return std::visit(
        [&]<ValidPathAsset A>(A const& a) {
            if constexpr (std::is_same_v<A, Currency>)
                return ripple::isXRP(a);
            else
                return false;
        },
        easset_);
}

constexpr bool
operator==(PathAsset const& lhs, PathAsset const& rhs)
{
    return std::visit(
        []<ValidPathAsset TLhs, ValidPathAsset TRhs>(
            TLhs const& lhs_, TRhs const& rhs_) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
                return lhs_ == rhs_;
            else
                return false;
        },
        lhs.value(),
        rhs.value());
}

template <typename Hasher>
void
hash_append(Hasher& h, PathAsset const& pathAsset)
{
    std::visit(
        [&]<ValidPathAsset T>(T const& e) { hash_append(h, e); },
        pathAsset.value());
}

inline bool
isXRP(PathAsset const& asset)
{
    return asset.isXRP();
}

std::string
to_string(PathAsset const& asset);

std::ostream&
operator<<(std::ostream& os, PathAsset const& x);

}  // namespace ripple

#endif  // RIPPLE_APP_PATHASSET_H_INCLUDED
