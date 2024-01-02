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

#ifndef RIPPLE_PROTOCOL_ISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_ISSUE_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/protocol/Asset.h>
#include <ripple/protocol/UintTypes.h>

#include <cassert>
#include <functional>
#include <type_traits>

namespace ripple {

/** An asset issued by an account.
    @see Currency, AccountID, Issue, Book
*/
class Issue
{
private:
    // using IOU = std::pair<Currency, AccountID>;
    // std::variant<CFT, IOU> issue_;
    Asset asset_{};
    std::optional<AccountID> account_{std::nullopt};

public:
    Issue()
    {
    }

    Issue(Issue const& iss) : asset_(iss.asset_), account_(iss.account_)
    {
    }

    Issue(Asset const& asset, AccountID const& account)
    {
        *this = std::make_pair(asset, account);
    }

    Issue(CFT const& u) : asset_(u)
    {
        account_ = std::nullopt;
    }

    Issue&
    operator=(Issue const& issue)
    {
        asset_ = issue.asset_;
        account_ = issue.account_;
        return *this;
    }
    Issue&
    operator=(std::pair<Asset, AccountID> const& pair)
    {
        if (pair.first.isCFT())
        {
            if (pair.second != std::get<CFT>(pair.first.asset()).second)
                Throw<std::logic_error>("Issue, invalid Asset/Account");
            account_ = std::nullopt;
        }
        else
        {
            account_ = pair.second;
        }
        asset_ = pair.first;
        return *this;
    }
    Issue&
    operator=(CFT const& cft)
    {
        asset_ = cft;
        account_ = std::nullopt;
        return *this;
    }
    Issue&
    operator=(uint192 const& cftid)
    {
        std::uint32_t sequence;
        std::memcpy(&sequence, cftid.data(), sizeof(sequence));
        sequence = boost::endian::big_to_native(sequence);
        AccountID account;
        std::memcpy(
            account.begin(), cftid.begin() + sizeof(sequence), sizeof(account));
        asset_ = std::make_pair(sequence, account);
        account_ = std::nullopt;
        return *this;
    }

    Asset const&
    asset() const
    {
        return asset_;
    }
    AccountID const&
    account() const
    {
        if (asset_.isCurrency())
            return *account_;
        return std::get<CFT>(asset_.asset()).second;
    }

    void
    setIssuer(AccountID const& issuer)
    {
        if (asset_.isCurrency())
            account_ = issuer;
        else
        {
            if (issuer != static_cast<CFT>(asset_).second)
                Throw<std::logic_error>("Invalid issuer for CFT");
        }
    }

    std::string
    getText() const;

    bool
    isCFT() const
    {
        return asset_.isCFT();
    }

    friend bool
    isCFT(Issue const& i)
    {
        return i.isCFT();
    }
};

bool
isConsistent(Issue const& ac);

bool
isConsistent(Asset const& asset, AccountID const& account);

std::string
to_string(Issue const& ac);

Json::Value
to_json(Issue const& is);

Issue
issueFromJson(Json::Value const& v);

std::ostream&
operator<<(std::ostream& os, Issue const& x);

template <class Hasher>
void
hash_append(Hasher& h, Issue const& r)
{
    using beast::hash_append;
    std::visit(
        [&](auto&& arg) { hash_append(h, arg, r.account()); },
        r.asset().asset());
}

/** Equality comparison. */
/** @{ */
[[nodiscard]] inline bool
operator==(Issue const& lhs, Issue const& rhs)
{
    return (lhs.asset() == rhs.asset()) &&
        (isXRP(lhs.asset()) || lhs.account() == rhs.account());
}
/** @} */

/** Strict weak ordering. */
/** @{ */
[[nodiscard]] inline std::weak_ordering
operator<=>(Issue const& lhs, Issue const& rhs)
{
    if (auto const c{lhs.asset().asset() <=> rhs.asset().asset()}; c != 0)
        return c;

    if (isXRP(lhs.asset()) || isCFT(lhs))
        return std::weak_ordering::equivalent;

    return lhs.account() <=> rhs.account();
}
/** @} */

//------------------------------------------------------------------------------

/** Returns an asset specifier that represents XRP. */
inline Issue const&
xrpIssue()
{
    static Issue issue{xrpCurrency(), xrpAccount()};
    return issue;
}

/** Returns an asset specifier that represents no account and currency. */
inline Issue const&
noIssue()
{
    static Issue issue{noCurrency(), noAccount()};
    return issue;
}
/** Returns an asset specifier that represents no account and no cft. */
inline Issue const&
noCftIssue()
{
    static Issue issue{noCFT()};
    return issue;
}

}  // namespace ripple

#endif
