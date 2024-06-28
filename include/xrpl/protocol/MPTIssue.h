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

class MPTIssue
{
private:
    MPTID mptID_;

public:
    MPTIssue() = default;

    MPTIssue(MPTID const& id);

    AccountID const&
    getIssuer() const;

    MPTID const&
    getMptID() const;

    friend constexpr bool
    operator==(MPTIssue const& lhs, MPTIssue const& rhs);

    friend constexpr bool
    operator!=(MPTIssue const& lhs, MPTIssue const& rhs);
};

constexpr bool
operator==(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return lhs.mptID_ == rhs.mptID_;
}

constexpr bool
operator!=(MPTIssue const& lhs, MPTIssue const& rhs)
{
    return !(lhs.mptID_ == rhs.mptID_);
}

inline bool
isXRP(MPTID const&)
{
    return false;
}

Json::Value
to_json(MPTIssue const& issue);

std::string
to_string(MPTIssue const& mpt);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_MPTISSUE_H_INCLUDED
