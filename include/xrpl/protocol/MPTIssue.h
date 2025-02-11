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

#ifndef RIPPLE_PROTOCOL_MPTISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_MPTISSUE_H_INCLUDED

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

/* Adapt MPTID to provide the same interface as Issue. Enables using static
 * polymorphism by Asset and other classes. MPTID is a 192-bit concatenation
 * of a 32-bit account sequence and a 160-bit account id.
 */
class MPTIssue
{
private:
    MPTID mptID_;

public:
    MPTIssue() = default;

    MPTIssue(MPTID const& issuanceID);

    MPTIssue(std::uint32_t sequence, AccountID const& account);

    AccountID const&
    getIssuer() const;

    MPTID const&
    getMptID() const;

    std::string
    getText() const;

    void
    setJson(Json::Value& jv) const;

    friend constexpr bool
    operator==(MPTIssue const& lhs, MPTIssue const& rhs);

    friend constexpr std::weak_ordering
    operator<=>(MPTIssue const& lhs, MPTIssue const& rhs);

    bool
    native() const
    {
        return false;
    }
};

constexpr bool
operator==(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return lhs.mptID_ == rhs.mptID_;
}

constexpr std::weak_ordering
operator<=>(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return lhs.mptID_ <=> rhs.mptID_;
}

/** MPT is a non-native token.
 */
inline bool
isXRP(MPTID const&)
{
    return false;
}

inline AccountID const&
getMPTIssuer(MPTID const& mptid)
{
    static_assert(sizeof(MPTID) == (sizeof(std::uint32_t) + sizeof(AccountID)));
    AccountID const* accountId = reinterpret_cast<AccountID const*>(
        mptid.data() + sizeof(std::uint32_t));
    return *accountId;
}

inline MPTID
noMPT()
{
    static MPTIssue mpt{0, noAccount()};
    return mpt.getMptID();
}

inline MPTID
badMPT()
{
    static MPTIssue mpt{0, xrpAccount()};
    return mpt.getMptID();
}

template <class Hasher>
void
hash_append(Hasher& h, MPTIssue const& r)
{
    using beast::hash_append;
    hash_append(h, r.getMptID());
}

Json::Value
to_json(MPTIssue const& mptIssue);

std::string
to_string(MPTIssue const& mptIssue);

MPTIssue
mptIssueFromJson(Json::Value const& jv);

std::ostream&
operator<<(std::ostream& os, MPTIssue const& x);

}  // namespace ripple

namespace std {

template <>
struct hash<ripple::MPTID> : ripple::MPTID::hasher
{
    explicit hash() = default;
};

}  // namespace std

#endif  // RIPPLE_PROTOCOL_MPTISSUE_H_INCLUDED
